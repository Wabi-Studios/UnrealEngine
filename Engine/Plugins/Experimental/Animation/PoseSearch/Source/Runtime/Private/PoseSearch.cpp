// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#if WITH_EDITORONLY_DATA
#include "PoseSearch/PoseSearchFeatureChannels.h"
#endif // WITH_EDITORONLY_DATA
#include "PoseSearchDerivedData.h"
#include "PoseSearchEigenHelper.h"

#include "Algo/BinarySearch.h"
#include "Async/ParallelFor.h"
#include "Templates/IdentityFunctor.h"
#include "Templates/Invoke.h"
#include "Templates/Less.h"
#include "Features/IModularFeatures.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimPoseSearchProvider.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/MirrorDataTable.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Trace/PoseSearchTraceLogger.h"
#include "UObject/ObjectSaveContext.h"
#include "Misc/MemStack.h"

#include "Animation/BuiltInAttributeTypes.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace1D.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#endif // WITH_EDITOR

IMPLEMENT_ANIMGRAPH_MESSAGE(UE::PoseSearch::IPoseHistoryProvider);

#define LOCTEXT_NAMESPACE "PoseSearch"

DEFINE_LOG_CATEGORY(LogPoseSearch);

DECLARE_STATS_GROUP(TEXT("PoseSearch"), STATGROUP_PoseSearch, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Search Brute Force"), STAT_PoseSearchBruteForce, STATGROUP_PoseSearch, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Search PCA/KNN"), STAT_PoseSearchPCAKNN, STATGROUP_PoseSearch, );
DEFINE_STAT(STAT_PoseSearchBruteForce);
DEFINE_STAT(STAT_PoseSearchPCAKNN);

namespace UE { namespace PoseSearch {

//////////////////////////////////////////////////////////////////////////
// Constants and utilities

// Temporarily disable parallel indexing due to a bug
// A single BoneContainer is being used while indexing assets, but some
// const BoneContainer methods change mutable lookup tables.
#define UE_POSE_SEARCH_FORCE_SINGLE_THREAD 1

#ifndef UE_POSE_SEARCH_FORCE_SINGLE_THREAD
#define UE_POSE_SEARCH_FORCE_SINGLE_THREAD 0
#endif

#if UE_POSE_SEARCH_FORCE_SINGLE_THREAD
	constexpr EParallelForFlags ParallelForFlags = EParallelForFlags::ForceSingleThread;
#else
	constexpr EParallelForFlags ParallelForFlags = EParallelForFlags::None;
#endif // UE_POSE_SEARCH_FORCE_SINGLE_THREAD

using RowMajorVector = Eigen::Matrix<float, 1, Eigen::Dynamic, Eigen::RowMajor>;
using RowMajorVectorMap = Eigen::Map<RowMajorVector, Eigen::RowMajor>;
using RowMajorVectorMapConst = Eigen::Map<const RowMajorVector, Eigen::RowMajor>;

using RowMajorMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
using RowMajorMatrixMap = Eigen::Map<RowMajorMatrix, Eigen::RowMajor>;
using RowMajorMatrixMapConst = Eigen::Map<const RowMajorMatrix, Eigen::RowMajor>;

using ColMajorMatrix = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;
using ColMajorMatrixMap = Eigen::Map<ColMajorMatrix, Eigen::ColMajor>;
using ColMajorMatrixMapConst = Eigen::Map<const ColMajorMatrix, Eigen::ColMajor>;

static bool IsSamplingRangeValid(FFloatInterval Range)
{
	return Range.IsValid() && (Range.Min >= 0.0f);
}

static inline float CompareFeatureVectors(int32 NumValues, const float* A, const float* B, const float* Weights)
{
	Eigen::Map<const Eigen::ArrayXf> VA(A, NumValues);
	Eigen::Map<const Eigen::ArrayXf> VB(B, NumValues);
	Eigen::Map<const Eigen::ArrayXf> VW(Weights, NumValues);

	return ((VA - VB).square() * VW).sum();
}

static inline float CompareFeatureVectors(int32 NumValues, const float* A, const float* B)
{
	Eigen::Map<const Eigen::ArrayXf> VA(A, NumValues);
	Eigen::Map<const Eigen::ArrayXf> VB(B, NumValues);

	return (VA - VB).square().sum();
}

static FFloatInterval GetEffectiveSamplingRange(const UAnimSequenceBase* Sequence, FFloatInterval RequestedSamplingRange)
{
	const bool bSampleAll = (RequestedSamplingRange.Min == 0.0f) && (RequestedSamplingRange.Max == 0.0f);
	const float SequencePlayLength = Sequence->GetPlayLength();
	FFloatInterval Range;
	Range.Min = bSampleAll ? 0.0f : RequestedSamplingRange.Min;
	Range.Max = bSampleAll ? SequencePlayLength : FMath::Min(SequencePlayLength, RequestedSamplingRange.Max);
	return Range;
}


/**
* Algo::LowerBound adapted to TIndexedContainerIterator for use with indexable but not necessarily contiguous containers. Used here with TRingBuffer.
*
* Performs binary search, resulting in position of the first element >= Value using predicate
*
* @param First TIndexedContainerIterator beginning of range to search through, must be already sorted by SortPredicate
* @param Last TIndexedContainerIterator end of range
* @param Value Value to look for
* @param SortPredicate Predicate for sort comparison, defaults to <
*
* @returns Position of the first element >= Value, may be position after last element in range
*/
template <typename IteratorType, typename ValueType, typename ProjectionType, typename SortPredicateType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value, ProjectionType Projection, SortPredicateType SortPredicate) -> decltype(First.GetIndex())
{
	using SizeType = decltype(First.GetIndex());

	check(First.GetIndex() <= Last.GetIndex());

	// Current start of sequence to check
	SizeType Start = First.GetIndex();

	// Size of sequence to check
	SizeType Size = Last.GetIndex() - Start;

	// With this method, if Size is even it will do one more comparison than necessary, but because Size can be predicted by the CPU it is faster in practice
	while (Size > 0)
	{
		const SizeType LeftoverSize = Size % 2;
		Size = Size / 2;

		const SizeType CheckIndex = Start + Size;
		const SizeType StartIfLess = CheckIndex + LeftoverSize;

		auto&& CheckValue = Invoke(Projection, *(First + CheckIndex));
		Start = SortPredicate(CheckValue, Value) ? StartIfLess : Start;
	}
	return Start;
}

template <typename IteratorType, typename ValueType, typename SortPredicateType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value, SortPredicateType SortPredicate) -> decltype(First.GetIndex())
{
	return LowerBound(First, Last, Value, FIdentityFunctor(), SortPredicate);
}

template <typename IteratorType, typename ValueType>
FORCEINLINE auto LowerBound(IteratorType First, IteratorType Last, const ValueType& Value) -> decltype(First.GetIndex())
{
	return LowerBound(First, Last, Value, FIdentityFunctor(), TLess<>());
}


//////////////////////////////////////////////////////////////////////////
// FFeatureTypeTraits

struct FFeatureTypeTraits
{
	EPoseSearchFeatureType Type = EPoseSearchFeatureType::Invalid;
	uint32 NumFloats = 0;
};

// Could upgrade to class objects in the future with value reader/writer functions
static constexpr FFeatureTypeTraits FeatureTypeTraits[] =
{
	{ EPoseSearchFeatureType::Position, 3 },
	{ EPoseSearchFeatureType::Rotation, 6 },
	{ EPoseSearchFeatureType::LinearVelocity, 3 },
	{ EPoseSearchFeatureType::AngularVelocity, 3 },
	{ EPoseSearchFeatureType::ForwardVector, 3 },
};

static FFeatureTypeTraits GetFeatureTypeTraits(EPoseSearchFeatureType Type)
{
	// Could allow external registration to a TSet of traits in the future
	// For now just use a simple local array
	for (const FFeatureTypeTraits& Traits : FeatureTypeTraits)
	{
		if (Traits.Type == Type)
		{
			return Traits;
		}
	}

	return FFeatureTypeTraits();
}

static void CalcChannelCosts(const UPoseSearchSchema* Schema, TArrayView<const float> CostVector, TArray<float>& OutChannelCosts)
{
	OutChannelCosts.Reset();
	OutChannelCosts.AddZeroed(Schema->Channels.Num());
	for (int ChannelIdx = 0; ChannelIdx != Schema->Channels.Num(); ++ChannelIdx)
	{
		for (int FeatureIdx = INDEX_NONE; Schema->Layout.EnumerateBy(ChannelIdx, EPoseSearchFeatureType::Invalid, FeatureIdx); /*empty*/)
		{
			const FPoseSearchFeatureDesc& Feature = Schema->Layout.Features[FeatureIdx];
			int32 ValueSize = GetFeatureTypeTraits(Feature.Type).NumFloats;
			int32 ValueTerm = Feature.ValueOffset + ValueSize;
			for (int32 ValueIdx = Feature.ValueOffset; ValueIdx != ValueTerm; ++ValueIdx)
			{
				OutChannelCosts[ChannelIdx] += CostVector[ValueIdx];
			}
		}
	}
}
}} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureDesc

bool FPoseSearchFeatureDesc::operator==(const FPoseSearchFeatureDesc& Other) const
{
	return
		(ChannelIdx == Other.ChannelIdx) &&
		(ChannelFeatureId == Other.ChannelFeatureId) &&
		(SubsampleIdx == Other.SubsampleIdx) &&
		(Type == Other.Type);
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorLayout

void FPoseSearchFeatureVectorLayout::Finalize()
{
	uint32 FloatCount = 0;

	// Initialize value offsets
	for (FPoseSearchFeatureDesc& Feature : Features)
	{
		Feature.ValueOffset = FloatCount;

		uint32 FeatureNumFloats = UE::PoseSearch::GetFeatureTypeTraits(Feature.Type).NumFloats;
		FloatCount += FeatureNumFloats;
	}

	NumFloats = FloatCount;
}

void FPoseSearchFeatureVectorLayout::Reset()
{
	Features.Reset();
	NumFloats = 0;
}

bool FPoseSearchFeatureVectorLayout::IsValid(int32 ChannelCount) const
{
	if (NumFloats == 0)
	{
		return false;
	}

	for (const FPoseSearchFeatureDesc& Feature : Features)
	{
		if (Feature.ChannelIdx >= ChannelCount)
		{
			return false;
		}
	}

	return true;
}

bool FPoseSearchFeatureVectorLayout::EnumerateBy(int32 ChannelIdx, EPoseSearchFeatureType Type, int32& InOutFeatureIdx) const
{
	auto IsChannelMatch = [](int32 ChannelIdx, const FPoseSearchFeatureDesc& Feature) -> bool
	{
		bool bChannelMatch = true;
		if (ChannelIdx >= 0)
		{
			bChannelMatch = Feature.ChannelIdx == ChannelIdx;
		}
		return bChannelMatch;
	};

	auto IsTypeMatch = [](EPoseSearchFeatureType Type, const FPoseSearchFeatureDesc& Feature) -> bool
	{
		bool bTypeMatch = true;
		if (Type != EPoseSearchFeatureType::Invalid)
		{
			bTypeMatch = Feature.Type == Type;
		}
		return bTypeMatch;
	};

	for (int32 Size = Features.Num(); ++InOutFeatureIdx < Size; )
	{
		const FPoseSearchFeatureDesc& Feature = Features[InOutFeatureIdx];

		bool bChannelMatch = IsChannelMatch(ChannelIdx, Feature);
		bool bTypeMatch = IsTypeMatch(Type, Feature);

		if (bChannelMatch && bTypeMatch)
		{
			return true;
		}
	}

	return false;
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchBone

uint32 FPoseSearchBone::GetTypeMask() const
{
	uint32 Mask = 0;
	
	if (bUsePosition)
	{
		Mask |= 1 << static_cast<int>(EPoseSearchFeatureType::Position);
	}
	if (bUseVelocity)
	{
		Mask |= 1 << static_cast<int>(EPoseSearchFeatureType::LinearVelocity);
	}
	if (bUseRotation)
	{
		Mask |= 1 << static_cast<int>(EPoseSearchFeatureType::Rotation);
	}

	return Mask;
}


//////////////////////////////////////////////////////////////////////////
// UPoseSearchSchema

namespace UE::PoseSearch {

int32 FSchemaInitializer::AddBoneReference(const FBoneReference& BoneReference)
{
	check(BoneReferences.Num() + 1 <= UPoseSearchSchema::MaxBoneReferences);
	return BoneReferences.AddUnique(BoneReference);
}

int32 FSchemaInitializer::AddFeatureDesc(const FPoseSearchFeatureDesc& FeatureDesc)
{
	check(FeatureDesc.ChannelIdx == CurrentChannelIdx);
	check(Features.Num() + 1 <= UPoseSearchSchema::MaxFeatures);
	check(!Features.Contains(FeatureDesc));
	return Features.Add(FeatureDesc);
}

} // namespace UE::PoseSearch

void UPoseSearchSchema::Finalize()
{
	using namespace UE::PoseSearch;

	SampleRate = FMath::Clamp(SampleRate, 1, 60);
	SamplingInterval = 1.0f / SampleRate;

	// Discard null channels
	Channels.RemoveAll([](TObjectPtr<UPoseSearchFeatureChannel>& Channel) { return Channel.IsNull(); });

	Layout.Reset();
	BoneReferences.Reset();

	FSchemaInitializer Initializer;
	for (int32 ChannelIdx = 0; ChannelIdx != Channels.Num(); ++ChannelIdx)
	{
		TObjectPtr<UPoseSearchFeatureChannel>& Channel = Channels[ChannelIdx];
		Channel->ChannelIdx = ChannelIdx;

		Initializer.CurrentChannelIdx = ChannelIdx;
		Channel->InitializeSchema(Initializer);
	}

	Layout.Features = MoveTemp(Initializer.Features);
	BoneReferences = MoveTemp(Initializer.BoneReferences);

	Layout.Finalize();

	EffectiveDataPreprocessor = DataPreprocessor;
	if (EffectiveDataPreprocessor == EPoseSearchDataPreprocessor::Automatic)
	{
		EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Normalize;
	}

	ResolveBoneReferences();
}

void UPoseSearchSchema::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Finalize();

	Super::PreSave(ObjectSaveContext);
}

void UPoseSearchSchema::PostLoad()
{
	Super::PostLoad();

	bool bNeedFinalize = false;

	// Migrate deprecated schema properties into channels
#if WITH_EDITORONLY_DATA
	if (!TrajectorySampleTimes_DEPRECATED.IsEmpty())
	{
		bNeedFinalize = true;

		UPoseSearchFeatureChannel_Trajectory* Channel = NewObject<UPoseSearchFeatureChannel_Trajectory>(this);
		Channel->Domain = EPoseSearchFeatureDomain::Time;
		Channel->SampleOffsets = TrajectorySampleTimes_DEPRECATED;
		Channel->bUseFacingDirections = bUseTrajectoryForwardVectors_DEPRECATED;
		Channel->bUseLinearVelocities = bUseTrajectoryVelocities_DEPRECATED;
		Channel->bUsePositions = bUseTrajectoryPositions_DEPRECATED;
		Channels.Add(Channel);

		TrajectorySampleTimes_DEPRECATED.Empty();
	}

	if (!TrajectorySampleDistances_DEPRECATED.IsEmpty())
	{
		bNeedFinalize = true;

		UPoseSearchFeatureChannel_Trajectory* Channel = NewObject<UPoseSearchFeatureChannel_Trajectory>(this);
		Channel->Domain = EPoseSearchFeatureDomain::Distance;
		Channel->SampleOffsets = TrajectorySampleDistances_DEPRECATED;
		Channel->bUseFacingDirections = bUseTrajectoryForwardVectors_DEPRECATED;
		Channel->bUseLinearVelocities = bUseTrajectoryVelocities_DEPRECATED;
		Channel->bUsePositions = bUseTrajectoryPositions_DEPRECATED;
		Channels.Add(Channel);

		TrajectorySampleDistances_DEPRECATED.Empty();
	}

	if (!PoseSampleTimes_DEPRECATED.IsEmpty())
	{
		bNeedFinalize = true;

		UPoseSearchFeatureChannel_Pose* Channel = NewObject<UPoseSearchFeatureChannel_Pose>(this);
		Channel->SampledBones = SampledBones_DEPRECATED;
		Channel->SampleTimes = PoseSampleTimes_DEPRECATED;
		Channels.Add(Channel);

		SampledBones_DEPRECATED.Empty();
		PoseSampleTimes_DEPRECATED.Empty();
	}
#endif // WITH_EDITORONLY_DATA

	if (bNeedFinalize)
	{
		Finalize();
	}
	else
	{
		ResolveBoneReferences();
	}
}

bool UPoseSearchSchema::IsValid() const
{
	bool bValid = Skeleton != nullptr;

	for (const FBoneReference& BoneRef : BoneReferences)
	{
		bValid &= BoneRef.HasValidSetup();
	}

	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel: Channels)
	{
		bValid &= !Channel.IsNull();
	}

	bValid &= (BoneReferences.Num() == BoneIndices.Num());

	bValid &= Layout.IsValid(Channels.Num());
	
	return bValid;
}

FFloatRange UPoseSearchSchema::GetHorizonRange(EPoseSearchFeatureDomain Domain) const
{
	FFloatRange GlobalRange = FFloatRange::Empty();
	for (const auto & Channel : Channels)
	{
		if (Channel)
		{
			FFloatRange ChannelRange = Channel->GetHorizonRange(Domain);
			GlobalRange = FFloatRange::Hull(GlobalRange, ChannelRange);
		}
	}

	return GlobalRange;
}

TArrayView<const float> UPoseSearchSchema::GetChannelSampleOffsets(int32 ChannelIdx) const
{
	if (Channels.IsValidIndex(ChannelIdx))
	{
		const auto & Channel = Channels[ChannelIdx];
		if (Channel)
		{
			return Channel->GetSampleOffsets();
		}
	}

	return {};
}

void UPoseSearchSchema::ResolveBoneReferences()
{
	// Initialize references to obtain bone indices
	for (FBoneReference& BoneRef : BoneReferences)
	{
		BoneRef.Initialize(Skeleton);
	}

	// Fill out bone index array
	BoneIndices.SetNum(BoneReferences.Num());
	for (int32 BoneRefIdx = 0; BoneRefIdx != BoneReferences.Num(); ++BoneRefIdx)
	{
		BoneIndices[BoneRefIdx] = BoneReferences[BoneRefIdx].BoneIndex;
	}

	// Build separate index array with parent indices guaranteed to be present. Sort for EnsureParentsPresent.
	BoneIndicesWithParents = BoneIndices;
	BoneIndicesWithParents.Sort();

	if (Skeleton)
	{
		FAnimationRuntime::EnsureParentsPresent(BoneIndicesWithParents, Skeleton->GetReferenceSkeleton());
	}

	// BoneIndicesWithParents should at least contain the root to support mirroring root motion
	if (BoneIndicesWithParents.Num() == 0)
	{
		BoneIndicesWithParents.Add(0);
	}
}



//////////////////////////////////////////////////////////////////////////
// FPoseSearchChannelWeightParams

FPoseSearchChannelWeightParams::FPoseSearchChannelWeightParams()
{
	TypeWeights.Empty((int32)EPoseSearchFeatureType::Num);
	for (int32 Type = 0; Type != (int32)EPoseSearchFeatureType::Num; ++Type)
	{
		TypeWeights.Add((EPoseSearchFeatureType)Type, 1.0f);
	}
}



//////////////////////////////////////////////////////////////////////////
// FPoseSearchWeightParams
const FPoseSearchChannelWeightParams* FPoseSearchWeightParams::GetChannelWeights(int32 ChannelIdx) const
{
	return ChannelWeights.IsValidIndex(ChannelIdx) ? &ChannelWeights[ChannelIdx] : nullptr;
}



//////////////////////////////////////////////////////////////////////////
// FPoseSearchWeights

void FPoseSearchWeights::Init(const FPoseSearchWeightParams& WeightParams, const UPoseSearchSchema* Schema)
{
	using namespace UE::PoseSearch;
	using namespace Eigen;

	// Convenience enum for indexing by horizon
	enum EHorizon : int
	{
		History,
		Prediction,
		Num
	};

	// Initialize weights
	Weights.Reset();
	Weights.SetNumZeroed(Schema->Layout.NumFloats);

	// Setup channel indexable weight params
	const int32 ChannelNum = Schema->Channels.Num();

	// Normalize channel weights
	Eigen::Array<float, Dynamic, 1> NormalizedChannelWeights(ChannelNum);
	for (int ChannelIdx = 0; ChannelIdx != ChannelNum; ++ChannelIdx)
	{
		const FPoseSearchChannelWeightParams* ChannelWeights = WeightParams.GetChannelWeights(ChannelIdx);
		NormalizedChannelWeights[ChannelIdx] = ChannelWeights ? ChannelWeights->ChannelWeight : 1.0f;

		// Zero the channel weight if there are no features in this channel
		int32 FeatureIdx = INDEX_NONE;
		if (!Schema->Layout.EnumerateBy(ChannelIdx, EPoseSearchFeatureType::Invalid, FeatureIdx))
		{
			NormalizedChannelWeights[ChannelIdx] = 0.0f;
		}
	}

	const float ChannelWeightSum = NormalizedChannelWeights.sum();
	if (!FMath::IsNearlyZero(ChannelWeightSum))
	{
		NormalizedChannelWeights *= (1.0f / ChannelWeightSum);
	}	


	// Determine maximum number of channel sample offsets for allocation
	int32 MaxChannelSampleOffsets = 0;
	for (int ChannelIdx = 0; ChannelIdx != ChannelNum; ++ChannelIdx)
	{
		TArrayView<const float> ChannelSampleOffsets = Schema->GetChannelSampleOffsets(ChannelIdx);
		MaxChannelSampleOffsets = FMath::Max(MaxChannelSampleOffsets, ChannelSampleOffsets.Num());
	}

	// WeightsByFeature is indexed by FeatureIdx in a Layout
	TArray<float, TInlineAllocator<32>> WeightsByFeatureStorage;
	WeightsByFeatureStorage.SetNum(Schema->Layout.Features.Num());
	Eigen::Map<ArrayXf> WeightsByFeature(WeightsByFeatureStorage.GetData(), WeightsByFeatureStorage.Num());

	// HorizonWeightsBySample is indexed by the channel's sample offsets in a Schema
	TArray<float, TInlineAllocator<16>> HorizonWeightsBySampleStorage;
	HorizonWeightsBySampleStorage.SetNum(MaxChannelSampleOffsets);
	Eigen::Map<ArrayXf> HorizonWeightsBySample(HorizonWeightsBySampleStorage.GetData(), HorizonWeightsBySampleStorage.Num());

	// WeightsByType is indexed by feature type
	Eigen::Array<float, (int)EPoseSearchFeatureType::Num, 1> WeightsByType;


	// Determine each channel's feature weights
	for (int ChannelIdx = 0; ChannelIdx != ChannelNum; ++ChannelIdx)
	{
		// Ignore this channel entirely if it has no weight
		if (FMath::IsNearlyZero(NormalizedChannelWeights[ChannelIdx]))
		{
			continue;
		}

		// Get channel info
		const FPoseSearchChannelWeightParams* ChannelWeights = WeightParams.GetChannelWeights(ChannelIdx);
		TArrayView<const float> ChannelSampleOffsets = Schema->GetChannelSampleOffsets(ChannelIdx);

		// Reset scratch weights
		WeightsByFeature.setConstant(0.0f);
		WeightsByType.setConstant(0.0f);
		HorizonWeightsBySample.setConstant(0.0f);


		// Initialize weights by type lookup
		for (int Type = 0; Type != (int)EPoseSearchFeatureType::Num; ++Type)
		{
			WeightsByType[Type] = ChannelWeights ? ChannelWeights->TypeWeights.FindRef((EPoseSearchFeatureType)Type) : 1.0f;

			// Zero the weight if this channel doesn't have any features using this type
			int32 FeatureIdx = INDEX_NONE;
			if (!Schema->Layout.EnumerateBy(ChannelIdx, (EPoseSearchFeatureType)Type, FeatureIdx))
			{
				WeightsByType[Type] = 0.0f;
			}
		}

		// Normalize type weights
		float TypeWeightsSum = WeightsByType.sum();
		if (!FMath::IsNearlyZero(TypeWeightsSum))
		{
			WeightsByType *= (1.0f / TypeWeightsSum);
		}
		else
		{
			// Ignore this channel entirely if there are no types contributing weight
			continue;
		}


		// Determine the range of sample offsets that make up the history and prediction horizons
		FInt32Range HorizonSampleIdxRanges[EHorizon::Num];
		{
			int32 IdxUpper = Algo::UpperBound(ChannelSampleOffsets, 0.0f);
			int32 IdxLower = ChannelSampleOffsets[0] <= 0.0f ? 0 : IdxUpper;
			HorizonSampleIdxRanges[EHorizon::History] = FInt32Range(IdxLower, IdxUpper);

			IdxLower = IdxUpper;
			IdxUpper = ChannelSampleOffsets.Num();
			HorizonSampleIdxRanges[EHorizon::Prediction] = FInt32Range(IdxLower, IdxUpper);
		}


		// Initialize horizon weights
		Eigen::Array<float, 1, EHorizon::Num> NormalizedHorizonWeights;
		NormalizedHorizonWeights.setConstant(0.0f);

		if (!HorizonSampleIdxRanges[EHorizon::History].IsEmpty())
		{
			NormalizedHorizonWeights[EHorizon::History] = ChannelWeights ? ChannelWeights->HistoryParams.Weight : 1.0f;
		}
		if (!HorizonSampleIdxRanges[EHorizon::Prediction].IsEmpty())
		{
			NormalizedHorizonWeights[EHorizon::Prediction] = ChannelWeights ? ChannelWeights->PredictionParams.Weight : 1.0f;
		}
		
		// Normalize horizon weights
		float HorizonWeightSum = NormalizedHorizonWeights.sum();
		if (!FMath::IsNearlyZero(HorizonWeightSum))
		{
			NormalizedHorizonWeights *= (1.0f / HorizonWeightSum);
		}
		else
		{
			// Ignore this channel entirely if the horizons don't contribute any weight
			continue;
		}


		auto SetHorizonSampleWeights = [&HorizonWeightsBySample, &ChannelSampleOffsets](FInt32Range SampleIdxRange, const FPoseSearchChannelHorizonParams* HorizonParams)
		{
			// Segment length represents the number of sample offsets in the span that make up this horizon
			int32 SegmentLength = SampleIdxRange.Size<int32>();

			if (SegmentLength > 0)
			{
				int32 SegmentBegin = SampleIdxRange.GetLowerBoundValue();
				if (HorizonParams && HorizonParams->bInterpolate && SegmentLength > 1)
				{
					// We'll map the range spanned by the horizon's sample offsets to the interpolation range
					// The interpolation range is 0 to 1 unless an initial value was set
					// The initial value allows the user to set a minimum weight or reverse the lerp direction
					// We'll normalize these weights in the next step
					FVector2f InputRange(ChannelSampleOffsets[SegmentBegin], ChannelSampleOffsets[SegmentBegin + SegmentLength - 1]);
					FVector2f OutputRange(HorizonParams->InitialValue, 1.0f - HorizonParams->InitialValue);

					for (int32 OffsetIdx = SegmentBegin; OffsetIdx != SegmentBegin + SegmentLength; ++OffsetIdx)
					{
						float SampleOffset = ChannelSampleOffsets[OffsetIdx];
						float Alpha = FMath::GetMappedRangeValueUnclamped(InputRange, OutputRange, SampleOffset);
						float Weight = FAlphaBlend::AlphaToBlendOption(Alpha, HorizonParams->InterpolationMethod);
						HorizonWeightsBySample[OffsetIdx] = Weight;
					}
				}
				else
				{
					// If we're not interpolating weights across this horizon, just give them all equal weight
					HorizonWeightsBySample.segment(SegmentBegin, SegmentLength).setConstant(1.0f);
				}

				// Normalize weights within the horizon's segment of sample offsets
				float HorizonSum = HorizonWeightsBySample.segment(SegmentBegin, SegmentLength).sum();
				if (!FMath::IsNearlyZero(HorizonSum))
				{
					HorizonWeightsBySample.segment(SegmentBegin, SegmentLength) *= 1.0f / HorizonSum;
				}
			}
		};

		SetHorizonSampleWeights(
			HorizonSampleIdxRanges[EHorizon::History],
			ChannelWeights ? &ChannelWeights->HistoryParams : nullptr
		);
		SetHorizonSampleWeights(
			HorizonSampleIdxRanges[EHorizon::Prediction],
			ChannelWeights ? &ChannelWeights->PredictionParams : nullptr
		);

		// Now set this channel's weights for every feature in each horizon
		Eigen::Array<float, 1, EHorizon::Num> HorizonSums;
		HorizonSums = 0.0f;
		for (int FeatureIdx = INDEX_NONE; Schema->Layout.EnumerateBy(ChannelIdx, EPoseSearchFeatureType::Invalid, FeatureIdx); /*empty*/)
		{
			const FPoseSearchFeatureDesc& Feature = Schema->Layout.Features[FeatureIdx];

			for (int HorizonIdx = 0; HorizonIdx != EHorizon::Num; ++HorizonIdx)
			{
				if (HorizonSampleIdxRanges[HorizonIdx].Contains(Feature.SubsampleIdx))
				{
					int HorizonSize = HorizonSampleIdxRanges[HorizonIdx].Size<int>();
					
					// Bug: HorizonSize * WeightsByType[(int)Feature.Type] assumes horizons are composed of homogenous features, not true any more for pose channel
					WeightsByFeature[FeatureIdx] = HorizonWeightsBySample[Feature.SubsampleIdx] * (HorizonSize * WeightsByType[(int)Feature.Type]);
					HorizonSums[HorizonIdx] += WeightsByFeature[FeatureIdx];
					break;
				}
			}
		}

		// Scale feature weights within horizons so that they have the desired total horizon weight
		for (int FeatureIdx = INDEX_NONE; Schema->Layout.EnumerateBy(ChannelIdx, EPoseSearchFeatureType::Invalid, FeatureIdx); /*empty*/)
		{
			const FPoseSearchFeatureDesc& Feature = Schema->Layout.Features[FeatureIdx];

			for (int HorizonIdx = 0; HorizonIdx != EHorizon::Num; ++HorizonIdx)
			{
				if (HorizonSampleIdxRanges[HorizonIdx].Contains(Feature.SubsampleIdx))
				{
					float HorizonWeight = NormalizedHorizonWeights[HorizonIdx] / HorizonSums[HorizonIdx];
					WeightsByFeature[FeatureIdx] *= HorizonWeight;
					break;
				}
			}
		}

		// Scale all features in all horizons so they have the desired channel weight
		WeightsByFeature *= NormalizedChannelWeights[ChannelIdx];

		// Weights should sum to channel weight at this point
		ensure(FMath::IsNearlyEqual(WeightsByFeature.sum(), NormalizedChannelWeights[ChannelIdx], KINDA_SMALL_NUMBER));

		// Merge feature weights for channel into per-value weights buffer
		// Weights are replicated per feature dimension so the cost function can directly index weights by value index
		for (int FeatureIdx = INDEX_NONE; Schema->Layout.EnumerateBy(ChannelIdx, EPoseSearchFeatureType::Invalid, FeatureIdx); /*empty*/)
		{
			const FPoseSearchFeatureDesc& Feature = Schema->Layout.Features[FeatureIdx];
			int32 ValueSize = GetFeatureTypeTraits(Feature.Type).NumFloats;
			int32 ValueTerm = Feature.ValueOffset + ValueSize;
			for (int32 ValueIdx = Feature.ValueOffset; ValueIdx != ValueTerm; ++ValueIdx)
			{
				Weights[ValueIdx] = WeightsByFeature[FeatureIdx];
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchWeightsContext

void FPoseSearchWeightsContext::Update(const UPoseSearchDatabase* ActiveDatabase)
{
	bool bRecomputeWeights = false;
	if (Database != ActiveDatabase)
	{
		Database = ActiveDatabase;
#if WITH_EDITOR
		SearchIndexHash = Database->GetSearchIndexHash();
#endif
		bRecomputeWeights = true;
	}

#if WITH_EDITOR
	if (Database.IsValid() && Database->GetSearchIndexHash() != SearchIndexHash)
	{
		SearchIndexHash = Database->GetSearchIndexHash();
		bRecomputeWeights = true;
	}
#endif

	if (bRecomputeWeights)
	{
		ComputedDefaultGroupWeights.Init(Database->DefaultWeights, Database->Schema);

		int32 NumGroups = ActiveDatabase ? ActiveDatabase->Groups.Num() : 0;
		ComputedGroupWeights.SetNum(NumGroups);

		for (int GroupIdx = 0; GroupIdx < NumGroups; ++GroupIdx)
		{
			ComputedGroupWeights[GroupIdx].Init(
				Database->Groups[GroupIdx].Weights, 
				Database->Schema);
		}
	}
}

const FPoseSearchWeights* FPoseSearchWeightsContext::GetGroupWeights(int32 WeightsGroupIdx) const
{
	if (WeightsGroupIdx == INDEX_NONE)
	{
		return &ComputedDefaultGroupWeights;
	}

	if (ComputedGroupWeights.IsValidIndex(WeightsGroupIdx))
	{
		return &ComputedGroupWeights[WeightsGroupIdx];
	}

	return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndexAsset
// 

int32 FPoseSearchIndex::FindAssetIndex(const FPoseSearchIndexAsset* Asset) const
{
	if (Asset == nullptr || Assets.Num() == 0)
	{
		return INDEX_NONE;
	}

	const FPoseSearchIndexAsset* Start = &Assets[0];
	int32 Result = Asset - Start;

	if (!Assets.IsValidIndex(Result))
	{
		return INDEX_NONE;
	}

	return Result;
}

const FPoseSearchIndexAsset* FPoseSearchIndex::FindAssetForPose(int32 PoseIdx) const
{
	auto Predicate = [PoseIdx](const FPoseSearchIndexAsset& Asset)
	{
		return Asset.IsPoseInRange(PoseIdx);
	};
	return Assets.FindByPredicate(Predicate);
}

float FPoseSearchIndex::GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* Asset) const
{
	if (!Asset)
	{
		Asset = FindAssetForPose(PoseIdx);
		if (!Asset)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("Couldn't find asset for pose %i in database"), PoseIdx);
			return -1.0f;
		}
	}

	if (!Asset->IsPoseInRange(PoseIdx))
	{
		UE_LOG(LogPoseSearch, Error, TEXT("Pose %i out of range in database"), PoseIdx);
		return -1.0f;
	}

	if (Asset->Type == ESearchIndexAssetType::Sequence)
	{
		const FFloatInterval SamplingRange = Asset->SamplingInterval;

		float AssetTime = FMath::Min(
			SamplingRange.Min + Schema->SamplingInterval * (PoseIdx - Asset->FirstPoseIdx),
			SamplingRange.Max);
		return AssetTime;
	}
	else if (Asset->Type == ESearchIndexAssetType::BlendSpace)
	{
		const FFloatInterval SamplingRange = Asset->SamplingInterval;

		// For BlendSpaces the AssetTime is in the range [0, 1] while the Sampling Range
		// is in real time (seconds)
		float AssetTime = FMath::Min(
			SamplingRange.Min + Schema->SamplingInterval * (PoseIdx - Asset->FirstPoseIdx),
			SamplingRange.Max) / (Asset->NumPoses * Schema->SamplingInterval);
		return AssetTime;
	}
	else
	{
		checkNoEntry();
		return -1.0f;
	}
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchIndex

bool FPoseSearchIndex::IsValid() const
{
	const bool bSchemaValid = Schema && Schema->IsValid();
	const bool bSearchIndexValid = bSchemaValid && (NumPoses * Schema->Layout.NumFloats == Values.Num());

	return bSearchIndexValid;
}

bool FPoseSearchIndex::IsEmpty() const
{
	const bool bEmpty = Assets.Num() == 0 || NumPoses == 0;
	return bEmpty;
}

TArrayView<const float> FPoseSearchIndex::GetPoseValues(int32 PoseIdx) const
{
	check(PoseIdx < NumPoses);
	int32 ValueOffset = PoseIdx * Schema->Layout.NumFloats;
	return MakeArrayView(&Values[ValueOffset], Schema->Layout.NumFloats);
}

void FPoseSearchIndex::Reset()
{
	NumPoses = 0;
	Assets.Reset();
	Values.Reset();
	Schema = nullptr;
}

void FPoseSearchIndex::Normalize(TArrayView<float> InOutPoseVector) const
{
	using namespace Eigen;

	auto TransformationMtx = Map<const Matrix<float, Dynamic, Dynamic, ColMajor>>
	(
		PreprocessInfo.TransformationMatrix.GetData(),
		PreprocessInfo.NumDimensions,
		PreprocessInfo.NumDimensions
	);
	auto SampleMean = Map<const Matrix<float, Dynamic, 1, ColMajor>>
	(
		PreprocessInfo.SampleMean.GetData(),
		PreprocessInfo.NumDimensions
	);

	checkSlow(InOutPoseVector.Num() == PreprocessInfo.NumDimensions);

	auto PoseVector = Map<Matrix<float, Dynamic, 1, ColMajor>>
	(
		InOutPoseVector.GetData(),
		InOutPoseVector.Num()
	);

	PoseVector = TransformationMtx * (PoseVector - SampleMean);
}

void FPoseSearchIndex::InverseNormalize(TArrayView<float> InOutNormalizedPoseVector) const
{
	using namespace Eigen;

	auto InverseTransformationMtx = Map<const Matrix<float, Dynamic, Dynamic, ColMajor>>
	(
		PreprocessInfo.InverseTransformationMatrix.GetData(),
		PreprocessInfo.NumDimensions,
		PreprocessInfo.NumDimensions
	);
	auto SampleMean = Map<const Matrix<float, Dynamic, 1, ColMajor>>
	(
		PreprocessInfo.SampleMean.GetData(),
		PreprocessInfo.NumDimensions
	);

	checkSlow(InOutNormalizedPoseVector.Num() == PreprocessInfo.NumDimensions);

	auto NormalizedPoseVector = Map<Matrix<float, Dynamic, 1, ColMajor>>
	(
		InOutNormalizedPoseVector.GetData(),
		InOutNormalizedPoseVector.Num()
	);

	NormalizedPoseVector = (InverseTransformationMtx * NormalizedPoseVector) + SampleMean;
}


//////////////////////////////////////////////////////////////////////////
// UPoseSearchSequenceMetaData

void UPoseSearchSequenceMetaData::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	SearchIndex.Reset();

#if WITH_EDITOR
	if (!IsTemplate())
	{
		if (IsValidForIndexing())
		{
			UObject* Outer = GetOuter();
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Outer))
			{
				UE::PoseSearch::BuildIndex(Sequence, this);
			}
		}
	}
#endif

	Super::PreSave(ObjectSaveContext);
}

bool UPoseSearchSequenceMetaData::IsValidForIndexing() const
{
	return Schema && Schema->IsValid() && UE::PoseSearch::IsSamplingRangeValid(SamplingRange);
}

bool UPoseSearchSequenceMetaData::IsValidForSearch() const
{
	return IsValidForIndexing() && SearchIndex.IsValid() && !SearchIndex.IsEmpty();
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchDatabaseSequence
FFloatInterval FPoseSearchDatabaseSequence::GetEffectiveSamplingRange() const
{
	return UE::PoseSearch::GetEffectiveSamplingRange(Sequence, SamplingRange);
}

//////////////////////////////////////////////////////////////////////////
// UPoseSearchDatabase

FPoseSearchIndex* UPoseSearchDatabase::GetSearchIndex()
{
	if (PrivateDerivedData == nullptr)
	{
		return nullptr;
	}

	return &PrivateDerivedData->SearchIndex;
}

const FPoseSearchIndex* UPoseSearchDatabase::GetSearchIndex() const
{
	if (PrivateDerivedData == nullptr)
	{
		return nullptr;
	}

	return &PrivateDerivedData->SearchIndex;
}

int32 UPoseSearchDatabase::GetPoseIndexFromTime(float Time, const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	FFloatInterval Range = SearchIndexAsset->SamplingInterval;

	const bool bHasPoseIndex =
		SearchIndexAsset->FirstPoseIdx != INDEX_NONE &&
		SearchIndexAsset->NumPoses > 0 &&
		Range.Contains(Time);

	if (bHasPoseIndex)
	{
		int32 PoseOffset = FMath::RoundToInt(Schema->SampleRate * (Time - Range.Min));
		
		check(PoseOffset >= 0);

		if (PoseOffset >= SearchIndexAsset->NumPoses)
		{
			if (IsSourceAssetLooping(SearchIndexAsset))
			{
				PoseOffset -= SearchIndexAsset->NumPoses;
			}
			else
			{
				PoseOffset = SearchIndexAsset->NumPoses - 1;
			}
		}

		int32 PoseIdx = SearchIndexAsset->FirstPoseIdx + PoseOffset;
		return PoseIdx;
	}

	return INDEX_NONE;
}

float UPoseSearchDatabase::GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* Asset) const
{
	float AssetTime = GetSearchIndex()->GetAssetTime(PoseIdx, Asset);
	return AssetTime;
}

const FPoseSearchDatabaseSequence& UPoseSearchDatabase::GetSequenceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	check(SearchIndexAsset->Type == ESearchIndexAssetType::Sequence);
	return Sequences[SearchIndexAsset->SourceAssetIdx];
}

const FPoseSearchDatabaseBlendSpace& UPoseSearchDatabase::GetBlendSpaceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	check(SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace);
	return BlendSpaces[SearchIndexAsset->SourceAssetIdx];
}

const bool UPoseSearchDatabase::IsSourceAssetLooping(const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		return GetSequenceSourceAsset(SearchIndexAsset).bLoopAnimation;
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		return GetBlendSpaceSourceAsset(SearchIndexAsset).bLoopAnimation;
	}
	else
	{
		checkNoEntry();
		return false;
	}
}

const FGameplayTagContainer* UPoseSearchDatabase::GetSourceAssetGroupTags(const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		return &GetSequenceSourceAsset(SearchIndexAsset).GroupTags;
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		return &GetBlendSpaceSourceAsset(SearchIndexAsset).GroupTags;
	}
	else
	{
		checkNoEntry();
		return nullptr;
	}
}

const FString UPoseSearchDatabase::GetSourceAssetName(const FPoseSearchIndexAsset* SearchIndexAsset) const
{
	if (SearchIndexAsset->Type == ESearchIndexAssetType::Sequence)
	{
		return GetSequenceSourceAsset(SearchIndexAsset).Sequence->GetName();
	}
	else if (SearchIndexAsset->Type == ESearchIndexAssetType::BlendSpace)
	{
		return GetBlendSpaceSourceAsset(SearchIndexAsset).BlendSpace->GetName();
	}
	else
	{
		checkNoEntry();
		return FString();
	}
}

int32 UPoseSearchDatabase::GetNumberOfPrincipalComponents() const
{
	return FMath::Min<int32>(NumberOfPrincipalComponents, Schema->Layout.NumFloats);
}

bool UPoseSearchDatabase::IsValidForIndexing() const
{
	bool bValid = Schema && Schema->IsValid() && !Sequences.IsEmpty();

	if (bValid)
	{
		bool bSequencesValid = true;
		for (const FPoseSearchDatabaseSequence& DbSequence : Sequences)
		{
			if (!DbSequence.Sequence)
			{
				bSequencesValid = false;
				break;
			}

			USkeleton* SeqSkeleton = DbSequence.Sequence->GetSkeleton();
			if (!SeqSkeleton || !SeqSkeleton->IsCompatible(Schema->Skeleton))
			{
				bSequencesValid = false;
				break;
			}
		}

		bValid = bSequencesValid;
	}

	return bValid;
}

bool UPoseSearchDatabase::IsValidForSearch() const
{
	const FPoseSearchIndex* SearchIndex = GetSearchIndex();
	bool bIsValid = IsValidForIndexing() && SearchIndex && SearchIndex->IsValid() && !SearchIndex->IsEmpty();

#if WITH_EDITOR
	const bool bIsCurrentDerivedData = 
		PrivateDerivedData &&
		PrivateDerivedData->PendingDerivedDataKey == PrivateDerivedData->DerivedDataKey.Hash;
	bIsValid = bIsValid && bIsCurrentDerivedData;
#endif // WITH_EDITOR

	return bIsValid;
}

void UPoseSearchDatabase::CollectSimpleSequences()
{
	for (auto& SimpleSequence: SimpleSequences)
	{
		auto Predicate = [&SimpleSequence](FPoseSearchDatabaseSequence& DbSequence) -> bool
		{
			return DbSequence.Sequence == SimpleSequence;
		};

		if (!Sequences.ContainsByPredicate(Predicate))
		{
			FPoseSearchDatabaseSequence& DbSequence = Sequences.AddDefaulted_GetRef();
			DbSequence.Sequence = SimpleSequence;
		}
	}

	SimpleSequences.Reset();
}

void UPoseSearchDatabase::CollectSimpleBlendSpaces()
{
	for (auto& SimpleBlendSpace : SimpleBlendSpaces)
	{
		auto Predicate = [&SimpleBlendSpace](FPoseSearchDatabaseBlendSpace& DbBlendSpace) -> bool
		{
			return DbBlendSpace.BlendSpace == SimpleBlendSpace;
		};

		if (!BlendSpaces.ContainsByPredicate(Predicate))
		{
			FPoseSearchDatabaseBlendSpace& DbBlendSpace = BlendSpaces.AddDefaulted_GetRef();
			DbBlendSpace.BlendSpace = SimpleBlendSpace;
		}
	}

	SimpleBlendSpaces.Reset();
}

static void FindValidSequenceIntervals(const FPoseSearchDatabaseSequence& DbSequence, TArray<FFloatRange>& ValidRanges)
{
	const UAnimSequence* Sequence = DbSequence.Sequence;
	check(DbSequence.Sequence);

	const float SequenceLength = DbSequence.Sequence->GetPlayLength();
	const FFloatInterval EffectiveSamplingInterval = DbSequence.GetEffectiveSamplingRange();

	// start from a single interval defined by the database sequence sampling range
	ValidRanges.Empty();
	ValidRanges.Add(FFloatRange::Inclusive(EffectiveSamplingInterval.Min, EffectiveSamplingInterval.Max));

	FAnimNotifyContext NotifyContext;
	Sequence->GetAnimNotifies(0.0f, SequenceLength, NotifyContext);

	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
		if (!NotifyEvent)
		{
			continue;
		}

		const UAnimNotifyState_PoseSearchExcludeFromDatabase* ExclusionNotifyState =
			Cast<const UAnimNotifyState_PoseSearchExcludeFromDatabase>(NotifyEvent->NotifyStateClass);
		if (ExclusionNotifyState)
		{
			FFloatRange ExclusionRange = 
				FFloatRange::Inclusive(NotifyEvent->GetTriggerTime(), NotifyEvent->GetEndTriggerTime());

			// Split every valid range based on the exclusion range just found. Because this might increase the 
			// number of ranges in ValidRanges, the algorithm iterates from end to start.
			for (int RangeIdx = ValidRanges.Num() - 1; RangeIdx >= 0; --RangeIdx)
			{
				FFloatRange EvaluatedRange = ValidRanges[RangeIdx];
				ValidRanges.RemoveAt(RangeIdx);

				TArray<FFloatRange> Diff = FFloatRange::Difference(EvaluatedRange, ExclusionRange);
				ValidRanges.Append(Diff);
			}
		}
	}
}

static inline void CollectGroupIndices(
	TArrayView<const FPoseSearchDatabaseGroup> Groups,
	const FGameplayTagContainer& GroupTags,
	const int32 Index,
	TArray<int32>& GroupIndices,
	TArray<int32>& BadGroupIndices)
{
	GroupIndices.Reset();

	for (const FGameplayTag& GroupTag : GroupTags)
	{
		const int32 GroupIndex = Groups.IndexOfByPredicate([&](const FPoseSearchDatabaseGroup& DatabaseGroup)
		{
			return DatabaseGroup.Tag == GroupTag;
		});

		// we don't add INDEX_NONE because index none represents a choice to use the default group by not adding
		// any group identifiers. If an added identifier doesn't match, that's an error. In the future this
		// should be made robust enough to prevent these errors from happening
		if (GroupIndex == INDEX_NONE)
		{
			BadGroupIndices.Add(Index);
		}
		else if (Groups[GroupIndex].bUseGroupWeights)
		{
			GroupIndices.Add(GroupIndex);
		}
	}

	if (GroupIndices.Num() == 0)
	{
		GroupIndices.Add(INDEX_NONE);
	}
}

void FPoseSearchDatabaseBlendSpace::GetBlendSpaceParameterSampleRanges(
	int32& HorizontalBlendNum,
	int32& VerticalBlendNum,
	float& HorizontalBlendMin,
	float& HorizontalBlendMax,
	float& VerticalBlendMin,
	float& VerticalBlendMax) const
{
	HorizontalBlendNum = bUseGridForSampling ? BlendSpace->GetBlendParameter(0).GridNum + 1 : FMath::Max(NumberOfHorizontalSamples, 1);
	VerticalBlendNum = bUseGridForSampling ? BlendSpace->GetBlendParameter(1).GridNum + 1 : FMath::Max(NumberOfVerticalSamples, 1);

	check(HorizontalBlendNum >= 1 && VerticalBlendNum >= 1);

	HorizontalBlendMin = BlendSpace->GetBlendParameter(0).Min;
	HorizontalBlendMax = BlendSpace->GetBlendParameter(0).Max;

	VerticalBlendMin = BlendSpace->GetBlendParameter(1).Min;
	VerticalBlendMax = BlendSpace->GetBlendParameter(1).Max;

	if (BlendSpace->IsA<UBlendSpace1D>())
	{
		VerticalBlendNum = 1;
		VerticalBlendMin = 0.0;
		VerticalBlendMax = 0.0;
	}
}

static FVector BlendParameterForSampleRanges(
	int32 HorizontalBlendIndex,
	int32 VerticalBlendIndex,
	int32 HorizontalBlendNum,
	int32 VerticalBlendNum,
	float HorizontalBlendMin,
	float HorizontalBlendMax,
	float VerticalBlendMin,
	float VerticalBlendMax)
{
	return FVector(
		HorizontalBlendNum > 1 ? 
			HorizontalBlendMin + (HorizontalBlendMax - HorizontalBlendMin) * 
			((float)HorizontalBlendIndex) / (HorizontalBlendNum - 1) : 
		HorizontalBlendMin,
		VerticalBlendNum > 1 ? 
			VerticalBlendMin + (VerticalBlendMax - VerticalBlendMin) * 
			((float)VerticalBlendIndex) / (VerticalBlendNum - 1) : 
		VerticalBlendMin,
		0.0f);
}

bool UPoseSearchDatabase::TryInitSearchIndexAssets(FPoseSearchIndex& OutSearchIndex)
{
	OutSearchIndex.Assets.Empty();
	
	bool bAnyMirrored = false;
	
	TArray<FFloatRange> ValidRanges;
	TArray<int32> GroupIndices;
	TArray<int32> BadSequenceGroupIndices;
	TArray<int32> BadBlendSpaceGroupIndices;

	for (int32 SequenceIdx = 0; SequenceIdx < Sequences.Num(); ++SequenceIdx)
	{
		const FPoseSearchDatabaseSequence& Sequence = Sequences[SequenceIdx];
		bool bAddUnmirrored = 
			Sequence.MirrorOption == EPoseSearchMirrorOption::UnmirroredOnly ||
			Sequence.MirrorOption == EPoseSearchMirrorOption::UnmirroredAndMirrored;
		bool bAddMirrored =
			Sequence.MirrorOption == EPoseSearchMirrorOption::MirroredOnly ||
			Sequence.MirrorOption == EPoseSearchMirrorOption::UnmirroredAndMirrored;

		CollectGroupIndices(
			Groups,
			Sequence.GroupTags,
			SequenceIdx,
			GroupIndices,
			BadSequenceGroupIndices);

		for (int32 GroupIndex : GroupIndices)
		{
			ValidRanges.Reset();
			FindValidSequenceIntervals(Sequence, ValidRanges);
			for (const FFloatRange& Range : ValidRanges)
			{
				if (bAddUnmirrored)
				{
					OutSearchIndex.Assets.Add(
						FPoseSearchIndexAsset(
							ESearchIndexAssetType::Sequence,
							GroupIndex,
							SequenceIdx,
							false,
							FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
				}

				if (bAddMirrored)
				{
					OutSearchIndex.Assets.Add(
						FPoseSearchIndexAsset(
							ESearchIndexAssetType::Sequence,
							GroupIndex,
							SequenceIdx,
							true,
							FFloatInterval(Range.GetLowerBoundValue(), Range.GetUpperBoundValue())));
					bAnyMirrored = true;
				}
			}
		}
	}

	TArray<FBlendSampleData> BlendSamples;

	for (int32 BlendSpaceIdx = 0; BlendSpaceIdx < BlendSpaces.Num(); ++BlendSpaceIdx)
	{
		const FPoseSearchDatabaseBlendSpace& BlendSpace = BlendSpaces[BlendSpaceIdx];
		bool bAddUnmirrored =
			BlendSpace.MirrorOption == EPoseSearchMirrorOption::UnmirroredOnly ||
			BlendSpace.MirrorOption == EPoseSearchMirrorOption::UnmirroredAndMirrored;
		bool bAddMirrored =
			BlendSpace.MirrorOption == EPoseSearchMirrorOption::MirroredOnly ||
			BlendSpace.MirrorOption == EPoseSearchMirrorOption::UnmirroredAndMirrored;

		CollectGroupIndices(
			Groups,
			BlendSpace.GroupTags,
			BlendSpaceIdx,
			GroupIndices,
			BadBlendSpaceGroupIndices);

		for (int32 GroupIndex : GroupIndices)
		{
			int32 HorizontalBlendNum, VerticalBlendNum;
			float HorizontalBlendMin, HorizontalBlendMax, VerticalBlendMin, VerticalBlendMax;

			BlendSpace.GetBlendSpaceParameterSampleRanges(
				HorizontalBlendNum,
				VerticalBlendNum,
				HorizontalBlendMin,
				HorizontalBlendMax,
				VerticalBlendMin,
				VerticalBlendMax);

			for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalBlendNum; HorizontalIndex++)
			{
				for (int32 VerticalIndex = 0; VerticalIndex < VerticalBlendNum; VerticalIndex++)
				{
					FVector BlendParameters = BlendParameterForSampleRanges(
						HorizontalIndex,
						VerticalIndex,
						HorizontalBlendNum,
						VerticalBlendNum,
						HorizontalBlendMin,
						HorizontalBlendMax,
						VerticalBlendMin,
						VerticalBlendMax);
						
					int32 TriangulationIndex = 0;
					BlendSpace.BlendSpace->GetSamplesFromBlendInput(BlendParameters, BlendSamples, TriangulationIndex, true);

					float PlayLength = BlendSpace.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

					if (bAddUnmirrored)
					{
						OutSearchIndex.Assets.Add(
							FPoseSearchIndexAsset(
								ESearchIndexAssetType::BlendSpace,
								GroupIndex,
								BlendSpaceIdx,
								false,
								FFloatInterval(0.0f, PlayLength),
								BlendParameters));
					}

					if (bAddMirrored)
					{
						OutSearchIndex.Assets.Add(
							FPoseSearchIndexAsset(
								ESearchIndexAssetType::BlendSpace,
								GroupIndex,
								BlendSpaceIdx,
								true,
								FFloatInterval(0.0f, PlayLength),
								BlendParameters));
						bAnyMirrored = true;
					}
				}
			}
		}
	}

	// @todo: change the above for loops fill OutSearchIndex.Assets already in ascnding group order
	// sorting by ascending SourceGroupIdx
	OutSearchIndex.Assets.Sort([](const FPoseSearchIndexAsset& InOne, const FPoseSearchIndexAsset& InTwo)
	{
		return InOne.SourceGroupIdx < InTwo.SourceGroupIdx;
	});

	if (bAnyMirrored && !Schema->MirrorDataTable)
	{
		UE_LOG(
			LogPoseSearch, 
			Error, 
			TEXT("Database %s is asking for mirrored sequences but MirrorDataBase is null in %s"),
			*GetNameSafe(this), 
			*GetNameSafe(Schema));
		OutSearchIndex.Assets.Empty();
		return false;
	}

	for (int32 BadGroupSequenceIdx : BadSequenceGroupIndices)
	{
		UE_LOG(
			LogPoseSearch,
			Warning,
			TEXT("Database %s, sequence %s is asking for a group that doesn't exist"),
			*GetNameSafe(this),
			*GetNameSafe(Sequences[BadGroupSequenceIdx].Sequence));
	}

	for (int32 BadGroupBlendSpaceIdx : BadBlendSpaceGroupIndices)
	{
		UE_LOG(
			LogPoseSearch,
			Warning,
			TEXT("Database %s, blendspace %s is asking for a group that doesn't exist"),
			*GetNameSafe(this),
			*GetNameSafe(BlendSpaces[BadGroupBlendSpaceIdx].BlendSpace));
	}

	return true;
}

void UPoseSearchDatabase::PostLoad()
{
#if WITH_EDITOR
	if (!PrivateDerivedData)
	{
		BeginCacheDerivedData();
	}
#endif

	Super::PostLoad();
}

#if WITH_EDITOR
void UPoseSearchDatabase::RegisterOnDerivedDataRebuild(const FOnDerivedDataRebuild& Delegate)
{
	OnDerivedDataRebuild.Add(Delegate);
}
void UPoseSearchDatabase::UnregisterOnDerivedDataRebuild(void* Unregister)
{
	OnDerivedDataRebuild.RemoveAll(Unregister);
}

void UPoseSearchDatabase::NotifyDerivedDataBuildStarted()
{
	OnDerivedDataRebuild.Broadcast();
}

void UPoseSearchDatabase::RegisterOnAssetChange(const FOnAssetChange& Delegate)
{
	OnAssetChange.Add(Delegate);
}

void UPoseSearchDatabase::UnregisterOnAssetChange(void* Unregister)
{
	OnAssetChange.RemoveAll(Unregister);
}

void UPoseSearchDatabase::NotifyAssetChange()
{
	OnAssetChange.Broadcast();
}

void UPoseSearchDatabase::RegisterOnGroupChange(const FOnGroupChange& Delegate)
{
	OnGroupChange.Add(Delegate);
}

void UPoseSearchDatabase::UnregisterOnGroupChange(void* Unregister)
{
	OnGroupChange.RemoveAll(Unregister);
}

void UPoseSearchDatabase::NotifyGroupChange()
{
	OnGroupChange.Broadcast();
}

void UPoseSearchDatabase::BeginCacheDerivedData()
{
	bool bPerformCache = true;
			
	using namespace UE::DerivedData;
	if (PrivateDerivedData)
	{
		const FIoHash ExistingDerivedDataHash = PrivateDerivedData->PendingDerivedDataKey;
		if (!ExistingDerivedDataHash.IsZero())
		{
			const FIoHash CurrentHash = UE::PoseSearch::FPoseSearchDatabaseAsyncCacheTask::CreateKey(*this);
			if (ExistingDerivedDataHash == CurrentHash)
			{
				bPerformCache = false;
			}
		}
	}

	if (bPerformCache)
	{
		if (!PrivateDerivedData)
		{
			PrivateDerivedData = new FPoseSearchDatabaseDerivedData();
		}

		PrivateDerivedData->Cache(*this, false);
	}
}

FIoHash UPoseSearchDatabase::GetSearchIndexHash() const
{
	if (!PrivateDerivedData)
	{
		return FIoHash::Zero;
	}

	return PrivateDerivedData->DerivedDataKey.Hash;
}

bool UPoseSearchDatabase::IsDerivedDataBuildPending() const
{
	if (!PrivateDerivedData)
	{
		return true;
	}

	return PrivateDerivedData->DerivedDataKey.Hash != PrivateDerivedData->PendingDerivedDataKey;
}
#endif // WITH_EDITOR


void UPoseSearchDatabase::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);

#if WITH_EDITOR
	if (!IsTemplate() && !ObjectSaveContext.IsProceduralSave())
	{
		if (IsValidForIndexing())
		{
			if (!PrivateDerivedData)
			{
				PrivateDerivedData = new FPoseSearchDatabaseDerivedData();
			}

			PrivateDerivedData->Cache(*this, true);
		}
	}
#endif
}

void UPoseSearchDatabase::Serialize(FArchive& Ar)
{
	using namespace UE::PoseSearch;

 	Super::Serialize(Ar);

	if (Ar.IsFilterEditorOnly())
	{
		if (Ar.IsLoading())
		{
			if (!PrivateDerivedData)
			{
				PrivateDerivedData = new FPoseSearchDatabaseDerivedData();
				PrivateDerivedData->SearchIndex.Schema = Schema;
			}
		}
		check(Ar.IsLoading() || (Ar.IsCooking() && IsDerivedDataValid()));
		FPoseSearchIndex* SearchIndex = GetSearchIndex();
		Ar << *SearchIndex;
	}
}

bool UPoseSearchDatabase::IsDerivedDataValid()
{
	const FPoseSearchIndex* SearchIndex = GetSearchIndex();
	bool bIsValid = SearchIndex && SearchIndex->IsValid();
	return bIsValid;
}

#if WITH_EDITOR
void UPoseSearchDatabase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bAssetChange = false;

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, SimpleSequences))
	{
		if (!SimpleSequences.IsEmpty())
		{
			CollectSimpleSequences();
			bAssetChange = true;
		}
	}

	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, SimpleBlendSpaces))
	{
		if (!SimpleBlendSpaces.IsEmpty())
		{
			CollectSimpleBlendSpaces();
			bAssetChange = true;
		}
	}

	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, Sequences) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, BlendSpaces))
	{
		bAssetChange = true;
	}

	if (bAssetChange)
	{
		NotifyAssetChange();
	}

	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPoseSearchDatabase, Groups))
	{
		NotifyGroupChange();
	}

	BeginCacheDerivedData();
}

void UPoseSearchDatabase::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Super::BeginCacheForCookedPlatformData(TargetPlatform);

	BeginCacheDerivedData();
}

bool UPoseSearchDatabase::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	if (!PrivateDerivedData)
	{
		PrivateDerivedData = new FPoseSearchDatabaseDerivedData();
		PrivateDerivedData->Cache(*this, true);
		return false;
	}

	if (PrivateDerivedData->AsyncTask && PrivateDerivedData->AsyncTask->Poll())
	{
		PrivateDerivedData->FinishCache();
	}

	if (PrivateDerivedData->AsyncTask)
	{
		return false;
	}

	return true;
}

#endif // WITH_EDITOR


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorBuilder

void FPoseSearchFeatureVectorBuilder::Init(const UPoseSearchSchema* InSchema)
{
	check(InSchema && InSchema->IsValid());
	Schema = InSchema;
	ResetFeatures();
}

void FPoseSearchFeatureVectorBuilder::Reset()
{
	Schema = nullptr;
	Values.Reset(0);
	ValuesNormalized.Reset(0);
	NumFeaturesAdded = 0;
	FeaturesAdded.Reset();
}

void FPoseSearchFeatureVectorBuilder::ResetFeatures()
{
	Values.Reset(0);
	Values.SetNumZeroed(Schema->Layout.NumFloats);
	ValuesNormalized.Reset(0);
	ValuesNormalized.SetNumZeroed(Schema->Layout.NumFloats);
	NumFeaturesAdded = 0;
	FeaturesAdded.Init(false, Schema->Layout.Features.Num());
}

void FPoseSearchFeatureVectorBuilder::SetTransform(FPoseSearchFeatureDesc Feature, const FTransform& Transform)
{
	SetPosition(Feature, Transform.GetTranslation());
	SetRotation(Feature, Transform.GetRotation());
}

void FPoseSearchFeatureVectorBuilder::SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	SetLinearVelocity(Feature, Transform, PrevTransform, DeltaTime);
	SetAngularVelocity(Feature, Transform, PrevTransform, DeltaTime);
}

void FPoseSearchFeatureVectorBuilder::SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	SetLinearVelocity(Feature, NextTransform, Transform, PrevTransform, DeltaTime);
	SetAngularVelocity(Feature, NextTransform, Transform, PrevTransform, DeltaTime);
}

void FPoseSearchFeatureVectorBuilder::SetPosition(FPoseSearchFeatureDesc Feature, const FVector& Position)
{
	Feature.Type = EPoseSearchFeatureType::Position;
	SetVector(Feature, Position);
}

void FPoseSearchFeatureVectorBuilder::SetRotation(FPoseSearchFeatureDesc Feature, const FQuat& Rotation)
{
	Feature.Type = EPoseSearchFeatureType::Rotation;
	int32 ElementIndex = Schema->Layout.Features.Find(Feature);
	if (ElementIndex >= 0)
	{
		FVector X = Rotation.GetAxisX();
		FVector Y = Rotation.GetAxisY();

		const FPoseSearchFeatureDesc& FoundElement = Schema->Layout.Features[ElementIndex];

		Values[FoundElement.ValueOffset + 0] = X.X;
		Values[FoundElement.ValueOffset + 1] = X.Y;
		Values[FoundElement.ValueOffset + 2] = X.Z;
		Values[FoundElement.ValueOffset + 3] = Y.X;
		Values[FoundElement.ValueOffset + 4] = Y.Y;
		Values[FoundElement.ValueOffset + 5] = Y.Z;

		if (!FeaturesAdded[ElementIndex])
		{
			FeaturesAdded[ElementIndex] = true;
			++NumFeaturesAdded;
		}
	}

	Feature.Type = EPoseSearchFeatureType::ForwardVector;
	SetVector(Feature, Rotation.GetAxisY());
}

void FPoseSearchFeatureVectorBuilder::SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Feature.Type = EPoseSearchFeatureType::LinearVelocity;
	FVector LinearVelocity = (Transform.GetTranslation() - PrevTransform.GetTranslation()) / DeltaTime;
	SetVector(Feature, LinearVelocity);
}

void FPoseSearchFeatureVectorBuilder::SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Feature.Type = EPoseSearchFeatureType::LinearVelocity;
	FVector LinearVelocityNext = (NextTransform.GetTranslation() - Transform.GetTranslation()) / DeltaTime;
	FVector LinearVelocityPrev = (Transform.GetTranslation() - PrevTransform.GetTranslation()) / DeltaTime;
	SetVector(Feature, (LinearVelocityNext + LinearVelocityPrev) / 2.0f);
}

static inline FVector QuaternionAngularVelocity(const FQuat& Rotation, const FQuat& PrevRotation, float DeltaTime)
{
	FQuat Q0 = PrevRotation;
	FQuat Q1 = Rotation;
	Q1.EnforceShortestArcWith(Q0);

	// Given angular velocity vector w, quaternion differentiation can be represented as
	//   dq/dt = (w * q)/2
	// Solve for w
	//   w = 2 * dq/dt * q^-1
	// And let dq/dt be expressed as the finite difference
	//   dq/dt = (q(t+h) - q(t)) / h
	FQuat DQDt = (Q1 - Q0) / DeltaTime;
	FQuat QInv = Q0.Inverse();
	FQuat W = (DQDt * QInv) * 2.0f;

	FVector AngularVelocity(W.X, W.Y, W.Z);

	return AngularVelocity;
}

void FPoseSearchFeatureVectorBuilder::SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Feature.Type = EPoseSearchFeatureType::AngularVelocity;
	FVector AngularVelocity = QuaternionAngularVelocity(Transform.GetRotation(), PrevTransform.GetRotation(), DeltaTime);
	SetVector(Feature, AngularVelocity);
}

void FPoseSearchFeatureVectorBuilder::SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime)
{
	Feature.Type = EPoseSearchFeatureType::AngularVelocity;
	FVector AngularVelocityNext = QuaternionAngularVelocity(NextTransform.GetRotation(), Transform.GetRotation(), DeltaTime);
	FVector AngularVelocityPrev = QuaternionAngularVelocity(Transform.GetRotation(), PrevTransform.GetRotation(), DeltaTime);
	SetVector(Feature, (AngularVelocityNext + AngularVelocityPrev) / 2.0f);
}

void FPoseSearchFeatureVectorBuilder::SetVector(FPoseSearchFeatureDesc Feature, const FVector& Vector)
{
	int32 ElementIndex = Schema->Layout.Features.Find(Feature);
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Schema->Layout.Features[ElementIndex];

		Values[FoundElement.ValueOffset + 0] = Vector[0];
		Values[FoundElement.ValueOffset + 1] = Vector[1];
		Values[FoundElement.ValueOffset + 2] = Vector[2];

		if (!FeaturesAdded[ElementIndex])
		{
			FeaturesAdded[ElementIndex] = true;
			++NumFeaturesAdded;
		}
	}
}

void FPoseSearchFeatureVectorBuilder::CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx)
{
	check(Schema == SearchIndex.Schema);

	TArrayView<const float> FeatureVector = SearchIndex.GetPoseValues(PoseIdx);

	ValuesNormalized = FeatureVector;
	Values = FeatureVector;
	SearchIndex.InverseNormalize(Values);

	NumFeaturesAdded = Schema->Layout.Features.Num();
	FeaturesAdded.SetRange(0, FeaturesAdded.Num(), true);
}

void FPoseSearchFeatureVectorBuilder::CopyFeature(const FPoseSearchFeatureVectorBuilder& OtherBuilder, int32 FeatureIdx)
{
	check(IsCompatible(OtherBuilder));
	check(OtherBuilder.FeaturesAdded[FeatureIdx]);

	const FPoseSearchFeatureDesc& FeatureDesc = Schema->Layout.Features[FeatureIdx];
	const int32 FeatureNumFloats = UE::PoseSearch::GetFeatureTypeTraits(FeatureDesc.Type).NumFloats;
	const int32 FeatureValueOffset = FeatureDesc.ValueOffset;

	for(int32 FeatureValueIdx = FeatureValueOffset; FeatureValueIdx != FeatureValueOffset + FeatureNumFloats; ++FeatureValueIdx)
	{
		Values[FeatureValueIdx] = OtherBuilder.Values[FeatureValueIdx];
	}

	if (!FeaturesAdded[FeatureIdx])
	{
		FeaturesAdded[FeatureIdx] = true;
		++NumFeaturesAdded;
	}
}

void FPoseSearchFeatureVectorBuilder::MergeReplace(const FPoseSearchFeatureVectorBuilder& OtherBuilder)
{
	check(IsCompatible(OtherBuilder));

	for (TConstSetBitIterator<> Iter(OtherBuilder.FeaturesAdded); Iter; ++Iter)
	{
		CopyFeature(OtherBuilder, Iter.GetIndex());
	}
}

bool FPoseSearchFeatureVectorBuilder::IsInitialized() const
{
	return (Schema != nullptr) && (Values.Num() == Schema->Layout.NumFloats);
}

bool FPoseSearchFeatureVectorBuilder::IsInitializedForSchema(const UPoseSearchSchema* InSchema) const
{
	return (Schema == InSchema) && IsInitialized();
}

bool FPoseSearchFeatureVectorBuilder::IsComplete() const
{
	return NumFeaturesAdded == Schema->Layout.Features.Num();
}

bool FPoseSearchFeatureVectorBuilder::IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const
{
	return IsInitialized() && (Schema == OtherBuilder.Schema);
}

const TBitArray<>& FPoseSearchFeatureVectorBuilder::GetFeaturesAdded() const
{
	return FeaturesAdded;
}

void FPoseSearchFeatureVectorBuilder::Normalize(const FPoseSearchIndex& ForSearchIndex)
{
	ValuesNormalized = Values;
	ForSearchIndex.Normalize(ValuesNormalized);
}

namespace UE { namespace PoseSearch {

//////////////////////////////////////////////////////////////////////////
// FPoseHistory

/**
* Fills skeleton transforms with evaluated compact pose transforms.
* Bones that weren't evaluated are filled with the bone's reference pose.
*/
static void CopyCompactToSkeletonPose(const FCompactPose& Pose, TArray<FTransform>& OutLocalTransforms)
{
	const FBoneContainer& BoneContainer = Pose.GetBoneContainer();
	const USkeleton* SkeletonAsset = BoneContainer.GetSkeletonAsset();
	check(SkeletonAsset);

	const FReferenceSkeleton& RefSkeleton = SkeletonAsset->GetReferenceSkeleton();
	TArrayView<const FTransform> RefSkeletonTransforms = MakeArrayView(RefSkeleton.GetRefBonePose());
	const int32 NumSkeletonBones = RefSkeleton.GetNum();

	OutLocalTransforms.SetNum(NumSkeletonBones);

	for (auto SkeletonBoneIdx = FSkeletonPoseBoneIndex(0); SkeletonBoneIdx != NumSkeletonBones; ++SkeletonBoneIdx)
	{
		FCompactPoseBoneIndex CompactBoneIdx = BoneContainer.GetCompactPoseIndexFromSkeletonPoseIndex(SkeletonBoneIdx);
		OutLocalTransforms[SkeletonBoneIdx.GetInt()] = CompactBoneIdx.IsValid() ? Pose[CompactBoneIdx] : RefSkeletonTransforms[SkeletonBoneIdx.GetInt()];
	}
}

void FPoseHistory::Init(int32 InNumPoses, float InTimeHorizon)
{
	Poses.Reserve(InNumPoses);
	Knots.Reserve(InNumPoses);
	TimeHorizon = InTimeHorizon;
}

void FPoseHistory::Init(const FPoseHistory& History)
{
	Poses = History.Poses;
	Knots = History.Knots;
	TimeHorizon = History.TimeHorizon;
}

bool FPoseHistory::TrySampleLocalPose(float SecondsAgo, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose, FTransform& RootTransform)
{
	int32 NextIdx = LowerBound(Knots.begin(), Knots.end(), SecondsAgo, TGreater<>());
	if (NextIdx <= 0 || NextIdx >= Knots.Num())
	{
		return false;
	}

	int32 PrevIdx = NextIdx - 1;

	const FPose& PrevPose = Poses[PrevIdx];
	const FPose& NextPose = Poses[NextIdx];

	// Compute alpha between previous and next knots
	float Alpha = FMath::GetMappedRangeValueUnclamped(
		FVector2f(Knots[PrevIdx], Knots[NextIdx]),
		FVector2f(0.0f, 1.0f),
		SecondsAgo);

	// We may not have accumulated enough poses yet
	if (PrevPose.LocalTransforms.Num() != NextPose.LocalTransforms.Num())
	{
		return false;
	}

	if (RequiredBones.Num() > PrevPose.LocalTransforms.Num())
	{
		return false;
	}

	// Lerp between poses by alpha to produce output local pose at requested sample time
	LocalPose = PrevPose.LocalTransforms;
	FAnimationRuntime::LerpBoneTransforms(
		LocalPose,
		NextPose.LocalTransforms,
		Alpha,
		RequiredBones);

	RootTransform.Blend(PrevPose.RootTransform, NextPose.RootTransform, Alpha);

	return true;
}

bool FPoseHistory::TrySamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones)
{
	// Compute local space pose at requested time
	bool bSampled = TrySampleLocalPose(SecondsAgo, RequiredBones, SampledLocalPose, SampledRootTransform);

	// Compute local space pose one sample interval in the past
	bSampled = bSampled && TrySampleLocalPose(SecondsAgo + GetSampleTimeInterval(), RequiredBones, SampledPrevLocalPose, SampledPrevRootTransform);

	// Convert local to component space
	if (bSampled)
	{
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledLocalPose, SampledComponentPose);
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkeleton, SampledPrevLocalPose, SampledPrevComponentPose);
	}

	return bSampled;
}

bool FPoseHistory::Update(float SecondsElapsed, const FPoseContext& PoseContext, FTransform ComponentTransform, FText* OutError, ERootUpdateMode UpdateMode)
{
	// Age our elapsed times
	for (float& Knot : Knots)
	{
		Knot += SecondsElapsed;
	}

	if (Knots.Num() != Knots.Max())
	{
		// Consume every pose until the queue is full
		Knots.AddUninitialized();
		Poses.Emplace();
	}
	else
	{
		// Exercise pose retention policy. We must guarantee there is always one additional knot
		// beyond the time horizon so we can compute derivatives at the time horizon. We also
		// want to evenly distribute knots across the entire history buffer so we only push additional
		// poses when enough time has elapsed.

		const float SampleInterval = GetSampleTimeInterval();

		bool bCanEvictOldest = Knots[1] >= TimeHorizon + SampleInterval;
		bool bShouldPushNewest = Knots[Knots.Num() - 2] >= SampleInterval;

		if (bCanEvictOldest && bShouldPushNewest)
		{
			FPose PoseTemp = MoveTemp(Poses.First());
			Poses.PopFront();
			Poses.Emplace(MoveTemp(PoseTemp));

			Knots.PopFront();
			Knots.AddUninitialized();
		}
	}

	// Regardless of the retention policy, we always update the most recent pose
	Knots.Last() = 0.0f;
	FPose& CurrentPose = Poses.Last();
	CopyCompactToSkeletonPose(PoseContext.Pose, CurrentPose.LocalTransforms);

	// Initialize with Previous Root Transform or Identity
	CurrentPose.RootTransform = Poses.Num() > 1 ? Poses[Poses.Num() - 2].RootTransform : FTransform::Identity;
	
	// Update using either AniumRootMotionProvider or Component Transform
	if (UpdateMode == ERootUpdateMode::RootMotionDelta)
	{
		const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

		if (RootMotionProvider)
		{
			if (RootMotionProvider->HasRootMotion(PoseContext.CustomAttributes))
			{
				FTransform RootMotionDelta = FTransform::Identity;
				RootMotionProvider->ExtractRootMotion(PoseContext.CustomAttributes, RootMotionDelta);

				CurrentPose.RootTransform = RootMotionDelta * CurrentPose.RootTransform;
			}
#if WITH_EDITORONLY_DATA	
			else
			{
				if (OutError)
				{
					*OutError = LOCTEXT("PoseHistoryRootMotionProviderError",
						"Input to Pose History has no Root Motion Attribute. Try disabling 'Use Root Motion'.");
				}
				return false;
			}
#endif
		}
#if WITH_EDITORONLY_DATA	
		else
		{
			if (OutError)
			{
				*OutError = LOCTEXT("PoseHistoryRootMotionAttributeError",
					"Could not get Root Motion Provider. Try disabling 'Use Root Motion'.");
			}
			return false;
		}
#endif
	}
	else if (UpdateMode == ERootUpdateMode::ComponentTransformDelta)
	{
		CurrentPose.RootTransform = ComponentTransform;
	}
	else
	{
		checkNoEntry();
	}

	return true;
}

float FPoseHistory::GetSampleTimeInterval() const
{
	// Reserve one knot for computing derivatives at the time horizon
	return TimeHorizon / (Knots.Max() - 1);
}


//////////////////////////////////////////////////////////////////////////
// FPoseSearchFeatureVectorReader

void FFeatureVectorReader::Init(const FPoseSearchFeatureVectorLayout* InLayout)
{
	check(InLayout);
	Layout = InLayout;
}

void FFeatureVectorReader::SetValues(TArrayView<const float> InValues)
{
	check(Layout);
	check(Layout->NumFloats == InValues.Num());
	Values = InValues;
}

bool FFeatureVectorReader::IsValid() const
{
	return Layout && (Layout->NumFloats == Values.Num());
}

bool FFeatureVectorReader::GetTransform(FPoseSearchFeatureDesc Element, FTransform* OutTransform) const
{
	FVector Position;
	bool bResult = GetPosition(Element, &Position);

	FQuat Rotation;
	bResult |= GetRotation(Element, &Rotation);

	OutTransform->SetComponents(Rotation, Position, FVector::OneVector);
	return bResult;
}

bool FFeatureVectorReader::GetPosition(FPoseSearchFeatureDesc Element, FVector* OutPosition) const
{
	Element.Type = EPoseSearchFeatureType::Position;
	return GetVector(Element, OutPosition);
}

bool FFeatureVectorReader::GetRotation(FPoseSearchFeatureDesc Element, FQuat* OutRotation) const
{
	Element.Type = EPoseSearchFeatureType::Rotation;
	int32 ElementIndex = IsValid() ? Layout->Features.Find(Element) : -1;
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

		FVector X;
		FVector Y;

		X.X = Values[FoundElement.ValueOffset + 0];
		X.Y = Values[FoundElement.ValueOffset + 1];
		X.Z = Values[FoundElement.ValueOffset + 2];
		Y.X = Values[FoundElement.ValueOffset + 3];
		Y.Y = Values[FoundElement.ValueOffset + 4];
		Y.Z = Values[FoundElement.ValueOffset + 5];

		FVector Z = FVector::CrossProduct(X, Y);

		FMatrix M(FMatrix::Identity);
		M.SetColumn(0, X);
		M.SetColumn(1, Y);
		M.SetColumn(2, Z);

		*OutRotation = FQuat(M);
		return true;
	}

	*OutRotation = FQuat::Identity;
	return false;
}

bool FFeatureVectorReader::GetForwardVector(FPoseSearchFeatureDesc Element, FVector* OutForwardVector) const
{
	Element.Type = EPoseSearchFeatureType::ForwardVector;
	return GetVector(Element, OutForwardVector);
}

bool FFeatureVectorReader::GetLinearVelocity(FPoseSearchFeatureDesc Element, FVector* OutLinearVelocity) const
{
	Element.Type = EPoseSearchFeatureType::LinearVelocity;
	return GetVector(Element, OutLinearVelocity);
}

bool FFeatureVectorReader::GetAngularVelocity(FPoseSearchFeatureDesc Element, FVector* OutAngularVelocity) const
{
	Element.Type = EPoseSearchFeatureType::AngularVelocity;
	return GetVector(Element, OutAngularVelocity);
}

bool FFeatureVectorReader::GetVector(FPoseSearchFeatureDesc Element, FVector* OutVector) const
{
	int32 ElementIndex = IsValid() ? Layout->Features.Find(Element) : -1;
	if (ElementIndex >= 0)
	{
		const FPoseSearchFeatureDesc& FoundElement = Layout->Features[ElementIndex];

		FVector V;
		V.X = Values[FoundElement.ValueOffset + 0];
		V.Y = Values[FoundElement.ValueOffset + 1];
		V.Z = Values[FoundElement.ValueOffset + 2];
		*OutVector = V;
		return true;
	}

	*OutVector = FVector::ZeroVector;
	return false;
}


//////////////////////////////////////////////////////////////////////////
// FDebugDrawParams

bool FDebugDrawParams::CanDraw() const
{
	if (!World)
	{
		return false;
	}

	const FPoseSearchIndex* SearchIndex = GetSearchIndex();
	if (!SearchIndex)
	{
		return false;
	}

	return SearchIndex->IsValid() && !SearchIndex->IsEmpty();
}

const FPoseSearchIndex* FDebugDrawParams::GetSearchIndex() const
{
	if (Database)
	{
		return Database->GetSearchIndex();
	}

	if (SequenceMetaData)
	{
		return &SequenceMetaData->SearchIndex;
	}

	return nullptr;
}

const UPoseSearchSchema* FDebugDrawParams::GetSchema() const
{
	if (Database)
	{
		return Database->Schema;
	}

	if (SequenceMetaData)
	{
		return SequenceMetaData->Schema;
	}

	return nullptr;
}


//////////////////////////////////////////////////////////////////////////
// FSearchContext

void FSearchContext::SetSource(const UPoseSearchDatabase* InSourceDatabase)
{
	SearchIndex = nullptr;
	DebugDrawParams.Database = nullptr;
	DebugDrawParams.SequenceMetaData = nullptr;

	SourceDatabase = InSourceDatabase;
	if (SourceDatabase)
	{
		if (ensure(SourceDatabase->IsValidForSearch()))
		{
			SearchIndex = SourceDatabase->GetSearchIndex();
			DebugDrawParams.Database = SourceDatabase;
			MirrorMismatchCost = SourceDatabase->MirroringMismatchCost;
		}
	}
}

void FSearchContext::SetSource(const UAnimSequenceBase* InSourceSequence)
{
	SearchIndex = nullptr;
	DebugDrawParams.Database = nullptr;
	DebugDrawParams.SequenceMetaData = nullptr;

	SourceSequence = InSourceSequence;
	const UPoseSearchSequenceMetaData* MetaData =
		SourceSequence->FindMetaDataByClass<UPoseSearchSequenceMetaData>();
	if (MetaData && MetaData->IsValidForSearch())
	{
		SearchIndex = &MetaData->SearchIndex;
		DebugDrawParams.SequenceMetaData = MetaData;
	}
}

const FPoseSearchIndex* FSearchContext::GetSearchIndex() const
{
	return SearchIndex;
}

float FSearchContext::GetMirrorMismatchCost() const
{
	return MirrorMismatchCost;
}


//////////////////////////////////////////////////////////////////////////
// FAssetSamplerContext

static void FillCompactPoseAndComponentRefRotations(FAssetSamplingContext& Context)
{
	if (!Context.MirrorDataTable.IsNull())
	{
		const UMirrorDataTable* MirrorDataTablePtr = Context.MirrorDataTable.Get();
		MirrorDataTablePtr->FillCompactPoseAndComponentRefRotations(
			Context.BoneContainer,
			Context.CompactPoseMirrorBones,
			Context.ComponentSpaceRefRotations);
	}
	else
	{
		Context.CompactPoseMirrorBones.Reset();
		Context.ComponentSpaceRefRotations.Reset();
	}
}

void FAssetSamplingContext::Init(const UPoseSearchSchema* Schema)
{
	MirrorDataTable = Schema->MirrorDataTable;
	BoneContainer.InitializeTo(Schema->BoneIndicesWithParents, FCurveEvaluationOption(false), *Schema->Skeleton);
	FillCompactPoseAndComponentRefRotations(*this);
}

FTransform FAssetSamplingContext::MirrorTransform(const FTransform& InTransform) const
{
	const EAxis::Type MirrorAxis = MirrorDataTable->MirrorAxis;
	FVector T = InTransform.GetTranslation();
	T = FAnimationRuntime::MirrorVector(T, MirrorAxis);
	const FQuat ReferenceRotation = ComponentSpaceRefRotations[FCompactPoseBoneIndex(0)];
	FQuat Q = InTransform.GetRotation();
	Q = FAnimationRuntime::MirrorQuat(Q, MirrorAxis);
	Q *= FAnimationRuntime::MirrorQuat(ReferenceRotation, MirrorAxis).Inverse() * ReferenceRotation;
	FTransform Result = FTransform(Q, T, InTransform.GetScale3D());
	return Result;
}


//////////////////////////////////////////////////////////////////////////
// Root motion extrapolation

// Uses distance delta between NextRootDistanceIndex and NextRootDistanceIndex - 1 and extrapolates it to ExtrapolationTime
static float ExtrapolateAccumulatedRootDistance(
	int32 SamplingRate,
	TArrayView<const float> AccumulatedRootDistance,
	int32 NextRootDistanceIndex, 
	float ExtrapolationTime,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	check(NextRootDistanceIndex > 0 && NextRootDistanceIndex < AccumulatedRootDistance.Num());

	const float DistanceDelta =
		AccumulatedRootDistance[NextRootDistanceIndex] -
		AccumulatedRootDistance[NextRootDistanceIndex - 1];
	const float Speed = DistanceDelta * SamplingRate;
	const float ExtrapolationSpeed = Speed >= ExtrapolationParameters.LinearSpeedThreshold ?
		Speed : 0.0f;
	const float ExtrapolatedDistance = ExtrapolationSpeed * ExtrapolationTime;

	return ExtrapolatedDistance;
}

static float ExtractAccumulatedRootDistance(
	int32 SamplingRate,
	TArrayView<const float> AccumulatedRootDistance,
	float PlayLength,
	float Time,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);

	// Find the distance sample that corresponds with the time and split into whole and partial parts
	float IntegralDistanceSample;
	float DistanceAlpha = FMath::Modf(ClampedTime * SamplingRate, &IntegralDistanceSample);
	float DistanceIdx = (int32)IntegralDistanceSample;

	// Verify the distance offset and any residual portion would be in bounds
	check(DistanceIdx + (DistanceAlpha > 0.0f ? 1 : 0) < AccumulatedRootDistance.Num());

	// Look up the distance and interpolate between distance samples if necessary
	float Distance = AccumulatedRootDistance[DistanceIdx];
	if (DistanceAlpha > 0.0f)
	{
		float NextDistance = AccumulatedRootDistance[DistanceIdx + 1];
		Distance = FMath::Lerp(Distance, NextDistance, DistanceAlpha);
	}

	const float ExtrapolationTime = Time - ClampedTime;

	if (ExtrapolationTime != 0.0f)
	{
		// If extrapolationTime is not zero, we extrapolate the beginning or the end of the animation to estimate
		// the root distance.
		const int32 DistIdx = (ExtrapolationTime > 0.0f) ? AccumulatedRootDistance.Num() - 1 : 1;
		const float ExtrapolatedDistance = ExtrapolateAccumulatedRootDistance(
			SamplingRate,
			AccumulatedRootDistance,
			DistIdx,
			ExtrapolationTime,
			ExtrapolationParameters);
		Distance += ExtrapolatedDistance;
	}

	return Distance;
}

static FTransform ExtrapolateRootMotion(
	FTransform SampleToExtrapolate,
	float SampleStart, 
	float SampleEnd, 
	float ExtrapolationTime,
	const FPoseSearchExtrapolationParameters& ExtrapolationParameters)
{
	const float SampleDelta = SampleEnd - SampleStart;
	check(!FMath::IsNearlyZero(SampleDelta));

	const FVector LinearVelocityToExtrapolate = SampleToExtrapolate.GetTranslation() / SampleDelta;
	const float LinearSpeedToExtrapolate = LinearVelocityToExtrapolate.Size();
	const bool bCanExtrapolateTranslation =
		LinearSpeedToExtrapolate >= ExtrapolationParameters.LinearSpeedThreshold;

	const float AngularSpeedToExtrapolateRad = SampleToExtrapolate.GetRotation().GetAngle() / SampleDelta;
	const bool bCanExtrapolateRotation =
		FMath::RadiansToDegrees(AngularSpeedToExtrapolateRad) >= ExtrapolationParameters.AngularSpeedThreshold;

	if (!bCanExtrapolateTranslation && !bCanExtrapolateRotation)
	{
		return FTransform::Identity;
	}

	if (!bCanExtrapolateTranslation)
	{
		SampleToExtrapolate.SetTranslation(FVector::ZeroVector);
	}

	if (!bCanExtrapolateRotation)
	{
		SampleToExtrapolate.SetRotation(FQuat::Identity);
	}

	// converting ExtrapolationTime to a positive number to avoid dealing with the negative extrapolation and inverting
	// transforms later on.
	const float AbsExtrapolationTime = FMath::Abs(ExtrapolationTime);
	const float AbsSampleDelta = FMath::Abs(SampleDelta);
	const FTransform AbsTimeSampleToExtrapolate =
		ExtrapolationTime >= 0.0f ? SampleToExtrapolate : SampleToExtrapolate.Inverse();

	// because we're extrapolating rotation, the extrapolation must be integrated over time
	const float SampleMultiplier = AbsExtrapolationTime / AbsSampleDelta;
	float IntegralNumSamples;
	float RemainingSampleFraction = FMath::Modf(SampleMultiplier, &IntegralNumSamples);
	int32 NumSamples = (int32)IntegralNumSamples;

	// adding full samples to the extrapolated root motion
	FTransform ExtrapolatedRootMotion = FTransform::Identity;
	for (int i = 0; i < NumSamples; ++i)
	{
		ExtrapolatedRootMotion = AbsTimeSampleToExtrapolate * ExtrapolatedRootMotion;
	}

	// and a blend with identity for whatever is left
	FTransform RemainingExtrapolatedRootMotion;
	RemainingExtrapolatedRootMotion.Blend(
		FTransform::Identity,
		AbsTimeSampleToExtrapolate,
		RemainingSampleFraction);

	ExtrapolatedRootMotion = RemainingExtrapolatedRootMotion * ExtrapolatedRootMotion;
	return ExtrapolatedRootMotion;
}


//////////////////////////////////////////////////////////////////////////
// FSequenceSampler

struct FSequenceSampler : public IAssetSampler
{
public:
	struct FInput
	{
		const UAnimSequence* Sequence = nullptr;
		bool bLoopable = false;
		int32 RootDistanceSamplingRate = 60;
		FPoseSearchExtrapolationParameters ExtrapolationParameters;
	} Input;

	void Init(const FInput& Input);
	void Process();

	float GetPlayLength() const override { return Input.Sequence->GetPlayLength(); };
	bool IsLoopable() const override { return Input.bLoopable; };

	float GetTimeFromRootDistance(float Distance) const override;

	float GetTotalRootDistance() const override { return TotalRootDistance; };
	FTransform GetTotalRootTransform() const override { return TotalRootTransform; }

	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const override;
	virtual float ExtractRootDistance(float Time) const override;
	virtual FTransform ExtractRootTransform(float Time) const override;
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<class UAnimNotifyState_PoseSearchBase*>& NotifyStates) const override;

private:
	float TotalRootDistance = 0.0f;
	FTransform TotalRootTransform = FTransform::Identity;
	TArray<float> AccumulatedRootDistance;

	void ProcessRootDistance();
};

void FSequenceSampler::Init(const FInput& InInput)
{
	check(InInput.Sequence);

	Input = InInput;
}

void FSequenceSampler::Process()
{
	ProcessRootDistance();
}

float FSequenceSampler::GetTimeFromRootDistance(float Distance) const
{
	int32 NextSampleIdx = 1;
	int32 PrevSampleIdx = 0;
	if (Distance > 0.0f)
	{
		// Search for the distance value. Because the values will be extrapolated if necessary
		// LowerBound might go past the end of the array, in which case the last valid index is used
		int32 ClipDistanceLowerBoundIndex = Algo::LowerBound(AccumulatedRootDistance, Distance);
		NextSampleIdx = FMath::Min(
			ClipDistanceLowerBoundIndex,
			AccumulatedRootDistance.Num() - 1);

		// Compute distance interpolation amount
		PrevSampleIdx = FMath::Max(0, NextSampleIdx - 1);
	}

	float NextDistance = AccumulatedRootDistance[NextSampleIdx];
	float PrevDistance = AccumulatedRootDistance[PrevSampleIdx];
	float DistanceSampleAlpha = FMath::GetRangePct(PrevDistance, NextDistance, Distance);

	// Convert to time
	float ClipTime = (float(NextSampleIdx) - (1.0f - DistanceSampleAlpha)) / Input.RootDistanceSamplingRate;
	return ClipTime;
}

void FSequenceSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	Input.Sequence->GetAnimationPose(OutAnimPoseData, ExtractionCtx);
}

FTransform FSequenceSampler::ExtractRootTransform(float Time) const
{
	if (Input.bLoopable)
	{
		FTransform LoopableRootTransform = Input.Sequence->ExtractRootMotion(0.0f, Time, true);
		return LoopableRootTransform;
	}

	const float ExtrapolationSampleTime = Input.ExtrapolationParameters.SampleTime;

	const float PlayLength = Input.Sequence->GetPlayLength();
	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
	const float ExtrapolationTime = Time - ClampedTime;

	FTransform RootTransform = FTransform::Identity;

	// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
	// animation to estimate where the root would be at Time
	if (ExtrapolationTime < -SMALL_NUMBER)
	{
		FTransform SampleToExtrapolate = Input.Sequence->ExtractRootMotionFromRange(0.0f, ExtrapolationSampleTime);

		const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
			SampleToExtrapolate,
			0.0f, ExtrapolationSampleTime, 
			ExtrapolationTime,
			Input.ExtrapolationParameters);
		RootTransform = ExtrapolatedRootMotion;
	}
	else
	{
		RootTransform = Input.Sequence->ExtractRootMotionFromRange(0.0f, ClampedTime);

		// If Time is greater than PlayLength, ExtrapolationTIme will be a positive number. In this case, we extrapolate
		// the end of the animation to estimate where the root would be at Time
		if (ExtrapolationTime > SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = Input.Sequence->ExtractRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

			const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
				SampleToExtrapolate,
				PlayLength - ExtrapolationSampleTime, PlayLength,
				ExtrapolationTime,
				Input.ExtrapolationParameters);
			RootTransform = ExtrapolatedRootMotion * RootTransform;
		}
	}

	return RootTransform;
}

float FSequenceSampler::ExtractRootDistance(float Time) const
{
	return ExtractAccumulatedRootDistance(
		Input.RootDistanceSamplingRate,
		AccumulatedRootDistance,
		Input.Sequence->GetPlayLength(),
		Time,
		Input.ExtrapolationParameters);
}

void FSequenceSampler::ExtractPoseSearchNotifyStates(
	float Time, 
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
	constexpr float ExtractionInterval = 1.0f / 120.0f;
	FAnimNotifyContext NotifyContext;
	Input.Sequence->GetAnimNotifies(Time - (ExtractionInterval * 0.5f), ExtractionInterval, NotifyContext);

	// check which notifies actually overlap Time and are of the right base type
	for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
	{
		const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
		if (!NotifyEvent)
		{
			continue;
		}

		if (NotifyEvent->GetTriggerTime() > Time ||
			NotifyEvent->GetEndTriggerTime() < Time)
		{
			continue;
		}

		UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify = 
			Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
		if (PoseSearchAnimNotify)
		{
			NotifyStates.Add(PoseSearchAnimNotify);
		}
	}
}

void FSequenceSampler::ProcessRootDistance()
{
	// Note the distance sampling interval is independent of the schema's sampling interval
	const float DistanceSamplingInterval = 1.0f / Input.RootDistanceSamplingRate;

	const FTransform InitialRootTransform = Input.Sequence->ExtractRootTrackTransform(0.0f, nullptr);

	uint32 NumDistanceSamples = FMath::CeilToInt(Input.Sequence->GetPlayLength() * Input.RootDistanceSamplingRate) + 1;
	AccumulatedRootDistance.Reserve(NumDistanceSamples);

	// Build a distance lookup table by sampling root motion at a fixed rate and accumulating
	// absolute translation deltas. During indexing we'll bsearch this table and interpolate
	// between samples in order to convert distance offsets to time offsets.
	// See also FAssetIndexer::AddTrajectoryDistanceFeatures().

	double TotalAccumulatedRootDistance = 0.0;
	FTransform LastRootTransform = InitialRootTransform;
	float SampleTime = 0.0f;
	for (int32 SampleIdx = 0; SampleIdx != NumDistanceSamples; ++SampleIdx)
	{
		SampleTime = FMath::Min(SampleIdx * DistanceSamplingInterval, Input.Sequence->GetPlayLength());

		FTransform RootTransform = Input.Sequence->ExtractRootTrackTransform(SampleTime, nullptr);
		FTransform LocalRootMotion = RootTransform.GetRelativeTransform(LastRootTransform);
		LastRootTransform = RootTransform;

		TotalAccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		AccumulatedRootDistance.Add((float)TotalAccumulatedRootDistance);
	}

	// Verify we sampled the final frame of the clip
	check(SampleTime == Input.Sequence->GetPlayLength());

	// Also emit root motion summary info to help with sample wrapping in 
	// FAssetIndexer::GetSampleTimeFromDistance() and FAssetIndexer::GetSampleInfo()
	TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	TotalRootDistance = AccumulatedRootDistance.Last();
}

//////////////////////////////////////////////////////////////////////////
// FBlendSpaceSampler

struct FBlendSpaceSampler : public IAssetSampler
{
public:
	struct FInput
	{
		const FAssetSamplingContext* SamplingContext = nullptr;
		const UBlendSpace* BlendSpace = nullptr;
		bool bLoopable = false;
		int32 RootDistanceSamplingRate = 60;
		int32 RootTransformSamplingRate = 60;
		FPoseSearchExtrapolationParameters ExtrapolationParameters;
		FVector BlendParameters;
	} Input;

	void Init(const FInput& Input);

	void Process();

	float GetPlayLength() const override { return PlayLength; };
	bool IsLoopable() const override { return Input.bLoopable; };

	float GetTimeFromRootDistance(float Distance) const override;

	float GetTotalRootDistance() const override { return TotalRootDistance; };
	FTransform GetTotalRootTransform() const override { return TotalRootTransform; }

	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const override;
	virtual float ExtractRootDistance(float Time) const override;
	virtual FTransform ExtractRootTransform(float Time) const override;
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<class UAnimNotifyState_PoseSearchBase*>& NotifyStates) const override;

private:
	float PlayLength = 0.0f;
	float TotalRootDistance = 0.0f;
	FTransform TotalRootTransform = FTransform::Identity;
	TArray<float> AccumulatedRootDistance;
	TArray<FTransform> AccumulatedRootTransform;
	
	void ProcessPlayLength();
	void ProcessRootDistance();
	void ProcessRootTransform();

	// Extracts the pre-computed blend space root transform. ProcessRootTransform must be run first.
	FTransform ExtractBlendSpaceRootTrackTransform(float Time) const;
	FTransform ExtractBlendSpaceRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const;
	FTransform ExtractBlendSpaceRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const;
};

void FBlendSpaceSampler::Init(const FInput& InInput)
{
	check(InInput.BlendSpace);

	Input = InInput;
}

void FBlendSpaceSampler::Process()
{
	FMemMark Mark(FMemStack::Get());

	ProcessPlayLength();
	ProcessRootTransform();
	ProcessRootDistance();
}

float FBlendSpaceSampler::GetTimeFromRootDistance(float Distance) const
{
	int32 NextSampleIdx = 1;
	int32 PrevSampleIdx = 0;
	if (Distance > 0.0f)
	{
		// Search for the distance value. Because the values will be extrapolated if necessary
		// LowerBound might go past the end of the array, in which case the last valid index is used
		int32 ClipDistanceLowerBoundIndex = Algo::LowerBound(AccumulatedRootDistance, Distance);
		NextSampleIdx = FMath::Min(
			ClipDistanceLowerBoundIndex,
			AccumulatedRootDistance.Num() - 1);

		// Compute distance interpolation amount
		PrevSampleIdx = FMath::Max(0, NextSampleIdx - 1);
	}

	float NextDistance = AccumulatedRootDistance[NextSampleIdx];
	float PrevDistance = AccumulatedRootDistance[PrevSampleIdx];
	float DistanceSampleAlpha = FMath::GetRangePct(PrevDistance, NextDistance, Distance);

	// Convert to time
	float ClipTime = (float(NextSampleIdx) - (1.0f - DistanceSampleAlpha)) / Input.RootDistanceSamplingRate;
	return ClipTime;
}

void FBlendSpaceSampler::ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
	{
		float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

		FDeltaTimeRecord BlendSampleDeltaTimeRecord;
		BlendSampleDeltaTimeRecord.Set(ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale, ExtractionCtx.DeltaTimeRecord.Delta * Scale);

		BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
		BlendSamples[BlendSampleIdex].PreviousTime = ExtractionCtx.DeltaTimeRecord.GetPrevious() * Scale;
		BlendSamples[BlendSampleIdex].Time = ExtractionCtx.CurrentTime * Scale;
	}

	Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, OutAnimPoseData);
}

FTransform FBlendSpaceSampler::ExtractRootTransform(float Time) const
{
	if (Input.bLoopable)
	{
		FTransform LoopableRootTransform = ExtractBlendSpaceRootMotion(0.0f, Time, true);
		return LoopableRootTransform;
	}

	const float ExtrapolationSampleTime = Input.ExtrapolationParameters.SampleTime;

	const float ClampedTime = FMath::Clamp(Time, 0.0f, PlayLength);
	const float ExtrapolationTime = Time - ClampedTime;

	FTransform RootTransform = FTransform::Identity;

	// If Time is less than zero, ExtrapolationTime will be negative. In this case, we extrapolate the beginning of the 
	// animation to estimate where the root would be at Time
	if (ExtrapolationTime < -SMALL_NUMBER)
	{
		FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(0.0f, ExtrapolationSampleTime);

		const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
			SampleToExtrapolate,
			0.0f, ExtrapolationSampleTime,
			ExtrapolationTime,
			Input.ExtrapolationParameters);
		RootTransform = ExtrapolatedRootMotion;
	}
	else
	{
		RootTransform = ExtractBlendSpaceRootMotionFromRange(0.0f, ClampedTime);

		// If Time is greater than PlayLength, ExtrapolationTIme will be a positive number. In this case, we extrapolate
		// the end of the animation to estimate where the root would be at Time
		if (ExtrapolationTime > SMALL_NUMBER)
		{
			FTransform SampleToExtrapolate = ExtractBlendSpaceRootMotionFromRange(PlayLength - ExtrapolationSampleTime, PlayLength);

			const FTransform ExtrapolatedRootMotion = ExtrapolateRootMotion(
				SampleToExtrapolate,
				PlayLength - ExtrapolationSampleTime, PlayLength,
				ExtrapolationTime,
				Input.ExtrapolationParameters);
			RootTransform = ExtrapolatedRootMotion * RootTransform;
		}
	}

	return RootTransform;
}

float FBlendSpaceSampler::ExtractRootDistance(float Time) const
{
	return ExtractAccumulatedRootDistance(
		Input.RootDistanceSamplingRate,
		AccumulatedRootDistance,
		PlayLength,
		Time,
		Input.ExtrapolationParameters);
}

static int32 GetHighestWeightSample(const TArray<struct FBlendSampleData>& SampleDataList)
{
	int32 HighestWeightIndex = 0;
	float HighestWeight = SampleDataList[HighestWeightIndex].GetClampedWeight();
	for (int32 I = 1; I < SampleDataList.Num(); I++)
	{
		if (SampleDataList[I].GetClampedWeight() > HighestWeight)
		{
			HighestWeightIndex = I;
			HighestWeight = SampleDataList[I].GetClampedWeight();
		}
	}
	return HighestWeightIndex;
}

void FBlendSpaceSampler::ExtractPoseSearchNotifyStates(
	float Time,
	TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const
{
	if (Input.BlendSpace->NotifyTriggerMode == ENotifyTriggerMode::HighestWeightedAnimation)
	{
		// Set up blend samples
		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

		// Find highest weighted
		const int32 HighestWeightIndex = GetHighestWeightSample(BlendSamples);

		check(HighestWeightIndex != -1);

		// getting pose search notifies in an interval of size ExtractionInterval, centered on Time
		constexpr float ExtractionInterval = 1.0f / 120.0f;

		float SampleTime = Time * (BlendSamples[HighestWeightIndex].Animation->GetPlayLength() / PlayLength);

		// Get notifies for highest weighted
		FAnimNotifyContext NotifyContext;
		BlendSamples[HighestWeightIndex].Animation->GetAnimNotifies(
			(SampleTime - (ExtractionInterval * 0.5f)),
			ExtractionInterval, 
			NotifyContext);

		// check which notifies actually overlap Time and are of the right base type
		for (const FAnimNotifyEventReference& EventReference : NotifyContext.ActiveNotifies)
		{
			const FAnimNotifyEvent* NotifyEvent = EventReference.GetNotify();
			if (!NotifyEvent)
			{
				continue;
			}

			if (NotifyEvent->GetTriggerTime() > SampleTime ||
				NotifyEvent->GetEndTriggerTime() < SampleTime)
			{
				continue;
			}

			UAnimNotifyState_PoseSearchBase* PoseSearchAnimNotify =
				Cast<UAnimNotifyState_PoseSearchBase>(NotifyEvent->NotifyStateClass);
			if (PoseSearchAnimNotify)
			{
				NotifyStates.Add(PoseSearchAnimNotify);
			}
		}
	}
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootTrackTransform(float Time) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	int32 Index = Time * Input.RootTransformSamplingRate;
	int32 FirstIndexClamped = FMath::Clamp(Index + 0, 0, AccumulatedRootTransform.Num() - 1);
	int32 SecondIndexClamped = FMath::Clamp(Index + 1, 0, AccumulatedRootTransform.Num() - 1);
	float Alpha = FMath::Fmod(Time * Input.RootTransformSamplingRate, 1.0f);
	FTransform OutputTransform;
	OutputTransform.Blend(
		AccumulatedRootTransform[FirstIndexClamped],
		AccumulatedRootTransform[SecondIndexClamped],
		Alpha);

	return OutputTransform;
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	FTransform RootTransformRefPose = ExtractBlendSpaceRootTrackTransform(0.0f);

	FTransform StartTransform = ExtractBlendSpaceRootTrackTransform(StartTrackPosition);
	FTransform EndTransform = ExtractBlendSpaceRootTrackTransform(EndTrackPosition);

	// Transform to Component Space
	const FTransform RootToComponent = RootTransformRefPose.Inverse();
	StartTransform = RootToComponent * StartTransform;
	EndTransform = RootToComponent * EndTransform;

	return EndTransform.GetRelativeTransform(StartTransform);
}

FTransform FBlendSpaceSampler::ExtractBlendSpaceRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		bool const bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// Disable looping here. Advance to desired position, or beginning / end of animation 
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, PlayLength);

			// Verify position assumptions
			//ensureMsgf(bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition), TEXT("in Animation %s(Skeleton %s) : bPlayingBackwards(%d), PreviousPosition(%0.2f), Current Position(%0.2f)"),
			//	*GetName(), *GetNameSafe(GetSkeleton()), bPlayingBackwards, PreviousPosition, CurrentPosition);

			RootMotionParams.Accumulate(ExtractBlendSpaceRootMotionFromRange(PreviousPosition, CurrentPosition));

			// If we've hit the end of the animation, and we're allowed to loop, keep going.
			if ((AdvanceType == ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? PlayLength : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

void FBlendSpaceSampler::ProcessPlayLength()
{
	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	PlayLength = Input.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);

	checkf(PlayLength > 0.0f, TEXT("Blendspace has zero play length"));
}

void FBlendSpaceSampler::ProcessRootTransform()
{
	// Pre-compute root motion

	int32 NumRootSamples = FMath::Max(PlayLength * Input.RootTransformSamplingRate + 1, 1);
	AccumulatedRootTransform.SetNumUninitialized(NumRootSamples);

	TArray<FBlendSampleData> BlendSamples;
	int32 TriangulationIndex = 0;
	Input.BlendSpace->GetSamplesFromBlendInput(Input.BlendParameters, BlendSamples, TriangulationIndex, true);

	FTransform RootMotionAccumulation = FTransform::Identity;

	AccumulatedRootTransform[0] = RootMotionAccumulation;

	for (int32 SampleIdx = 1; SampleIdx < NumRootSamples; ++SampleIdx)
	{
		float PreviousTime = float(SampleIdx - 1) / Input.RootTransformSamplingRate;
		float CurrentTime = float(SampleIdx - 0) / Input.RootTransformSamplingRate;

		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, CurrentTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(CurrentTime, true, DeltaTimeRecord, Input.bLoopable);

		for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
		{
			float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

			FDeltaTimeRecord BlendSampleDeltaTimeRecord;
			BlendSampleDeltaTimeRecord.Set(DeltaTimeRecord.GetPrevious() * Scale, DeltaTimeRecord.Delta * Scale);

			BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
			BlendSamples[BlendSampleIdex].PreviousTime = PreviousTime * Scale;
			BlendSamples[BlendSampleIdex].Time = CurrentTime * Scale;
		}

		FCompactPose Pose;
		FBlendedCurve BlendedCurve;
		Anim::FStackAttributeContainer StackAttributeContainer;
		FAnimationPoseData AnimPoseData(Pose, BlendedCurve, StackAttributeContainer);

		Pose.SetBoneContainer(&Input.SamplingContext->BoneContainer);
		BlendedCurve.InitFrom(Input.SamplingContext->BoneContainer);

		Input.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, AnimPoseData);

		const Anim::IAnimRootMotionProvider* RootMotionProvider = Anim::IAnimRootMotionProvider::Get();

		if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
		{
			if (ensureMsgf(RootMotionProvider->HasRootMotion(StackAttributeContainer), TEXT("Blend Space had no Root Motion Attribute.")))
			{
				FTransform RootMotionDelta;
				RootMotionProvider->ExtractRootMotion(StackAttributeContainer, RootMotionDelta);

				RootMotionAccumulation = RootMotionDelta * RootMotionAccumulation;
			}
		}

		AccumulatedRootTransform[SampleIdx] = RootMotionAccumulation;
	}
}

void FBlendSpaceSampler::ProcessRootDistance()
{
	checkf(AccumulatedRootTransform.Num() > 0, TEXT("ProcessRootTransform must be run first"));

	// Note the distance sampling interval is independent of the schema's sampling interval
	const float DistanceSamplingInterval = 1.0f / Input.RootDistanceSamplingRate;

	const FTransform InitialRootTransform = FTransform::Identity;

	uint32 NumDistanceSamples = FMath::CeilToInt(PlayLength * Input.RootDistanceSamplingRate) + 1;
	AccumulatedRootDistance.Reserve(NumDistanceSamples);

	// Build a distance lookup table by sampling root motion at a fixed rate and accumulating
	// absolute translation deltas. During indexing we'll bsearch this table and interpolate
	// between samples in order to convert distance offsets to time offsets.
	// See also FAssetIndexer::AddTrajectoryDistanceFeatures().
	double TotalAccumulatedRootDistance = 0.0;
	FTransform LastRootTransform = InitialRootTransform;
	float SampleTime = 0.0f;
	for (int32 SampleIdx = 0; SampleIdx != NumDistanceSamples; ++SampleIdx)
	{
		SampleTime = FMath::Min(SampleIdx * DistanceSamplingInterval, PlayLength);

		FTransform RootTransform = ExtractBlendSpaceRootTrackTransform(SampleTime);
		FTransform LocalRootMotion = RootTransform.GetRelativeTransform(LastRootTransform);
		LastRootTransform = RootTransform;

		TotalAccumulatedRootDistance += LocalRootMotion.GetTranslation().Size();
		AccumulatedRootDistance.Add((float)TotalAccumulatedRootDistance);
	}

	// Verify we sampled the final frame of the clip
	check(SampleTime == PlayLength);

	// Also emit root motion summary info to help with sample wrapping in 
	// FAssetIndexer::GetSampleTimeFromDistance() and FAssetIndexer::GetSampleInfo()
	TotalRootTransform = LastRootTransform.GetRelativeTransform(InitialRootTransform);
	TotalRootDistance = AccumulatedRootDistance.Last();
}

//////////////////////////////////////////////////////////////////////////
// FAssetIndexer helpers

struct FSamplingParam
{
	float WrappedParam = 0.0f;
	int32 NumCycles = 0;
	
	// If the animation can't loop, WrappedParam contains the clamped value and whatever is left is stored here
	float Extrapolation = 0.0f;
};

static FSamplingParam WrapOrClampSamplingParam(bool bCanWrap, float SamplingParamExtent, float SamplingParam)
{
	// This is a helper function used by both time and distance sampling. A schema may specify time or distance
	// offsets that are multiple cycles of a clip away from the current pose being sampled.
	// And that time or distance offset may before the beginning of the clip (SamplingParam < 0.0f)
	// or after the end of the clip (SamplingParam > SamplingParamExtent). So this function
	// helps determine how many cycles need to be applied and what the wrapped value should be, clamping
	// if necessary.

	FSamplingParam Result;

	Result.WrappedParam = SamplingParam;

	if (bCanWrap)
	{
		if (SamplingParam < 0.0f)
		{
			while (Result.WrappedParam < 0.0f)
			{
				Result.WrappedParam += SamplingParamExtent;
				++Result.NumCycles;
			}
		}

		else
		{
			while (Result.WrappedParam > SamplingParamExtent)
			{
				Result.WrappedParam -= SamplingParamExtent;
				++Result.NumCycles;
			}
		}
	}

	float ParamClamped = FMath::Clamp(Result.WrappedParam, 0.0f, SamplingParamExtent);
	if (ParamClamped != Result.WrappedParam)
	{
		check(!bCanWrap);
		Result.Extrapolation = Result.WrappedParam - ParamClamped;
		Result.WrappedParam = ParamClamped;
	}
	
	return Result;
}


//////////////////////////////////////////////////////////////////////////
// FAssetIndexer

class FAssetIndexer : public IAssetIndexer
{
public:

	struct FOutput
	{
		int32 FirstIndexedSample = 0;
		int32 LastIndexedSample = 0;
		int32 NumIndexedPoses = 0;
		TArray<float> FeatureVectorTable;
		TArray<FPoseSearchPoseMetadata> PoseMetadata;
		TSet<int32> InvalidChannels;
		TBitArray<> AllFeaturesNotAdded;
	} Output;

	void Reset();
	void Init(const FAssetIndexingContext& IndexingContext);
	bool Process();

public: // IAssetIndexer

	const FAssetIndexingContext& GetIndexingContext() const override { return IndexingContext; }
	FSampleInfo GetSampleInfo(float SampleTime) const override;
	FSampleInfo GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const override;
	const float GetSampleTimeFromDistance(float Distance) const override;
	FTransform MirrorTransform(const FTransform& Transform) const override;

private:
	FAssetIndexingContext IndexingContext;
	TArray<FPoseSearchFeatureVectorBuilder> FeatureVectorBuilders;
	FPoseSearchPoseMetadata Metadata;
	
	void AddMetadata(int32 SampleIdx);
};

void FAssetIndexer::Reset()
{
	Output.FirstIndexedSample = 0;
	Output.LastIndexedSample = 0;
	Output.NumIndexedPoses = 0;

	Output.FeatureVectorTable.Reset(0);
	Output.PoseMetadata.Reset(0);
	Output.InvalidChannels.Reset();
	Output.AllFeaturesNotAdded.Reset();
}

void FAssetIndexer::Init(const FAssetIndexingContext& InIndexingContext)
{
	check(InIndexingContext.Schema);
	check(InIndexingContext.Schema->IsValid());
	check(InIndexingContext.MainSampler);

	IndexingContext = InIndexingContext;

	Reset();

	Output.FirstIndexedSample = FMath::FloorToInt(IndexingContext.RequestedSamplingRange.Min * IndexingContext.Schema->SampleRate);
	Output.LastIndexedSample = 
		FMath::Max(0, FMath::CeilToInt(IndexingContext.RequestedSamplingRange.Max * IndexingContext.Schema->SampleRate));
	Output.NumIndexedPoses = Output.LastIndexedSample - Output.FirstIndexedSample + 1;
	
	Output.FeatureVectorTable.SetNumZeroed(IndexingContext.Schema->Layout.NumFloats * Output.NumIndexedPoses);

	Output.PoseMetadata.SetNum(Output.NumIndexedPoses);

	FeatureVectorBuilders.SetNum(Output.NumIndexedPoses);
	for (FPoseSearchFeatureVectorBuilder& Builder : FeatureVectorBuilders)
	{
		Builder.Init(IndexingContext.Schema);
	}
}

bool FAssetIndexer::Process()
{
	FMemMark Mark(FMemStack::Get());

	IndexingContext.BeginSampleIdx = Output.FirstIndexedSample;
	IndexingContext.EndSampleIdx = Output.LastIndexedSample + 1;
	const int32 NumSamplesInRange = IndexingContext.EndSampleIdx - IndexingContext.BeginSampleIdx;

	FAssetIndexingOutput AssetIndexingOutput{FeatureVectorBuilders};

	// Index each channel
	for (int32 ChannelIdx = 0; ChannelIdx != IndexingContext.Schema->Channels.Num(); ++ChannelIdx)
	{
		const TObjectPtr<UPoseSearchFeatureChannel>& Channel = IndexingContext.Schema->Channels[ChannelIdx];
		Channel->IndexAsset(*this, AssetIndexingOutput);
	}

	// Verify all channels provided a complete feature vector for all poses
	TBitArray<> PoseFeaturesNotAdded;
	for (int32 VectorIdx = 0; VectorIdx != NumSamplesInRange; ++VectorIdx)
	{
		if (!FeatureVectorBuilders[VectorIdx].IsComplete())
		{
			PoseFeaturesNotAdded = FeatureVectorBuilders[VectorIdx].GetFeaturesAdded();
			PoseFeaturesNotAdded.BitwiseNOT();
			TBitArray<>::BitwiseOR(Output.AllFeaturesNotAdded, PoseFeaturesNotAdded, EBitwiseOperatorFlags::MaxSize);

			for (TConstSetBitIterator<> FeatureIter(PoseFeaturesNotAdded); FeatureIter; ++FeatureIter)
			{
				int32 FeatureIdx = FeatureIter.GetIndex();
				const FPoseSearchFeatureDesc& FeatureDesc = IndexingContext.Schema->Layout.Features[FeatureIdx];
				Output.InvalidChannels.Add(FeatureDesc.ChannelIdx);
			}
		}
	}

	Output.InvalidChannels.Sort(TLess<>());

	// Merge spans of feature vectors into contiguous buffer
	for (int32 VectorIdx = 0; VectorIdx != NumSamplesInRange; ++VectorIdx)
	{
		if (FeatureVectorBuilders[VectorIdx].IsComplete())
		{
			const int32 SampleIdx = VectorIdx + IndexingContext.BeginSampleIdx;
			const int32 PoseIdx = SampleIdx - Output.FirstIndexedSample;
			const int32 FirstValueIdx = PoseIdx * IndexingContext.Schema->Layout.NumFloats;
			TArrayView<float> WriteValues = MakeArrayView(&Output.FeatureVectorTable[FirstValueIdx], IndexingContext.Schema->Layout.NumFloats);
			TArrayView<const float> ReadValues = FeatureVectorBuilders[VectorIdx].GetValues();
			check(WriteValues.Num() == ReadValues.Num());
			FMemory::Memcpy(WriteValues.GetData(), ReadValues.GetData(), WriteValues.Num() * WriteValues.GetTypeSize());
		}
	}

	// Generate pose metadata
	for (int32 SampleIdx = IndexingContext.BeginSampleIdx; SampleIdx != IndexingContext.EndSampleIdx; ++SampleIdx)
	{
		AddMetadata(SampleIdx);
		const int32 PoseIdx = SampleIdx - Output.FirstIndexedSample;
		Output.PoseMetadata[PoseIdx] = Metadata;
	}

	return Output.InvalidChannels.IsEmpty();
}

const float FAssetIndexer::GetSampleTimeFromDistance(float SampleDistance) const
{
	auto CanWrapDistanceSamples = [](const IAssetSampler* Sampler) -> bool
	{
		constexpr float SMALL_ROOT_DISTANCE = 1.0f;
		return Sampler->IsLoopable() && Sampler->GetTotalRootDistance() > SMALL_ROOT_DISTANCE;
	};

	float MainTotalDistance = IndexingContext.MainSampler->GetTotalRootDistance();
	bool bMainCanWrap = CanWrapDistanceSamples(IndexingContext.MainSampler);

	float SampleTime = MAX_flt;

	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (IndexingContext.LeadInSampler && (SampleDistance < 0.0f))
		{
			const IAssetSampler* ClipSampler = IndexingContext.LeadInSampler;

			bool bLeadInCanWrap = CanWrapDistanceSamples(IndexingContext.LeadInSampler);
			float LeadRelativeDistance = SampleDistance + ClipSampler->GetTotalRootDistance();
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipSampler->GetTotalRootDistance(), LeadRelativeDistance);

			float ClipTime = ClipSampler->GetTimeFromRootDistance(
				SamplingParam.WrappedParam + SamplingParam.Extrapolation);

			// Make the lead in clip time relative to the main sequence again and unwrap
			SampleTime = -((SamplingParam.NumCycles * ClipSampler->GetPlayLength()) + (ClipSampler->GetPlayLength() - ClipTime));
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (IndexingContext.FollowUpSampler && (SampleDistance > MainTotalDistance))
		{
			const IAssetSampler* ClipSampler = IndexingContext.FollowUpSampler;

			bool bFollowUpCanWrap = CanWrapDistanceSamples(IndexingContext.FollowUpSampler);
			float FollowRelativeDistance = SampleDistance - MainTotalDistance;
			FSamplingParam SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipSampler->GetTotalRootDistance(), FollowRelativeDistance);

			float ClipTime = ClipSampler->GetTimeFromRootDistance(
				SamplingParam.WrappedParam + SamplingParam.Extrapolation);

			// Make the follow up clip time relative to the main sequence again and unwrap
			SampleTime = IndexingContext.MainSampler->GetPlayLength() + SamplingParam.NumCycles * ClipSampler->GetPlayLength() + ClipTime;
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (SampleTime == MAX_flt)
	{
		float MainRelativeDistance = SampleDistance;
		if (SampleDistance < 0.0f && bMainCanWrap)
		{
			// In this case we're sampling a loop backwards, so MainRelativeDistance must adjust so the number of cycles 
			// is counted correctly.
			MainRelativeDistance += IndexingContext.MainSampler->GetTotalRootDistance();
		}

		FSamplingParam SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainTotalDistance, MainRelativeDistance);
		float ClipTime = IndexingContext.MainSampler->GetTimeFromRootDistance(
			SamplingParam.WrappedParam + SamplingParam.Extrapolation);

		// Unwrap the main clip time
		if (bMainCanWrap)
		{
			if (SampleDistance < 0.0f)
			{
				SampleTime = -((SamplingParam.NumCycles * IndexingContext.MainSampler->GetPlayLength()) + (IndexingContext.MainSampler->GetPlayLength() - ClipTime));
			}
			else
			{
				SampleTime = SamplingParam.NumCycles * IndexingContext.MainSampler->GetPlayLength() + ClipTime;
			}
		}
		else
		{
			SampleTime = ClipTime;
		}
	}

	return SampleTime;
}

FAssetIndexer::FSampleInfo FAssetIndexer::GetSampleInfo(float SampleTime) const
{
	FSampleInfo Sample;

	FTransform RootMotionLast = FTransform::Identity;
	FTransform RootMotionInitial = FTransform::Identity;

	float RootDistanceLast = 0.0f;
	float RootDistanceInitial = 0.0f;

	auto CanWrapTimeSamples = [](const IAssetSampler* Sampler) -> bool
	{
		return Sampler->IsLoopable();
	};

	float MainPlayLength = IndexingContext.MainSampler->GetPlayLength();
	bool bMainCanWrap = CanWrapTimeSamples(IndexingContext.MainSampler);

	FSamplingParam SamplingParam;
	if (!bMainCanWrap)
	{
		// Use the lead in anim if we would have to clamp to the beginning of the main anim
		if (IndexingContext.LeadInSampler && (SampleTime < 0.0f))
		{
			const IAssetSampler* ClipSampler = IndexingContext.LeadInSampler;

			bool bLeadInCanWrap = CanWrapTimeSamples(IndexingContext.LeadInSampler);
			float LeadRelativeTime = SampleTime + ClipSampler->GetPlayLength();
			SamplingParam = WrapOrClampSamplingParam(bLeadInCanWrap, ClipSampler->GetPlayLength(), LeadRelativeTime);

			Sample.Clip = IndexingContext.LeadInSampler;

			check(SamplingParam.Extrapolation <= 0.0f);
			if (SamplingParam.Extrapolation < 0.0f)
			{
				RootMotionInitial = IndexingContext.LeadInSampler->GetTotalRootTransform().Inverse();
				RootDistanceInitial = -IndexingContext.LeadInSampler->GetTotalRootDistance();
			}
			else
			{
				RootMotionInitial = FTransform::Identity;
				RootDistanceInitial = 0.0f;
			}

			RootMotionLast = IndexingContext.LeadInSampler->GetTotalRootTransform();
			RootDistanceLast = IndexingContext.LeadInSampler->GetTotalRootDistance();
		}

		// Use the follow up anim if we would have clamp to the end of the main anim
		else if (IndexingContext.FollowUpSampler && (SampleTime > MainPlayLength))
		{
			const IAssetSampler* ClipSampler = IndexingContext.FollowUpSampler;

			bool bFollowUpCanWrap = CanWrapTimeSamples(IndexingContext.FollowUpSampler);
			float FollowRelativeTime = SampleTime - MainPlayLength;
			SamplingParam = WrapOrClampSamplingParam(bFollowUpCanWrap, ClipSampler->GetPlayLength(), FollowRelativeTime);

			Sample.Clip = IndexingContext.FollowUpSampler;

			RootMotionInitial = IndexingContext.MainSampler->GetTotalRootTransform();
			RootDistanceInitial = IndexingContext.MainSampler->GetTotalRootDistance();

			RootMotionLast = IndexingContext.FollowUpSampler->GetTotalRootTransform();
			RootDistanceLast = IndexingContext.FollowUpSampler->GetTotalRootDistance();
		}
	}

	// Use the main anim if we didn't use the lead-in or follow-up anims.
	// The main anim sample may have been wrapped or clamped
	if (!Sample.IsValid())
	{
		float MainRelativeTime = SampleTime;
		if (SampleTime < 0.0f && bMainCanWrap)
		{
			// In this case we're sampling a loop backwards, so MainRelativeTime must adjust so the number of cycles is
			// counted correctly.
			MainRelativeTime += MainPlayLength;
		}

		SamplingParam = WrapOrClampSamplingParam(bMainCanWrap, MainPlayLength, MainRelativeTime);

		Sample.Clip = IndexingContext.MainSampler;

		RootMotionInitial = FTransform::Identity;
		RootDistanceInitial = 0.0f;

		RootMotionLast = IndexingContext.MainSampler->GetTotalRootTransform();
		RootDistanceLast = IndexingContext.MainSampler->GetTotalRootDistance();
	}


	if (FMath::Abs(SamplingParam.Extrapolation) > SMALL_NUMBER)
	{
		Sample.bClamped = true;
		Sample.ClipTime = SamplingParam.WrappedParam + SamplingParam.Extrapolation;
		const FTransform ClipRootMotion = Sample.Clip->ExtractRootTransform(Sample.ClipTime);
		const float ClipDistance = Sample.Clip->ExtractRootDistance(Sample.ClipTime);

		Sample.RootTransform = ClipRootMotion * RootMotionInitial;
		Sample.RootDistance = RootDistanceInitial + ClipDistance;
	}
	else
	{
		Sample.ClipTime = SamplingParam.WrappedParam;

		// Determine how to accumulate motion for every cycle of the anim. If the sample
		// had to be clamped, this motion will end up not getting applied below.
		// Also invert the accumulation direction if the requested sample was wrapped backwards.
		FTransform RootMotionPerCycle = RootMotionLast;
		float RootDistancePerCycle = RootDistanceLast;
		if (SampleTime < 0.0f)
		{
			RootMotionPerCycle = RootMotionPerCycle.Inverse();
			RootDistancePerCycle *= -1;
		}

		// Find the remaining motion deltas after wrapping
		FTransform RootMotionRemainder = Sample.Clip->ExtractRootTransform(Sample.ClipTime);
		float RootDistanceRemainder = Sample.Clip->ExtractRootDistance(Sample.ClipTime);

		// Invert motion deltas if we wrapped backwards
		if (SampleTime < 0.0f)
		{
			RootMotionRemainder.SetToRelativeTransform(RootMotionLast);
			RootDistanceRemainder = -(RootDistanceLast - RootDistanceRemainder);
		}

		Sample.RootTransform = RootMotionInitial;
		Sample.RootDistance = RootDistanceInitial;

		// Note if the sample was clamped, no motion will be applied here because NumCycles will be zero
		int32 CyclesRemaining = SamplingParam.NumCycles;
		while (CyclesRemaining--)
		{
			Sample.RootTransform = RootMotionPerCycle * Sample.RootTransform;
         	Sample.RootDistance += RootDistancePerCycle;
		}

		Sample.RootTransform = RootMotionRemainder * Sample.RootTransform;
		Sample.RootDistance += RootDistanceRemainder;
	}

	return Sample;
}

FAssetIndexer::FSampleInfo FAssetIndexer::GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const
{
	FSampleInfo Sample = GetSampleInfo(SampleTime);
	Sample.RootTransform.SetToRelativeTransform(Origin.RootTransform);
	Sample.RootDistance = Origin.RootDistance - Sample.RootDistance;
	return Sample;
}

FTransform FAssetIndexer::MirrorTransform(const FTransform& Transform) const
{
	return IndexingContext.bMirrored ? IndexingContext.SamplingContext->MirrorTransform(Transform) : Transform;
}

void FAssetIndexer::AddMetadata(int32 SampleIdx)
{
	const float SequenceLength = IndexingContext.MainSampler->GetPlayLength();
	const float SampleTime = FMath::Min(SampleIdx * IndexingContext.Schema->SamplingInterval, SequenceLength);

	Metadata = FPoseSearchPoseMetadata();

	const bool bBlockTransition =
		!IndexingContext.MainSampler->IsLoopable() &&
		(SampleTime < IndexingContext.RequestedSamplingRange.Min + IndexingContext.BlockTransitionParameters.SequenceStartInterval ||
		 SampleTime > IndexingContext.RequestedSamplingRange.Max - IndexingContext.BlockTransitionParameters.SequenceEndInterval);

	if (bBlockTransition)
	{
		EnumAddFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition);
	}

	TArray<UAnimNotifyState_PoseSearchBase*> NotifyStates;
	IndexingContext.MainSampler->ExtractPoseSearchNotifyStates(SampleTime, NotifyStates);
	for (const UAnimNotifyState_PoseSearchBase* PoseSearchNotify : NotifyStates)
	{
		if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchBlockTransition>())
		{
			EnumAddFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition);
		}
		else if (PoseSearchNotify->GetClass()->IsChildOf<UAnimNotifyState_PoseSearchModifyCost>())
		{
			const UAnimNotifyState_PoseSearchModifyCost* ModifyCostNotify =
				Cast<const UAnimNotifyState_PoseSearchModifyCost>(PoseSearchNotify);
			Metadata.CostAddend = ModifyCostNotify->CostAddend;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// PoseSearch API

static void DrawFeatureVector(const FDebugDrawParams& DrawParams, const FFeatureVectorReader& Reader)
{
	const UPoseSearchSchema* Schema = DrawParams.GetSchema();

	for (int32 ChannelIdx = 0; ChannelIdx != Schema->Channels.Num(); ++ChannelIdx)
	{
		if (DrawParams.ChannelMask & (1 << ChannelIdx))
		{
			Schema->Channels[ChannelIdx]->DebugDraw(DrawParams, Reader);
		}
	}
}

static void DrawFeatureVector(const FDebugDrawParams& DrawParams, TArrayView<const float> PoseVector)
{
	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	check(Schema)

	if (PoseVector.Num() != Schema->Layout.NumFloats)
	{
		return;
	}

	FFeatureVectorReader Reader;
	Reader.Init(&Schema->Layout);
	Reader.SetValues(PoseVector);
	DrawFeatureVector(DrawParams, Reader);
}

static void DrawSearchIndex(const FDebugDrawParams& DrawParams)
{
	const UPoseSearchSchema* Schema = DrawParams.GetSchema();
	const FPoseSearchIndex* SearchIndex = DrawParams.GetSearchIndex();
	check(Schema);
	check(SearchIndex);

	FFeatureVectorReader Reader;
	Reader.Init(&Schema->Layout);

	const int32 LastPoseIdx = SearchIndex->NumPoses;

	TArray<float> PoseVector;
	for (int32 PoseIdx = 0; PoseIdx != LastPoseIdx; ++PoseIdx)
	{
		PoseVector = SearchIndex->GetPoseValues(PoseIdx);
		SearchIndex->InverseNormalize(PoseVector);
		Reader.SetValues(PoseVector);
		DrawFeatureVector(DrawParams, Reader);
	}
}

void Draw(const FDebugDrawParams& DebugDrawParams)
{
	if (DebugDrawParams.CanDraw())
	{
		if (DebugDrawParams.PoseIdx != INDEX_NONE)
		{
			const FPoseSearchIndex* SearchIndex = DebugDrawParams.GetSearchIndex();
			check(SearchIndex);

			TArray<float> PoseVector;
			PoseVector = SearchIndex->GetPoseValues(DebugDrawParams.PoseIdx);
			SearchIndex->InverseNormalize(PoseVector);
			DrawFeatureVector(DebugDrawParams, PoseVector);
		}
		if (!DebugDrawParams.PoseVector.IsEmpty())
		{
			DrawFeatureVector(DebugDrawParams, DebugDrawParams.PoseVector);
		}
		if (EnumHasAnyFlags(DebugDrawParams.Flags, EDebugDrawFlags::DrawSearchIndex))
		{
			DrawSearchIndex(DebugDrawParams);
		}
	}
}

static void PreprocessSearchIndexNone(FPoseSearchIndex* SearchIndex)
{
	// This function leaves the data unmodified and simply outputs the transformation
	// and inverse transformation matrices as the identity matrix and the sample mean
	// as the zero vector.

	using namespace Eigen;

	check(SearchIndex->IsValid() && !SearchIndex->IsEmpty());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(NumDimensions * NumPoses);
	Info.InverseTransformationMatrix.SetNumZeroed(NumDimensions * NumPoses);
	Info.SampleMean.SetNumZeroed(NumDimensions);

	// Map output transformation matrix
	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		NumDimensions, NumPoses
	);

	// Map output inverse transformation matrix
	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		NumDimensions, NumPoses
	);

	// Map output sample mean vector
	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), NumDimensions);

	// Write the transformation matrices and sample mean
	TransformMap = MatrixXf::Identity(NumDimensions, NumPoses);
	InverseTransformMap = MatrixXf::Identity(NumDimensions, NumPoses);
	SampleMeanMap = VectorXf::Zero(NumDimensions);
}

static inline Eigen::VectorXd ComputeFeatureMeanDeviations(const Eigen::MatrixXd& CenteredPoseMatrix, const FPoseSearchFeatureVectorLayout& Layout)
{
	using namespace Eigen;

	const int32 NumPoses = CenteredPoseMatrix.cols();
	const int32 NumDimensions = CenteredPoseMatrix.rows();

	VectorXd MeanDeviations(NumDimensions);
	MeanDeviations.setConstant(1.0);
	for (const FPoseSearchFeatureDesc& Feature : Layout.Features)
	{
		int32 FeatureDims = GetFeatureTypeTraits(Feature.Type).NumFloats;

		// Construct a submatrix for the feature and find the average distance to the feature's centroid.
		// Since we've already mean centered the data, the average distance to the centroid is simply the average norm.
		double FeatureMeanDeviation = CenteredPoseMatrix.block(Feature.ValueOffset, 0, FeatureDims, NumPoses).colwise().norm().mean();

		// Fill the feature's corresponding scaling axes with the average distance
		// Avoid scaling by zero by leaving near-zero deviations as 1.0
		if (FeatureMeanDeviation > KINDA_SMALL_NUMBER)
		{
			MeanDeviations.segment(Feature.ValueOffset, FeatureDims).setConstant(FeatureMeanDeviation);
		}
	}

	return MeanDeviations;
}

static void PreprocessSearchIndexNormalize(FPoseSearchIndex* SearchIndex)
{
	// This function performs a modified z-score normalization where features are normalized
	// by mean absolute deviation rather than standard deviation. Both methods are preferable
	// here to min-max scaling because they preserve outliers.
	// 
	// Mean absolute deviation is preferred here over standard deviation because the latter
	// emphasizes outliers since squaring the distance from the mean increases variance 
	// exponentially rather than additively and square rooting the sum of squares does not 
	// remove that bias. [1]
	//
	// The pose matrix is transformed in place and the transformation matrix, its inverse,
	// and data mean vector are computed and stored along with it.
	//
	// N:	number of dimensions for input column vectors
	// P:	number of input column vectors
	// X:	NxP input matrix
	// x_p:	pth column vector of input matrix
	// u:   mean column vector of X
	//
	// S:	mean absolute deviations of X, as diagonal NxN matrix with average distances replicated for each feature's axes
	// s_n:	nth deviation
	//
	// Normalization by mean absolute deviation algorithm:
	//
	// 1) mean-center X
	//    x_p := x_p - u
	// 2) rescale X by inverse mean absolute deviation
	//    x_p := x_p * s_n^(-1)
	// 
	// Let S^(-1) be the inverse of S where the nth diagonal element is s_n^(-1)
	// then step 2 can be expressed as matrix multiplication:
	// X := S^(-1) * X
	//
	// By persisting the mean vector u and linear transform S, we can bring an input vector q
	// into the same space as the mean centered and scaled data matrix X:
	// q := S^(-1) * (q - u)
	//
	// This operation is invertible, a normalized data vector x can be unscaled via:
	// x := (S * x) + u
	//
	// References:
	// [1] Gorard, S. (2005), "Revisiting a 90-Year-Old Debate: The Advantages of the Mean Deviation."
	//     British Journal of Educational Studies, 53: 417-430.

	using namespace Eigen;

	check(SearchIndex->IsValid() && !SearchIndex->IsEmpty());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	// Map input buffer
	auto PoseMatrixSourceMap = Map<Matrix<float, Dynamic, Dynamic, RowMajor>>(
		SearchIndex->Values.GetData(),
		NumPoses,		// rows
		NumDimensions	// cols
	);

	// Copy row major float matrix to column major double matrix
	MatrixXd PoseMatrix = PoseMatrixSourceMap.transpose().cast<double>();
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

#if UE_POSE_SEARCH_EIGEN_DEBUG
	MatrixXd PoseMatrixOriginal = PoseMatrix;
#endif

	// Mean center
	VectorXd SampleMean = PoseMatrix.rowwise().mean();
	PoseMatrix = PoseMatrix.colwise() - SampleMean;

	// Compute per-feature average distances
	VectorXd MeanDeviations = ComputeFeatureMeanDeviations(PoseMatrix, Layout);

	// Construct a scaling matrix that uniformly scales each feature by its average distance from the mean
	MatrixXd ScalingMatrix = MeanDeviations.cwiseInverse().asDiagonal();

	// Construct the inverse scaling matrix
	MatrixXd InverseScalingMatrix = MeanDeviations.asDiagonal();

	// Rescale data by transforming it with the scaling matrix
	// Now each feature has an average Euclidean length = 1.
	PoseMatrix = ScalingMatrix * PoseMatrix;

	// Write normalized data back to source buffer, converting from column data back to row data
	PoseMatrixSourceMap = PoseMatrix.transpose().cast<float>();

	// Output preprocessing info
	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(ScalingMatrix.size());
	Info.InverseTransformationMatrix.SetNumZeroed(InverseScalingMatrix.size());
	Info.SampleMean.SetNumZeroed(SampleMean.size());

	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		ScalingMatrix.rows(), ScalingMatrix.cols()
	);

	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		InverseScalingMatrix.rows(), InverseScalingMatrix.cols()
	);

	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), SampleMean.size());

	// Output scaling matrix, inverse scaling matrix, and mean vector
	TransformMap = ScalingMatrix.cast<float>();
	InverseTransformMap = InverseScalingMatrix.cast<float>();
	SampleMeanMap = SampleMean.cast<float>();

#if UE_POSE_SEARCH_EIGEN_DEBUG
	FString MeanDevationsStr = EigenMatrixToString(MeanDeviations);
	FString PoseMtxOriginalStr = EigenMatrixToString(PoseMatrixOriginal);
	FString PoseMtxStr = EigenMatrixToString(PoseMatrix);
	FString TransformationStr = EigenMatrixToString(TransformMap);
	FString InverseTransformationStr = EigenMatrixToString(InverseTransformMap);
	FString SampleMeanStr = EigenMatrixToString(SampleMeanMap);
#endif // UE_POSE_SEARCH_EIGEN_DEBUG
}

static void PreprocessSearchIndexSphere(FPoseSearchIndex* SearchIndex)
{
	// This function performs correlation based zero-phase component analysis sphering (ZCA-cor sphering)
	// The pose matrix is transformed in place and the transformation matrix, its inverse,
	// and data mean vector are computed and stored along with it.
	//
	// N:	number of dimensions for input column vectors
	// P:	number of input column vectors
	// X:	NxP input matrix
	// x_p:	pth column vector of input matrix
	// u:   mean column vector of X
	//
	// Eigendecomposition of correlation matrix of X:
	// cor(X) = (1/P) * X * X^T = V * D * V^T
	//
	// V:	eigenvectors of cor(X), stacked as columns in an orthogonal NxN matrix
	// D:	eigenvalues of cor(X), as diagonal NxN matrix
	// d_n:	nth eigenvalue
	// s_n: nth standard deviation
	// s_n^2 = d_n, the variance along the nth eigenvector
	// s_n   = d_n^(1/2)
	//
	// ZCA sphering algorithm:
	//
	// 1) mean-center X
	//    x_p := x_p - u
	// 2) align largest orthogonal directions of variance in X to coordinate axes (PCA rotate)
	//    x_p := V^T * x_p
	// 3) rescale X by inverse standard deviation
	//    x_p := x_p * d_n^(-1/2)
	// 4) return now rescaled X back to original rotation (inverse PCA rotate)
	//    x_p := V * x_p
	// 
	// Let D^(-1/2) be the inverse square root of D where the nth diagonal element is d_n^(-1/2)
	// then steps 2-4 can be expressed as a series of matrix multiplications:
	// Z = V * D^(-1/2) * V^T
	// X := Z * X
	//
	// By persisting the mean vector u and linear transform Z, we can bring an input vector q
	// into the same space as the sphered data matrix X:
	// q := Z * (q - u)
	//
	// This operation is invertible, a sphere standardized data vector x can be unscaled via:
	// Z^(-1) = V * D^(1/2) * V^T
	// x := (Z^(-1) * x) + u
	//
	// The sphering process allows nearest neighbor queries to use the Mahalanobis metric
	// which is unitless, scale-invariant, and accounts for feature correlation.
	// The Mahalanobis distance between two random vectors x and y in data matrix X is:
	// d(x,y) = ((x-y)^T * cov(X)^(-1) * (x-y))^(1/2)
	//
	// Because sphering transforms X into a new matrix with identity covariance, the Mahalanobis
	// distance equation above reduces to Euclidean distance since cov(X)^(-1) = I:
	// d(x,y) = ((x-y)^T * (x-y))^(1/2)
	// 
	// References:
	// Watt, Jeremy, et al. Machine Learning Refined: Foundations, Algorithms, and Applications.
	// 2nd ed., Cambridge University Press, 2020.
	// 
	// Kessy, Agnan, Alex Lewin, and Korbinian Strimmer. "Optimal whitening and decorrelation."
	// The American Statistician 72.4 (2018): 309-314.
	// 
	// https://en.wikipedia.org/wiki/Whitening_transformation
	// 
	// https://en.wikipedia.org/wiki/Mahalanobis_distance
	//
	// Note this sphering preprocessor needs more work and isn't yet exposed in the editor as an option.
	// Todo:
	// - Figure out apparent flipping behavior
	// - Try singular value decomposition in place of eigendecomposition
	// - Remove zero variance feature axes from data and search queries
	// - Support weighted Mahalanobis metric. User supplied weights need to be transformed to data's new basis.

#if UE_POSE_SEARCH_EIGEN_DEBUG
	double StartTime = FPlatformTime::Seconds();
#endif

	using namespace Eigen;

	check(SearchIndex->IsValid() && !SearchIndex->IsEmpty());

	FPoseSearchIndexPreprocessInfo& Info = SearchIndex->PreprocessInfo;
	Info.Reset();

	const FPoseSearchFeatureVectorLayout& Layout = SearchIndex->Schema->Layout;

	const int32 NumPoses = SearchIndex->NumPoses;
	const int32 NumDimensions = Layout.NumFloats;

	// Map input buffer
	auto PoseMatrixSourceMap = Map<Matrix<float, Dynamic, Dynamic, RowMajor>>(
		SearchIndex->Values.GetData(),
		NumPoses,		// rows
		NumDimensions	// cols
	);

	// Copy row major float matrix to column major double matrix
	MatrixXd PoseMatrix = PoseMatrixSourceMap.transpose().cast<double>();
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

#if UE_POSE_SEARCH_EIGEN_DEBUG
	MatrixXd PoseMatrixOriginal = PoseMatrix;
#endif

	// Mean center
	VectorXd SampleMean = PoseMatrix.rowwise().mean();
	PoseMatrix = PoseMatrix.colwise() - SampleMean;

	// Compute per-feature average distances
	VectorXd MeanDeviations = ComputeFeatureMeanDeviations(PoseMatrix, Layout);

	// Rescale data by transforming it with the scaling matrix
	// Now each feature has an average Euclidean length = 1.
	MatrixXd PoseMatrixNormalized = MeanDeviations.cwiseInverse().asDiagonal() * PoseMatrix;

	// Compute sample covariance
	MatrixXd Covariance = ((1.0 / NumPoses) * (PoseMatrixNormalized * PoseMatrixNormalized.transpose())) + 1e-7 * MatrixXd::Identity(NumDimensions, NumDimensions);

	VectorXd StdDev = Covariance.diagonal().cwiseSqrt();
	VectorXd InvStdDev = StdDev.cwiseInverse();
	MatrixXd Correlation = InvStdDev.asDiagonal() * Covariance * InvStdDev.asDiagonal();

	// Compute eigenvalues and eigenvectors of correlation matrix
	SelfAdjointEigenSolver<MatrixXd> EigenDecomposition(Correlation, ComputeEigenvectors);

	VectorXd EigenValues = EigenDecomposition.eigenvalues();
	MatrixXd EigenVectors = EigenDecomposition.eigenvectors();

	// Sort eigenpairs by descending eigenvalue
	{
		const Eigen::Index n = EigenValues.size();
		for (Eigen::Index i = 0; i < n-1; ++i)
		{
			Index k;
			EigenValues.segment(i,n-i).cwiseAbs().maxCoeff(&k);
			if (k > 0)
			{
				std::swap(EigenValues[i], EigenValues[k+i]);
				EigenVectors.col(i).swap(EigenVectors.col(k+i));
			}
		}
	}

	// Regularize eigenvalues
	EigenValues = EigenValues.array() + 1e-7;

	// Compute ZCA-cor and ZCA-cor^(-1)
	MatrixXd ZCA = EigenVectors * EigenValues.cwiseInverse().cwiseSqrt().asDiagonal() * EigenVectors.transpose() * MeanDeviations.cwiseInverse().asDiagonal();
	MatrixXd ZCAInverse = MeanDeviations.asDiagonal() * EigenVectors * EigenValues.cwiseSqrt().asDiagonal() * EigenVectors.transpose();

	// Apply sphering transform to the data matrix
	PoseMatrix = ZCA * PoseMatrix;
	checkSlow(PoseMatrix.rows() == NumDimensions);
	checkSlow(PoseMatrix.cols() == NumPoses);

	// Write data back to source buffer, converting from column data back to row data
	PoseMatrixSourceMap = PoseMatrix.transpose().cast<float>();

	// Output preprocessing info
	Info.NumDimensions = NumDimensions;
	Info.TransformationMatrix.SetNumZeroed(ZCA.size());
	Info.InverseTransformationMatrix.SetNumZeroed(ZCAInverse.size());
	Info.SampleMean.SetNumZeroed(SampleMean.size());

	auto TransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.TransformationMatrix.GetData(),
		ZCA.rows(), ZCA.cols()
	);

	auto InverseTransformMap = Map<Matrix<float, Dynamic, Dynamic, ColMajor>>(
		Info.InverseTransformationMatrix.GetData(),
		ZCAInverse.rows(), ZCAInverse.cols()
	);

	auto SampleMeanMap = Map<VectorXf>(Info.SampleMean.GetData(), SampleMean.size());

	// Output sphering matrix, inverse sphering matrix, and mean vector
	TransformMap = ZCA.cast<float>();
	InverseTransformMap = ZCAInverse.cast<float>();
	SampleMeanMap = SampleMean.cast<float>();

#if UE_POSE_SEARCH_EIGEN_DEBUG
	double ElapsedTime = FPlatformTime::Seconds() - StartTime;

	FString EigenValuesStr = EigenMatrixToString(EigenValues);
	FString EigenVectorsStr = EigenMatrixToString(EigenVectors);

	FString CovarianceStr = EigenMatrixToString(Covariance);
	FString CorrelationStr = EigenMatrixToString(Correlation);

	FString ZCAStr = EigenMatrixToString(ZCA);
	FString ZCAInverseStr = EigenMatrixToString(ZCAInverse);

	FString PoseMatrixSphereStr = EigenMatrixToString(PoseMatrix);
	MatrixXd PoseMatrixUnsphered = ZCAInverse * PoseMatrix;
	PoseMatrixUnsphered = PoseMatrixUnsphered.colwise() + SampleMean;
	FString PoseMatrixUnspheredStr = EigenMatrixToString(PoseMatrixUnsphered);
	FString PoseMatrixOriginalStr = EigenMatrixToString(PoseMatrixOriginal);

	FString OutputPoseMatrixStr = EigenMatrixToString(PoseMatrixSourceMap);

	FString TransformStr = EigenMatrixToString(TransformMap);
	FString InverseTransformStr = EigenMatrixToString(InverseTransformMap);
	FString SampleMeanStr = EigenMatrixToString(SampleMeanMap);
#endif // UE_POSE_SEARCH_EIGEN_DEBUG
}

static void PreprocessSearchIndex(FPoseSearchIndex* SearchIndex)
{
	switch (SearchIndex->Schema->EffectiveDataPreprocessor)
	{
		case EPoseSearchDataPreprocessor::Normalize:
			PreprocessSearchIndexNormalize(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::Sphere:
			PreprocessSearchIndexSphere(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::None:
			PreprocessSearchIndexNone(SearchIndex);
		break;

		case EPoseSearchDataPreprocessor::Invalid:
			checkNoEntry();
		break;
	}
}

static void PreprocessGroupSearchIndexWeights(FGroupSearchIndex& GroupSearchIndex, const UPoseSearchDatabase* Database)
{
	const FPoseSearchWeightParams& WeightParams = GroupSearchIndex.GroupIndex == INDEX_NONE ? Database->DefaultWeights : Database->Groups[GroupSearchIndex.GroupIndex].Weights;
	FPoseSearchWeights Weights;
	Weights.Init(WeightParams, Database->Schema);
	GroupSearchIndex.Weights = Weights.Weights;
}

// it calcualtes Mean and PCAProjectionMatrix
static void PreprocessGroupSearchIndexPCAData(FGroupSearchIndex& GroupSearchIndex, const UPoseSearchDatabase* Database, const float* GroupValues, float* GroupPCAValues)
{
	// binding SearchIndex.Values and SearchIndex.PCAValues Eigen row major matrix maps
	const int32 NumDimensions = Database->Schema->Layout.NumFloats;
	const int32 NumGroupPoses = GroupSearchIndex.EndPoseIndex - GroupSearchIndex.StartPoseIndex;

	const RowMajorVectorMapConst MapWeights(GroupSearchIndex.Weights.GetData(), 1, NumDimensions);
	const RowMajorMatrixMapConst MapGroupValues(GroupValues, NumGroupPoses, NumDimensions);
	const RowMajorMatrix WeightedGroupValues = MapGroupValues.array().rowwise() * MapWeights.array();
	const uint32 NumberOfPrincipalComponents = Database->GetNumberOfPrincipalComponents();
	RowMajorMatrixMap MapGroupPCAValues(GroupPCAValues, NumGroupPoses, NumberOfPrincipalComponents);

	// calculating the mean
	GroupSearchIndex.Mean.SetNumZeroed(NumDimensions);
	RowMajorVectorMap Mean(GroupSearchIndex.Mean.GetData(), 1, NumDimensions);
	Mean = WeightedGroupValues.colwise().mean();

	// use the mean to center the data points
	const RowMajorMatrix CenteredGroupValues = WeightedGroupValues.rowwise() - Mean;


	// estimating the covariance matrix (with dimensionality of NumDimensions, NumDimensions)
	// formula: https://en.wikipedia.org/wiki/Covariance_matrix#Estimation
	// details: https://en.wikipedia.org/wiki/Estimation_of_covariance_matrices
	const ColMajorMatrix CovariantMatrix = (CenteredGroupValues.transpose() * CenteredGroupValues) / float(NumGroupPoses - 1);
	const Eigen::SelfAdjointEigenSolver<ColMajorMatrix> EigenSolver(CovariantMatrix);

	check(EigenSolver.info() == Eigen::Success);

	// validating EigenSolver results
	const ColMajorMatrix EigenVectors = EigenSolver.eigenvectors().real();

	if (Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate && NumberOfPrincipalComponents == NumDimensions)
	{
		const RowMajorVector ReciprocalWeights = MapWeights.cwiseInverse();
		const RowMajorMatrix ProjectedGroupValues = CenteredGroupValues * EigenVectors;
		for (Eigen::Index RowIndex = 0; RowIndex < MapGroupValues.rows(); ++RowIndex)
		{
			const RowMajorVector WeightedReconstructedPoint = ProjectedGroupValues.row(RowIndex) * EigenVectors.transpose() + Mean;
			const RowMajorVector ReconstructedPoint = WeightedReconstructedPoint.array() * ReciprocalWeights.array();
			const float Error = (ReconstructedPoint - MapGroupValues.row(RowIndex)).squaredNorm();
			check(Error < UE_KINDA_SMALL_NUMBER);
		}
	}

	// sorting EigenVectors by EigenValues, so we pick the most significant ones to compose our PCA projection matrix.
	const RowMajorVector EigenValues = EigenSolver.eigenvalues().real();
	TArray<size_t> Indexer;
	Indexer.Reserve(NumDimensions);
	for (size_t DimensionIndex = 0; DimensionIndex < NumDimensions; ++DimensionIndex)
	{
		Indexer.Push(DimensionIndex);
	}
	Indexer.Sort([&EigenValues](size_t a, size_t b)
	{
		return EigenValues[a] > EigenValues[b];
	});

	// composing the PCA projection matrix with the PCANumComponents most significant EigenVectors
	GroupSearchIndex.PCAProjectionMatrix.SetNumZeroed(NumDimensions * NumberOfPrincipalComponents);
	ColMajorMatrixMap PCAProjectionMatrix(GroupSearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);
	float AccumulatedVariance = 0.f;
	for (size_t PCAComponentIndex = 0; PCAComponentIndex < NumberOfPrincipalComponents; ++PCAComponentIndex)
	{
		PCAProjectionMatrix.col(PCAComponentIndex) = EigenVectors.col(Indexer[PCAComponentIndex]);
		AccumulatedVariance += EigenValues[Indexer[PCAComponentIndex]];
	}

	// calculating the total variance knowing that eigen values measure variance along the principal components:
	const float TotalVariance = EigenValues.sum();
	// and explained variance as ratio between AccumulatedVariance and TotalVariance: https://ro-che.info/articles/2017-12-11-pca-explained-variance
	const float ExplainedVariance = TotalVariance > UE_KINDA_SMALL_NUMBER ? AccumulatedVariance / TotalVariance : 0.f;

	MapGroupPCAValues = CenteredGroupValues * PCAProjectionMatrix;

	if (Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate && NumberOfPrincipalComponents == NumDimensions)
	{
		const RowMajorVector ReciprocalWeights = MapWeights.cwiseInverse();
		for (Eigen::Index RowIndex = 0; RowIndex < MapGroupValues.rows(); ++RowIndex)
		{
			const RowMajorVector WeightedReconstructedValues = MapGroupPCAValues.row(RowIndex) * PCAProjectionMatrix.transpose() + Mean;
			const RowMajorVector ReconstructedValues = WeightedReconstructedValues.array() * ReciprocalWeights.array();
			const float Error = (ReconstructedValues - MapGroupValues.row(RowIndex)).squaredNorm();
			check(Error < UE_KINDA_SMALL_NUMBER);
		}
	}
}

static void PreprocessGroupSearchIndexKDTree(FGroupSearchIndex& GroupSearchIndex, const UPoseSearchDatabase* Database, const float* GroupValues, const float* GroupPCAValues)
{
	const int32 NumGroupPoses = GroupSearchIndex.EndPoseIndex - GroupSearchIndex.StartPoseIndex;
	const uint32 NumberOfPrincipalComponents = Database->GetNumberOfPrincipalComponents();
	GroupSearchIndex.KDTree.Construct(NumGroupPoses, NumberOfPrincipalComponents, GroupPCAValues, Database->KDTreeMaxLeafSize);

	if (Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate)
	{
		// testing the KDTree is returning the proper searches for all the points in pca space
		for (size_t PointIndex = 0; PointIndex < NumGroupPoses; ++PointIndex)
		{
			constexpr size_t NumResults = 10;
			size_t ResultIndexes[NumResults + 1] = { 0 };
			float ResultDistanceSqr[NumResults + 1] = { 0.0f };
			FKDTree::KNNResultSet ResultSet(NumResults, ResultIndexes, ResultDistanceSqr);
			GroupSearchIndex.KDTree.FindNeighbors(ResultSet, &GroupPCAValues[PointIndex * NumberOfPrincipalComponents]);

			size_t ResultIndex = 0;
			for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
			{
				if (PointIndex == ResultIndexes[ResultIndex])
				{
					check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
					break;
				}
			}
			check(ResultIndex < ResultSet.Num());
		}

		// testing the KDTree is returning the proper searches for all the original points transformed in pca space
		const int32 NumDimensions = Database->Schema->Layout.NumFloats;
		for (size_t PointIndex = 0; PointIndex < NumGroupPoses; ++PointIndex)
		{
			constexpr size_t NumResults = 10;
			size_t ResultIndexes[NumResults + 1] = { 0 };
			float ResultDistanceSqr[NumResults + 1] = { 0.0f };
			FKDTree::KNNResultSet ResultSet(NumResults, ResultIndexes, ResultDistanceSqr);

			const RowMajorVectorMapConst MapGroupValues(&GroupValues[PointIndex * NumDimensions], 1, NumDimensions);
			const RowMajorVectorMapConst MapWeights(GroupSearchIndex.Weights.GetData(), 1, NumDimensions);
			const RowMajorVectorMapConst Mean(GroupSearchIndex.Mean.GetData(), 1, NumDimensions);
			const ColMajorMatrixMapConst PCAProjectionMatrix(GroupSearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);

			const RowMajorMatrix WeightedGroupValues = MapGroupValues.array() * MapWeights.array();
			const RowMajorMatrix CenteredGroupValues = WeightedGroupValues - Mean;
			const RowMajorVector ProjectedGroupValues  = CenteredGroupValues * PCAProjectionMatrix;

			GroupSearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedGroupValues.data());

			size_t ResultIndex = 0;
			for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
			{
				if (PointIndex == ResultIndexes[ResultIndex])
				{
					check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
					break;
				}
			}
			check(ResultIndex < ResultSet.Num());
		}
	}
}

static void PreprocessGroupSearchIndex(FPoseSearchIndex& SearchIndex, const UPoseSearchDatabase* Database)
{
	const uint32 NumberOfPrincipalComponents = Database->GetNumberOfPrincipalComponents();
	if (NumberOfPrincipalComponents > 0)
	{
		const int32 NumDimensions = Database->Schema->Layout.NumFloats;

		// preallocating the PCAValues for all the groups
		SearchIndex.PCAValues.Reset();
		SearchIndex.PCAValues.AddZeroed(SearchIndex.NumPoses * NumberOfPrincipalComponents);
		
		// original serial for loop: 
		// for (int32 i = 0; i != SearchIndex.Groups.Num(); ++i)
		ParallelFor(SearchIndex.Groups.Num(), [&](int32 i)
		{
			FGroupSearchIndex& GroupSearchIndex = SearchIndex.Groups[i];
			
			const float* GroupValues = SearchIndex.Values.GetData() + GroupSearchIndex.StartPoseIndex * NumDimensions;
			float* GroupPCAValues = SearchIndex.PCAValues.GetData() + GroupSearchIndex.StartPoseIndex * NumberOfPrincipalComponents;

			PreprocessGroupSearchIndexWeights(GroupSearchIndex, Database);
			PreprocessGroupSearchIndexPCAData(GroupSearchIndex, Database, GroupValues, GroupPCAValues);
			PreprocessGroupSearchIndexKDTree(GroupSearchIndex, Database, GroupValues, GroupPCAValues);
		});
	}
	else
	{
		// we don't need groups
		SearchIndex.Groups.Reset();
	}
}

bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData)
{
	check(Sequence);
	check(SequenceMetaData);

	if (!SequenceMetaData->IsValidForIndexing())
	{
		return false;
	}

	USkeleton* SeqSkeleton = Sequence->GetSkeleton();
	if (!SeqSkeleton || !SeqSkeleton->IsCompatible(SequenceMetaData->Schema->Skeleton))
	{
		return false;
	}

	FAssetSamplingContext SamplingContext;
	SamplingContext.Init(SequenceMetaData->Schema);

	FSequenceSampler Sampler;
	FSequenceSampler::FInput SamplerInput;
	SamplerInput.ExtrapolationParameters = SequenceMetaData->ExtrapolationParameters;
	SamplerInput.Sequence = Sequence;
	SamplerInput.bLoopable = false;
	Sampler.Init(SamplerInput);
	Sampler.Process();

	FAssetIndexer Indexer;
	FAssetIndexingContext IndexerContext;
	IndexerContext.SamplingContext = &SamplingContext;
	IndexerContext.MainSampler = &Sampler;
	IndexerContext.Schema = SequenceMetaData->Schema;
	IndexerContext.RequestedSamplingRange = GetEffectiveSamplingRange(Sequence, SequenceMetaData->SamplingRange);
	Indexer.Init(IndexerContext);
	if (!Indexer.Process())
	{
		return false;
	}

	SequenceMetaData->SearchIndex.Assets.Empty();
	FPoseSearchIndexAsset SearchIndexAsset;
	SearchIndexAsset.SourceAssetIdx = 0;
	SearchIndexAsset.FirstPoseIdx = 0;
	SearchIndexAsset.NumPoses = Indexer.Output.NumIndexedPoses;
	SearchIndexAsset.SamplingInterval = IndexerContext.RequestedSamplingRange;

	SequenceMetaData->SearchIndex.Values = Indexer.Output.FeatureVectorTable;
	SequenceMetaData->SearchIndex.NumPoses = Indexer.Output.NumIndexedPoses;
	SequenceMetaData->SearchIndex.Schema = SequenceMetaData->Schema;
	SequenceMetaData->SearchIndex.Assets.Add(SearchIndexAsset);
	SequenceMetaData->SearchIndex.PoseMetadata = Indexer.Output.PoseMetadata;

	PreprocessSearchIndex(&SequenceMetaData->SearchIndex);

	return true;
}

struct FDatabaseIndexingContext
{
	UPoseSearchDatabase* Database = nullptr;
	FPoseSearchIndex* SearchIndex = nullptr;

	FAssetSamplingContext SamplingContext;
	TArray<FSequenceSampler> SequenceSamplers;
	TMap<const UAnimSequence*, int32> SequenceSamplerMap;
	TArray<FBlendSpaceSampler> BlendSpaceSamplers;
	TMap<TPair<const UBlendSpace*, FVector>, int32> BlendSpaceSamplerMap;

	TArray<FAssetIndexer> Indexers;

	void PrepareSamplers();
	void PrepareIndexers();
	bool IndexAssets();
	void JoinIndex();
};

void FDatabaseIndexingContext::PrepareSamplers()
{
	SamplingContext.Init(Database->Schema);

	// Prepare samplers for all sequences

	auto AddSequenceSampler = [&](const UAnimSequence* Sequence, bool bLoopable)
	{
		if (!SequenceSamplerMap.Contains(Sequence))
		{
			int32 SequenceSamplerIdx = SequenceSamplers.AddDefaulted();
			SequenceSamplerMap.Add(Sequence, SequenceSamplerIdx);

			FSequenceSampler::FInput Input;
			Input.ExtrapolationParameters = Database->ExtrapolationParameters;
			Input.Sequence = Sequence;
			Input.bLoopable = bLoopable;
			SequenceSamplers[SequenceSamplerIdx].Init(Input);
		}
	};

	for (const FPoseSearchDatabaseSequence& DbSequence : Database->Sequences)
	{
		if (DbSequence.Sequence)
		{
			AddSequenceSampler(DbSequence.Sequence, DbSequence.bLoopAnimation);
		}

		if (DbSequence.LeadInSequence)
		{
			AddSequenceSampler(DbSequence.LeadInSequence, DbSequence.bLoopLeadInAnimation);
		}

		if (DbSequence.FollowUpSequence)
		{
			AddSequenceSampler(DbSequence.FollowUpSequence, DbSequence.bLoopFollowUpAnimation);
		}
	}

	ParallelFor(
		SequenceSamplers.Num(),
		[this](int32 SamplerIdx) { SequenceSamplers[SamplerIdx].Process(); },
		ParallelForFlags
	);

	// Prepare samplers for all blend spaces

	for (const FPoseSearchDatabaseBlendSpace& DbBlendSpace : Database->BlendSpaces)
	{
		if (DbBlendSpace.BlendSpace)
		{
			int32 HorizontalBlendNum, VerticalBlendNum;
			float HorizontalBlendMin, HorizontalBlendMax, VerticalBlendMin, VerticalBlendMax;

			DbBlendSpace.GetBlendSpaceParameterSampleRanges(
				HorizontalBlendNum,
				VerticalBlendNum,
				HorizontalBlendMin,
				HorizontalBlendMax,
				VerticalBlendMin,
				VerticalBlendMax);

			for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalBlendNum; HorizontalIndex++)
			{
				for (int32 VerticalIndex = 0; VerticalIndex < VerticalBlendNum; VerticalIndex++)
				{
					FVector BlendParameters = BlendParameterForSampleRanges(
						HorizontalIndex,
						VerticalIndex,
						HorizontalBlendNum,
						VerticalBlendNum,
						HorizontalBlendMin,
						HorizontalBlendMax,
						VerticalBlendMin,
						VerticalBlendMax);

					if (!BlendSpaceSamplerMap.Contains({ DbBlendSpace.BlendSpace, BlendParameters }))
					{
						int32 BlendSpaceSamplerIdx = BlendSpaceSamplers.AddDefaulted();
						BlendSpaceSamplerMap.Add({ DbBlendSpace.BlendSpace, BlendParameters }, BlendSpaceSamplerIdx);

						FBlendSpaceSampler::FInput Input;
						Input.SamplingContext = &SamplingContext;
						Input.ExtrapolationParameters = Database->ExtrapolationParameters;
						Input.BlendSpace = DbBlendSpace.BlendSpace;
						Input.bLoopable = DbBlendSpace.bLoopAnimation;
						Input.BlendParameters = BlendParameters;

						BlendSpaceSamplers[BlendSpaceSamplerIdx].Init(Input);
					}
				}
			}
		}
	}

	ParallelFor(
		BlendSpaceSamplers.Num(),
		[this](int32 SamplerIdx) { BlendSpaceSamplers[SamplerIdx].Process(); },
		ParallelForFlags
	);
}

void FDatabaseIndexingContext::PrepareIndexers()
{
	Indexers.Reserve(Database->GetSearchIndex()->Assets.Num());

	auto GetSequenceSampler = [&](const UAnimSequence* Sequence) -> const FSequenceSampler*
	{
		return Sequence ? &SequenceSamplers[SequenceSamplerMap[Sequence]] : nullptr;
	};

	auto GetBlendSpaceSampler = [&](const UBlendSpace* BlendSpace, const FVector BlendParameters) -> const FBlendSpaceSampler*
	{
		return BlendSpace ? &BlendSpaceSamplers[BlendSpaceSamplerMap[{BlendSpace, BlendParameters}]] : nullptr;
	};
	Indexers.Reserve(Database->GetSearchIndex()->Assets.Num());
	for (int32 AssetIdx = 0; AssetIdx != SearchIndex->Assets.Num(); ++AssetIdx)
	{
		const FPoseSearchIndexAsset& SearchIndexAsset = SearchIndex->Assets[AssetIdx];

		FAssetIndexingContext IndexerContext;
		IndexerContext.SamplingContext = &SamplingContext;
		IndexerContext.Schema = Database->Schema;
		IndexerContext.BlockTransitionParameters = Database->BlockTransitionParameters;
		IndexerContext.RequestedSamplingRange = SearchIndexAsset.SamplingInterval;
		IndexerContext.bMirrored = SearchIndexAsset.bMirrored;

		if (SearchIndexAsset.Type == ESearchIndexAssetType::Sequence)
		{
			const FPoseSearchDatabaseSequence& DbSequence = Database->GetSequenceSourceAsset(&SearchIndexAsset);
			const float SequenceLength = DbSequence.Sequence->GetPlayLength();
			IndexerContext.MainSampler = GetSequenceSampler(DbSequence.Sequence);
			IndexerContext.LeadInSampler =
				SearchIndexAsset.SamplingInterval.Min == 0.0f ? GetSequenceSampler(DbSequence.LeadInSequence) : nullptr;
			IndexerContext.FollowUpSampler =
				SearchIndexAsset.SamplingInterval.Max == SequenceLength ? GetSequenceSampler(DbSequence.FollowUpSequence) : nullptr;
		}
		else if (SearchIndexAsset.Type == ESearchIndexAssetType::BlendSpace)
		{
			const FPoseSearchDatabaseBlendSpace& DbBlendSpace = Database->GetBlendSpaceSourceAsset(&SearchIndexAsset);
			IndexerContext.MainSampler = GetBlendSpaceSampler(DbBlendSpace.BlendSpace, SearchIndexAsset.BlendParameters);
		}
		else
		{
			checkNoEntry();
		}

		FAssetIndexer& Indexer = Indexers.AddDefaulted_GetRef();
		Indexer.Init(IndexerContext);
	}
}

bool FDatabaseIndexingContext::IndexAssets()
{
	bool bSuccess = true;

	// Index sequence data
	ParallelFor(
		Indexers.Num(),
		[this](int32 SequenceIdx) { Indexers[SequenceIdx].Process(); },
		ParallelForFlags
	);

	// Validate indexing process
	TSet<int32> AllInvalidChannels;
	TBitArray<> AllInvalidFeatures;
	for (FAssetIndexer& Indexer : Indexers)
	{
		AllInvalidChannels.Union(Indexer.Output.InvalidChannels);
		TBitArray<>::BitwiseOR(AllInvalidFeatures, Indexer.Output.AllFeaturesNotAdded, EBitwiseOperatorFlags::MaxSize);
	}

	if (!AllInvalidChannels.IsEmpty())
	{
		bSuccess = false;
	}

	for (int32 InvalidChannelIdx : AllInvalidChannels)
	{
		const TObjectPtr<UPoseSearchFeatureChannel> ChannelPtr = Database->Schema->Channels[InvalidChannelIdx];
		if (ChannelPtr)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("Feature channel '%f' failed to index all features from all assets"), *ChannelPtr->GetName());
		}
	}

	return bSuccess;
}

void FDatabaseIndexingContext::JoinIndex()
{
	// Write index info to asset and count up total poses and storage required
	int32 TotalPoses = 0;
	int32 TotalFloats = 0;
	
	SearchIndex->Groups.Reset();

	if (SearchIndex->Assets.Num() > 0)
	{
		SearchIndex->Groups.AddDefaulted();
		SearchIndex->Groups.Last().GroupIndex = SearchIndex->Assets[0].SourceGroupIdx;

		for (int32 AssetIdx = 0; AssetIdx != SearchIndex->Assets.Num(); ++AssetIdx)
		{
			const FAssetIndexer::FOutput& Output = Indexers[AssetIdx].Output;

			FPoseSearchIndexAsset& SearchIndexAsset = SearchIndex->Assets[AssetIdx];
			
			if (SearchIndexAsset.SourceGroupIdx != SearchIndex->Groups.Last().GroupIndex)
			{
				// making sure groups are sorted correctly in ascending order
				check(SearchIndexAsset.SourceGroupIdx > SearchIndex->Groups.Last().GroupIndex);
				// finalizing the previous group before adding a new one
				SearchIndex->Groups.Last().EndPoseIndex = TotalPoses;

				SearchIndex->Groups.AddDefaulted();
				SearchIndex->Groups.Last().GroupIndex = SearchIndexAsset.SourceGroupIdx;
				SearchIndex->Groups.Last().StartPoseIndex = TotalPoses;
			}
			
			SearchIndexAsset.NumPoses = Output.NumIndexedPoses;
			SearchIndexAsset.FirstPoseIdx = TotalPoses;

			TotalPoses += Output.NumIndexedPoses;
			TotalFloats += Output.FeatureVectorTable.Num();
		}
		
		// finalizing the last inserted group
		SearchIndex->Groups.Last().EndPoseIndex = TotalPoses;
	}
	
	check(TotalFloats == TotalPoses * Database->Schema->Layout.NumFloats);
	
	// Join animation data into a single search index
	SearchIndex->Values.Reset(TotalFloats);
	SearchIndex->PoseMetadata.Reset(TotalPoses);
	SearchIndex->PCAValues.Reset();
	
	for (const FAssetIndexer& Indexer : Indexers)
	{
		const FAssetIndexer::FOutput& Output = Indexer.Output;
		SearchIndex->Values.Append(Output.FeatureVectorTable.GetData(), Output.FeatureVectorTable.Num());
		SearchIndex->PoseMetadata.Append(Output.PoseMetadata);
	}

	SearchIndex->NumPoses = TotalPoses;
	SearchIndex->Schema = Database->Schema;
}

bool BuildIndex(UPoseSearchDatabase* Database, FPoseSearchIndex& OutSearchIndex)
{
	check(Database);

	bool bSuccess = true;

	if (!Database->IsValidForIndexing())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("Database '%f' is invalid for indexing"), *Database->GetName());
		return false;
	}

	OutSearchIndex.Schema = Database->Schema;

	if (!Database->TryInitSearchIndexAssets(OutSearchIndex))
	{
		return false;
	}

	FDatabaseIndexingContext DbIndexingContext;
	DbIndexingContext.Database = Database;
	DbIndexingContext.SearchIndex = &OutSearchIndex;

	DbIndexingContext.PrepareSamplers();
	DbIndexingContext.PrepareIndexers();
	bSuccess &= DbIndexingContext.IndexAssets();
	DbIndexingContext.JoinIndex();

	PreprocessSearchIndex(&OutSearchIndex);

	PreprocessGroupSearchIndex(OutSearchIndex, Database);
	
	return bSuccess;
}

bool FQueryBuildingContext::IsInitialized() const
{
	return Schema && Schema->IsValid() && Query.IsInitializedForSchema(Schema);
}

bool BuildQuery(FQueryBuildingContext& QueryBuildingContext)
{
	if (!QueryBuildingContext.IsInitialized())
	{
		return false;
	}

	bool bSuccess = true;
	for (const TObjectPtr<UPoseSearchFeatureChannel>& Channel : QueryBuildingContext.Schema->Channels)
	{
		bool bChannelSuccess = Channel->BuildQuery(QueryBuildingContext);
		bSuccess &= bChannelSuccess;
	}

	return bSuccess;
}

FSearchResult SearchPCAKDTree(FSearchContext& SearchContext)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_PCA_KNN);
	SCOPE_CYCLE_COUNTER(STAT_PoseSearchPCAKNN);

	FSearchResult Result;

	const UPoseSearchDatabase* Database = SearchContext.GetSourceDatabase();
	check(Database);

	const int32 NumDimensions = Database->Schema->Layout.NumFloats;
	const FPoseSearchIndex* SearchIndex = SearchContext.GetSearchIndex();
	check(SearchIndex);

	const uint32 NumberOfPrincipalComponents = Database->GetNumberOfPrincipalComponents();
	const uint32 KDTreeQueryNumNeighbors = FMath::Clamp<uint32>(Database->KDTreeQueryNumNeighbors, 1, SearchIndex->NumPoses);

	//stack allocated temporaries
	TArrayView<size_t> ResultIndexes((size_t*)FMemory_Alloca((KDTreeQueryNumNeighbors + 1) * sizeof(size_t)), KDTreeQueryNumNeighbors + 1);
	TArrayView<float> ResultDistanceSqr((float*)FMemory_Alloca((KDTreeQueryNumNeighbors + 1) * sizeof(float)), KDTreeQueryNumNeighbors + 1);
	RowMajorVectorMap WeightedQueryValues((float*)FMemory_Alloca(NumDimensions * sizeof(float)), 1, NumDimensions);
	RowMajorVectorMap CenteredQueryValues((float*)FMemory_Alloca(NumDimensions * sizeof(float)), 1, NumDimensions);
	RowMajorVectorMap ProjectedQueryValues((float*)FMemory_Alloca(NumberOfPrincipalComponents * sizeof(float)), 1, NumberOfPrincipalComponents);

	// KDTree in PCA space search
	if (Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Validate)
	{
		for (const FGroupSearchIndex& GroupSearchIndex : SearchIndex->Groups)
		{
			const RowMajorVectorMapConst MapWeights(GroupSearchIndex.Weights.GetData(), 1, NumDimensions);

			// testing the KDTree is returning the proper searches for all the original points transformed in pca space
			for (int32 PoseIdx = GroupSearchIndex.StartPoseIndex; PoseIdx < GroupSearchIndex.EndPoseIndex; ++PoseIdx)
			{
				FKDTree::KNNResultSet ResultSet(Database->KDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr);
				TArrayView<const float> PoseValues = SearchIndex->GetPoseValues(PoseIdx);

				const RowMajorVectorMapConst Mean(GroupSearchIndex.Mean.GetData(), 1, NumDimensions);
				const ColMajorMatrixMapConst PCAProjectionMatrix(GroupSearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);

				const RowMajorVectorMapConst QueryValues(PoseValues.GetData(), 1, NumDimensions);
				WeightedQueryValues = QueryValues.array() * MapWeights.array();
				CenteredQueryValues.noalias() = WeightedQueryValues - Mean;
				ProjectedQueryValues.noalias() = CenteredQueryValues * PCAProjectionMatrix;

				GroupSearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedQueryValues.data());

				size_t ResultIndex = 0;
				for (; ResultIndex < ResultSet.Num(); ++ResultIndex)
				{
					if ((PoseIdx - GroupSearchIndex.StartPoseIndex) == ResultIndexes[ResultIndex])
					{
						check(ResultDistanceSqr[ResultIndex] < UE_KINDA_SMALL_NUMBER);
						break;
					}
				}
				check(ResultIndex < ResultSet.Num());
			}
		}
	}

	// @todo: implement support for DatabaseTagQuery
	FPoseCost BestPoseCost;
	int32 BestPoseIdx = INDEX_NONE;
	for (const FGroupSearchIndex& GroupSearchIndex : SearchIndex->Groups)
	{
		const RowMajorVectorMapConst MapWeights(GroupSearchIndex.Weights.GetData(), 1, NumDimensions);
		FKDTree::KNNResultSet ResultSet(KDTreeQueryNumNeighbors, ResultIndexes, ResultDistanceSqr);

		check(SearchContext.QueryValues.Num() == NumDimensions);

		const RowMajorVectorMapConst Mean(GroupSearchIndex.Mean.GetData(), 1, NumDimensions);
		const ColMajorMatrixMapConst PCAProjectionMatrix(GroupSearchIndex.PCAProjectionMatrix.GetData(), NumDimensions, NumberOfPrincipalComponents);

		// transforming query values into PCA space to query the KDTree
		const RowMajorVectorMapConst QueryValues(SearchContext.QueryValues.GetData(), 1, NumDimensions);
		WeightedQueryValues = QueryValues.array() * MapWeights.array();
		CenteredQueryValues.noalias() = WeightedQueryValues - Mean;
		ProjectedQueryValues.noalias() = CenteredQueryValues * PCAProjectionMatrix;

		GroupSearchIndex.KDTree.FindNeighbors(ResultSet, ProjectedQueryValues.data());

		for (size_t ResultIndex = 0; ResultIndex < ResultSet.Num(); ++ResultIndex)
		{
			const int32 PoseIdx = ResultIndexes[ResultIndex] + GroupSearchIndex.StartPoseIndex;

			const FPoseSearchPoseMetadata& Metadata = SearchIndex->PoseMetadata[PoseIdx];

			if (EnumHasAnyFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition))
			{
				continue;
			}

			FPoseCost PoseCost = ComparePoses(PoseIdx, SearchContext, GroupSearchIndex.GroupIndex);

			if (PoseCost < BestPoseCost)
			{
				BestPoseCost = PoseCost;
				BestPoseIdx = PoseIdx;
			}
		}
	}

	Result.PoseCost = BestPoseCost;
	Result.PoseIdx = BestPoseIdx;
	Result.SearchIndexAsset = SearchIndex->FindAssetForPose(BestPoseIdx);
	Result.AssetTime = SearchIndex->GetAssetTime(BestPoseIdx, Result.SearchIndexAsset);

	return Result;
}

FSearchResult SearchBruteForce(FSearchContext& SearchContext)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_Brute_Force);
	SCOPE_CYCLE_COUNTER(STAT_PoseSearchBruteForce);

	FSearchResult Result;

	const FPoseSearchIndex* SearchIndex = SearchContext.GetSearchIndex();
	check(SearchIndex);
	const UPoseSearchDatabase* Database = SearchContext.GetSourceDatabase();

	FPoseCost BestPoseCost;
	int32 BestPoseIdx = INDEX_NONE;
	for (const FPoseSearchIndexAsset& Asset : SearchIndex->Assets)
	{
		if (Database && SearchContext.DatabaseTagQuery)
		{
			if (!SearchContext.DatabaseTagQuery->Matches(*Database->GetSourceAssetGroupTags(&Asset)))
			{
				continue;
			}
		}

		const int32 EndIndex = Asset.FirstPoseIdx + Asset.NumPoses;
		for (int32 PoseIdx = Asset.FirstPoseIdx; PoseIdx < EndIndex; ++PoseIdx)
		{
			const FPoseSearchPoseMetadata& Metadata = SearchIndex->PoseMetadata[PoseIdx];

			if (EnumHasAnyFlags(Metadata.Flags, EPoseSearchPoseFlags::BlockTransition))
			{
				continue;
			}

			FPoseCost PoseCost = ComparePoses(PoseIdx, SearchContext, Asset.SourceGroupIdx);

			if (PoseCost < BestPoseCost)
			{
				BestPoseCost = PoseCost;
				BestPoseIdx = PoseIdx;
			}
		}
	}

	Result.PoseCost = BestPoseCost;
	Result.PoseIdx = BestPoseIdx;
	Result.SearchIndexAsset = SearchIndex->FindAssetForPose(BestPoseIdx);
	Result.AssetTime = SearchIndex->GetAssetTime(BestPoseIdx, Result.SearchIndexAsset);

	return Result;
}

FSearchResult Search(FSearchContext& SearchContext)
{
	FSearchResult Result;

	const FPoseSearchIndex* SearchIndex = SearchContext.GetSearchIndex();
	if (!SearchIndex)
	{
		return Result;
	}

	if (!ensure(SearchIndex->IsValid() && !SearchIndex->IsEmpty()))
	{
		return Result;
	}

	if (!ensure(SearchContext.QueryValues.Num() == SearchIndex->Schema->Layout.NumFloats))
	{
		return Result;
	}

	const UPoseSearchDatabase* Database = SearchContext.GetSourceDatabase();
	if (Database && Database->PoseSearchMode != EPoseSearchMode::BruteForce)
	{
		Result = SearchPCAKDTree(SearchContext);
	}

	if (!Database || Database->PoseSearchMode == EPoseSearchMode::BruteForce || Database->PoseSearchMode == EPoseSearchMode::PCAKDTree_Compare)
	{
		Result = SearchBruteForce(SearchContext);
	}

	SearchContext.DebugDrawParams.PoseVector = SearchContext.QueryValues;
	SearchContext.DebugDrawParams.PoseIdx = Result.PoseIdx;
	Draw(SearchContext.DebugDrawParams);

	return Result;
}

static void ComputePoseCostAddends(
	int32 PoseIdx, 
	FSearchContext& SearchContext, 
	float& OutNotifyAddend, 
	float& OutMirrorMismatchAddend)
{
	OutNotifyAddend = 0.0f;
	OutMirrorMismatchAddend = 0.0f;

	if (SearchContext.QueryMirrorRequest != EPoseSearchBooleanRequest::Indifferent)
	{
		const FPoseSearchIndexAsset* IndexAsset = SearchContext.GetSearchIndex()->FindAssetForPose(PoseIdx);
		const bool bMirroringMismatch =
			(IndexAsset->bMirrored && SearchContext.QueryMirrorRequest == EPoseSearchBooleanRequest::FalseValue) ||
			(!IndexAsset->bMirrored && SearchContext.QueryMirrorRequest == EPoseSearchBooleanRequest::TrueValue);
		if (bMirroringMismatch)
		{
			OutMirrorMismatchAddend = SearchContext.GetMirrorMismatchCost();
		}
	}

	const FPoseSearchPoseMetadata& PoseMetadata = SearchContext.GetSearchIndex()->PoseMetadata[PoseIdx];
	OutNotifyAddend = PoseMetadata.CostAddend;
}


FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, int32 GroupIdx)
{
	FPoseCost Result;

	const FPoseSearchIndex* SearchIndex = SearchContext.GetSearchIndex();
	if (!ensure(SearchIndex))
	{
		return Result;
	}

	TArrayView<const float> PoseValues = SearchIndex->GetPoseValues(PoseIdx);
	if (!ensure(PoseValues.Num() == SearchContext.QueryValues.Num()))
	{
		return Result;
	}

	if (SearchContext.WeightsContext)
	{
		if (GroupIdx == INDEX_NONE)
		{
			const FPoseSearchIndexAsset* SearchIndexAsset = SearchIndex->FindAssetForPose(PoseIdx);
			if (!ensure(SearchIndexAsset))
			{
				return Result;
			}

			GroupIdx = SearchIndexAsset->SourceGroupIdx;
		}

		const FPoseSearchWeights* WeightsSet =
			SearchContext.WeightsContext->GetGroupWeights(GroupIdx);
		Result.Dissimilarity = CompareFeatureVectors(
			PoseValues.Num(),
			PoseValues.GetData(),
			SearchContext.QueryValues.GetData(),
			WeightsSet->Weights.GetData());
	}
	else
	{
		Result.Dissimilarity = CompareFeatureVectors(PoseValues.Num(), PoseValues.GetData(), SearchContext.QueryValues.GetData());
	}

	float NotifyAddend = 0.0f;
	float MirrorMismatchAddend = 0.0f;
	ComputePoseCostAddends(PoseIdx, SearchContext, NotifyAddend, MirrorMismatchAddend);
	Result.CostAddend = NotifyAddend + MirrorMismatchAddend;
	Result.TotalCost = Result.Dissimilarity + Result.CostAddend;

	return Result;
}


FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, FPoseCostDetails& OutPoseCostDetails)
{
	using namespace Eigen;

	FPoseCost Result;

	TArrayView<const float> PoseValues = SearchContext.GetSearchIndex()->GetPoseValues(PoseIdx);
	const int32 Dims = PoseValues.Num();
	if (!ensure(Dims == SearchContext.QueryValues.Num()))
	{
		return Result;
	}

	OutPoseCostDetails.CostVector.SetNum(Dims);

	// Setup Eigen views onto our vectors
	auto OutCostVector = Map<ArrayXf>(OutPoseCostDetails.CostVector.GetData(), Dims);
	auto PoseVector = Map<const ArrayXf>(PoseValues.GetData(), Dims);
	auto QueryVector = Map<const ArrayXf>(SearchContext.QueryValues.GetData(), Dims);
	
	// Compute weighted squared difference vector
	const FPoseSearchIndexAsset* SearchIndexAsset = SearchContext.GetSearchIndex()->FindAssetForPose(PoseIdx);
	if (SearchContext.WeightsContext)
	{
		const FPoseSearchWeights* WeightsSet = 
			SearchContext.WeightsContext->GetGroupWeights(SearchIndexAsset->SourceGroupIdx);
		check(WeightsSet);
		check(WeightsSet->Weights.Num() == Dims);
		auto WeightsVector = Map<const ArrayXf>(WeightsSet->Weights.GetData(), Dims);

		OutCostVector = WeightsVector * (PoseVector - QueryVector).square();
		Result.Dissimilarity = OutCostVector.sum();
	}
	else
	{
		OutCostVector = (PoseVector - QueryVector).square();
		Result.Dissimilarity = OutCostVector.sum();
	}

	// Output result
	float NotifyAddend = 0.0f;
	float MirrorMismatchAddend = 0.0f;
	ComputePoseCostAddends(PoseIdx, SearchContext, NotifyAddend, MirrorMismatchAddend);
	Result.CostAddend = NotifyAddend + MirrorMismatchAddend;
	Result.TotalCost = Result.Dissimilarity + Result.CostAddend;

	// Output cost details
	OutPoseCostDetails.NotifyCostAddend = NotifyAddend;
	OutPoseCostDetails.MirrorMismatchCostAddend = MirrorMismatchAddend;
	OutPoseCostDetails.PoseCost = Result;
	CalcChannelCosts(SearchContext.GetSearchIndex()->Schema, OutPoseCostDetails.CostVector, OutPoseCostDetails.ChannelCosts);


#if DO_GUARD_SLOW
	{
		// Verify details pose comparator agrees with runtime pose comparator
		FPoseCost RuntimeComparatorCost = ComparePoses(PoseIdx, SearchContext, SearchIndexAsset->SourceGroupIdx);
		checkSlow(FMath::IsNearlyEqual(Result.TotalCost, RuntimeComparatorCost.TotalCost, 1e-3f));

		// Verify channel cost decomposition agrees with runtime pose comparator
		auto OutChannelCosts = Map<const ArrayXf>(OutPoseCostDetails.ChannelCosts.GetData(), OutPoseCostDetails.ChannelCosts.Num());
		checkSlow(FMath::IsNearlyEqual(OutChannelCosts.sum(), RuntimeComparatorCost.Dissimilarity, 1e-3f));
	}
#endif

	return Result;
}


//////////////////////////////////////////////////////////////////////////
// FModule

class FModule : public IModuleInterface, public UE::Anim::IPoseSearchProvider
{
public: // IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public: // IPoseSearchProvider
	virtual UE::Anim::IPoseSearchProvider::FSearchResult Search(const FAnimationBaseContext& GraphContext, const UAnimSequenceBase* Sequence) override;

private:
#if WITH_EDITOR
	void OnObjectSaved(UObject* SavedObject, FObjectPreSaveContext SaveContext);
#endif // WITH_EDITOR
};

void FModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(UE::Anim::IPoseSearchProvider::ModularFeatureName, this);

#if UE_POSE_SEARCH_TRACE_ENABLED
	// Enable the PoseSearch trace channel
	UE::Trace::ToggleChannel(*FTraceLogger::Name.ToString(), true);
#endif

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FModule::OnObjectSaved);
#endif // WITH_EDITOR
}

void FModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(UE::Anim::IPoseSearchProvider::ModularFeatureName, this);
}

UE::Anim::IPoseSearchProvider::FSearchResult FModule::Search(const FAnimationBaseContext& GraphContext, const UAnimSequenceBase* Sequence)
{
	UE::Anim::IPoseSearchProvider::FSearchResult ProviderResult;

	const UPoseSearchSequenceMetaData* MetaData = Sequence ? Sequence->FindMetaDataByClass<UPoseSearchSequenceMetaData>() : nullptr;
	if (!MetaData || !MetaData->IsValidForSearch())
	{
		return ProviderResult;
	}

	IPoseHistoryProvider* PoseHistoryProvider = GraphContext.GetMessage<IPoseHistoryProvider>();
	if (!PoseHistoryProvider)
	{
		return ProviderResult;
	}

	FPoseHistory& PoseHistory = PoseHistoryProvider->GetPoseHistory();
	FPoseSearchFeatureVectorBuilder& QueryBuilder = PoseHistory.GetQueryBuilder();
	QueryBuilder.Init(MetaData->Schema);

	UE::PoseSearch::FQueryBuildingContext QueryBuildingContext(QueryBuilder);
	QueryBuildingContext.Schema = MetaData->Schema;
	QueryBuildingContext.History = &PoseHistory;
	QueryBuildingContext.Trajectory = nullptr;

	if (!UE::PoseSearch::BuildQuery(QueryBuildingContext))
	{
		return ProviderResult;
	}

	QueryBuilder.Normalize(MetaData->SearchIndex);

	::UE::PoseSearch::FSearchContext SearchContext;
	SearchContext.SetSource(Sequence);
	SearchContext.QueryValues = QueryBuilder.GetNormalizedValues();
	::UE::PoseSearch::FSearchResult Result = ::UE::PoseSearch::Search(SearchContext);

	ProviderResult.Dissimilarity = Result.PoseCost.TotalCost;
	ProviderResult.PoseIdx = Result.PoseIdx;
	ProviderResult.TimeOffsetSeconds = Result.AssetTime;
	return ProviderResult;
}

#if WITH_EDITOR

void GetPoseSearchDatabaseAssetDataList(TArray<FAssetData>& OutPoseSearchDatabaseAssetDataList)
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UPoseSearchDatabase::StaticClass()->GetClassPathName());

	OutPoseSearchDatabaseAssetDataList.Reset();
	AssetRegistryModule.Get().GetAssets(Filter, OutPoseSearchDatabaseAssetDataList);
}

void ForEachPoseSearchDatabase(bool bLoadAssets, TFunctionRef<void(UPoseSearchDatabase&)> InFunction)
{
	TArray<FAssetData> PoseSearchDatabaseAssetDataList;
	GetPoseSearchDatabaseAssetDataList(PoseSearchDatabaseAssetDataList);
	for (const auto& PoseSearchDbAssetData : PoseSearchDatabaseAssetDataList)
	{
		if (UPoseSearchDatabase* PoseSearchDb = 
			Cast<UPoseSearchDatabase>(PoseSearchDbAssetData.FastGetAsset(bLoadAssets)))
		{
			InFunction(*PoseSearchDb);
		}
	}
}

void FModule::OnObjectSaved(UObject* SavedObject, FObjectPreSaveContext SaveContext)
{
	if (UAnimSequence* SavedSequence = Cast<UAnimSequence>(SavedObject))
	{
		ForEachPoseSearchDatabase(false, [SavedSequence](UPoseSearchDatabase& PoseSearchDb)
		{
			bool bSequenceFound =
				PoseSearchDb.Sequences.ContainsByPredicate([SavedSequence](FPoseSearchDatabaseSequence& DbSequence)
			{
				bool bIsMatch =
					SavedSequence == DbSequence.Sequence ||
					SavedSequence == DbSequence.LeadInSequence ||
					SavedSequence == DbSequence.FollowUpSequence;
				return bIsMatch;
			});

			if (bSequenceFound)
			{
				PoseSearchDb.BeginCacheDerivedData();
			}
		});
	}
	else if (UBlendSpace* SavedBlendSpace = Cast<UBlendSpace>(SavedObject))
	{
		ForEachPoseSearchDatabase(false, [SavedBlendSpace](UPoseSearchDatabase& PoseSearchDb)
		{
			bool bBlendSpaceFound = PoseSearchDb.BlendSpaces.ContainsByPredicate(
					[SavedBlendSpace](FPoseSearchDatabaseBlendSpace& DbBlendSpace)
			{
				return SavedBlendSpace == DbBlendSpace.BlendSpace;
			});

			if (bBlendSpaceFound)
			{
				PoseSearchDb.BeginCacheDerivedData();
			}
		});
	}
	else if (UPoseSearchSchema* SavedSchema = Cast<UPoseSearchSchema>(SavedObject))
	{
		ForEachPoseSearchDatabase(false, [SavedSchema](UPoseSearchDatabase& PoseSearchDb)
		{
			if (PoseSearchDb.Schema == SavedSchema)
			{
				PoseSearchDb.BeginCacheDerivedData();
			}
		});
	}
	else if (USkeleton* SavedSkeleton = Cast<USkeleton>(SavedObject))
	{
		ForEachPoseSearchDatabase(false, [SavedSkeleton](UPoseSearchDatabase& PoseSearchDb)
		{
			if (PoseSearchDb.Schema && PoseSearchDb.Schema->Skeleton == SavedSkeleton)
			{
				PoseSearchDb.BeginCacheDerivedData();
			}
		});
	}
}
#endif // WITH_EDITOR

}} // namespace UE::PoseSearch

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::PoseSearch::FModule, PoseSearch)