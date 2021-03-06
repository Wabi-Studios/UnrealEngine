// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/RingBuffer.h"
#include "Engine/DataAsset.h"
#include "Modules/ModuleManager.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimMetaData.h"
#include "Animation/AnimNodeMessages.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/MotionTrajectoryTypes.h"
#include "AlphaBlend.h"
#include "BoneIndices.h"
#include "GameplayTagContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "PoseSearch/KDTree.h"

#include "PoseSearch.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPoseSearch, Log, All);


//////////////////////////////////////////////////////////////////////////
// Forward declarations

class UAnimSequence;
class UBlendSpace;
struct FCompactPose;
struct FPoseContext;
struct FReferenceSkeleton;
struct FPoseSearchDatabaseDerivedData;
class UAnimNotifyState_PoseSearchBase;
class UPoseSearchSchema;
class FBlake3;

namespace UE::PoseSearch {

class FPoseHistory;
struct FPoseSearchDatabaseAsyncCacheTask;
struct FDebugDrawParams;
class FFeatureVectorReader;
struct FSchemaInitializer;
struct FQueryBuildingContext;

} // namespace UE::PoseSearch



//////////////////////////////////////////////////////////////////////////
// Constants

UENUM()
enum class EPoseSearchFeatureType : int8
{
	Position,	
	Rotation,
	LinearVelocity,
	AngularVelocity,
	ForwardVector,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchFeatureDomain : int32
{
	Time,
	Distance,

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchBooleanRequest : int32
{
	FalseValue,
	TrueValue,
	Indifferent, // if this is used, there will be no cost difference between true and false results

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchMode : int32
{
	BruteForce,
	PCAKDTree,
	PCAKDTree_Validate,	// runs PCAKDTree and performs validation tests
	PCAKDTree_Compare,	// compares BruteForce vs PCAKDTree

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchDataPreprocessor : int32
{
	None,
	Automatic,
	Normalize,
	Sphere UMETA(Hidden),

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};

UENUM()
enum class EPoseSearchPoseFlags : uint32
{
	None = 0,

	// Don't return this pose as a search result
	BlockTransition = 1 << 0,
};
ENUM_CLASS_FLAGS(EPoseSearchPoseFlags);

UENUM()
enum class ESearchIndexAssetType : int32
{
	Invalid,
	Sequence,
	BlendSpace,
};

UENUM()
enum class EPoseSearchMirrorOption : int32
{
	UnmirroredOnly UMETA(DisplayName = "Original Only"),
	MirroredOnly UMETA(DisplayName = "Mirrored Only"),
	UnmirroredAndMirrored UMETA(DisplayName = "Original and Mirrored"),

	Num UMETA(Hidden),
	Invalid = Num UMETA(Hidden)
};



//////////////////////////////////////////////////////////////////////////
// Common structs

USTRUCT()
struct POSESEARCH_API FPoseSearchExtrapolationParameters
{
	GENERATED_BODY()

	// If the angular root motion speed in degrees is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float AngularSpeedThreshold = 1.0f;

	// If the root motion linear speed is below this value, it will be treated as zero.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float LinearSpeedThreshold = 1.0f;

	// Time from sequence start/end used to extrapolate the trajectory.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SampleTime = 0.05f;
};

USTRUCT()
struct FPoseSearchBlockTransitionParameters
{
	GENERATED_BODY()

	// Excluding the beginning of sequences can help ensure an exact past trajectory is used when building the features
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceStartInterval = 0.0f;

	// Excluding the end of sequences help ensure an exact future trajectory, and also prevents the selection of
	// a sequence which will end too soon to be worth selecting.
	UPROPERTY(EditAnywhere, Category = "Settings")
	float SequenceEndInterval = 0.0f;
};

USTRUCT()
struct POSESEARCH_API FPoseSearchBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Config)
	FBoneReference Reference;

	UPROPERTY(EditAnywhere, Category = Config)
	bool bUseVelocity = false;

	UPROPERTY(EditAnywhere, Category = Config)
	bool bUsePosition = false;

	UPROPERTY(EditAnywhere, Category = Config)
	bool bUseRotation = false;

	// this function will return a mask out of EPoseSearchFeatureType based on which features were selected for
	// the bone.
	uint32 GetTypeMask() const;
};



//////////////////////////////////////////////////////////////////////////
// Feature descriptions and vector layout

/** Describes each feature of a vector, including data type, sampling options, and buffer offset. */
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureDesc
{
	GENERATED_BODY()

	// Index into UPoseSearchSchema::Channels
	UPROPERTY()
	int8 ChannelIdx = -1;

	// Optional feature identifier within a channel
	UPROPERTY()
	int8 ChannelFeatureId = 0;

	// Index into channel's sample offsets, if any
	UPROPERTY()
	int8 SubsampleIdx = 0;

	// Value type of the feature
	UPROPERTY()
	EPoseSearchFeatureType Type = EPoseSearchFeatureType::Invalid;

	// Set via FPoseSearchFeatureLayout::Init() and ignored by operator==
	UPROPERTY()
	int16 ValueOffset = 0;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
		EPoseSearchFeatureDomain Domain_DEPRECATED = EPoseSearchFeatureDomain::Time;

	UPROPERTY()
	int32 SchemaBoneIdx_DEPRECATED = 0;
#endif

	bool operator==(const FPoseSearchFeatureDesc& Other) const;

	bool IsValid() const { return Type != EPoseSearchFeatureType::Invalid; }
};


/**
* Explicit description of a pose feature vector.
* Determined by options set in a UPoseSearchSchema and owned by the schema.
* See UPoseSearchSchema::GenerateLayout().
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchFeatureVectorLayout
{
	GENERATED_BODY()

	void Finalize();
	void Reset();

	UPROPERTY()
	TArray<FPoseSearchFeatureDesc> Features;

	UPROPERTY()
	int32 NumFloats = 0;

	bool IsValid(int32 ChannelCount) const;

	bool EnumerateBy(int32 ChannelIdx, EPoseSearchFeatureType Type, int32& InOutFeatureIdx) const;
};



//////////////////////////////////////////////////////////////////////////
// Asset sampling and indexing

namespace UE::PoseSearch {

struct POSESEARCH_API FAssetSamplingContext
{
	// Time delta used for computing pose derivatives
	static constexpr float FiniteDelta = 1 / 60.0f;

	FBoneContainer BoneContainer;

	// Mirror data table pointer copied from Schema for convenience
	TObjectPtr<UMirrorDataTable> MirrorDataTable = nullptr;

	// Compact pose format of Mirror Bone Map
	TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex> CompactPoseMirrorBones;

	// Pre-calculated component space rotations of reference pose, which allows mirror to work with any joint orientation
	// Only initialized and used when a mirroring table is specified
	TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> ComponentSpaceRefRotations;


	void Init(const UPoseSearchSchema* Schema);
	FTransform MirrorTransform(const FTransform& Transform) const;
};

/**
 * Helper interface for sampling data from animation assets
 */
class POSESEARCH_API IAssetSampler
{
public:
	virtual ~IAssetSampler() {};

	virtual float GetPlayLength() const = 0;
	virtual bool IsLoopable() const = 0;

	// Gets the time associated with a particular root distance traveled
	virtual float GetTimeFromRootDistance(float Distance) const = 0;

	// Gets the total root distance traveled 
	virtual float GetTotalRootDistance() const = 0;

	// Gets the final root transformation at the end of the asset's playback time
	virtual FTransform GetTotalRootTransform() const = 0;

	// Extracts pose for this asset for a given context
	virtual void ExtractPose(const FAnimExtractContext& ExtractionCtx, FAnimationPoseData& OutAnimPoseData) const = 0;

	// Extracts the accumulated root distance at the given time, using the extremities of the sequence to extrapolate 
	// beyond the sequence limits when Time is less than zero or greater than the sequence length
	virtual float ExtractRootDistance(float Time) const = 0;

	// Extracts root transform at the given time, using the extremities of the sequence to extrapolate beyond the 
	// sequence limits when Time is less than zero or greater than the sequence length.
	virtual FTransform ExtractRootTransform(float Time) const = 0;

	// Extracts notify states inheriting from UAnimNotifyState_PoseSearchBase present in the sequence at Time.
	// The function does not empty NotifyStates before adding new notifies!
	virtual void ExtractPoseSearchNotifyStates(float Time, TArray<UAnimNotifyState_PoseSearchBase*>& NotifyStates) const = 0;
};

/**
 * Inputs for asset indexing
 */
struct FAssetIndexingContext
{
	const FAssetSamplingContext* SamplingContext = nullptr;
	const UPoseSearchSchema* Schema = nullptr;
	const IAssetSampler* MainSampler = nullptr;
	const IAssetSampler* LeadInSampler = nullptr;
	const IAssetSampler* FollowUpSampler = nullptr;
	bool bMirrored = false;
	FFloatInterval RequestedSamplingRange = FFloatInterval(0.0f, 0.0f);
	FPoseSearchBlockTransitionParameters BlockTransitionParameters;

	// Index this asset's data from BeginPoseIdx up to but not including EndPoseIdx
	int32 BeginSampleIdx = 0;
	int32 EndSampleIdx = 0;
};

/**
 * Output of indexer data for this asset
 */
struct FAssetIndexingOutput
{
	// Channel data should be written to this array of feature vector builders
	// Size is EndPoseIdx - BeginPoseIdx and PoseVectors[0] contains data for BeginPoseIdx
	const TArrayView<FPoseSearchFeatureVectorBuilder> PoseVectors;
};

class POSESEARCH_API IAssetIndexer
{
public:
	struct FSampleInfo
	{
		const IAssetSampler* Clip = nullptr;
		FTransform RootTransform;
		float ClipTime = 0.0f;
		float RootDistance = 0.0f;
		bool bClamped = false;

		bool IsValid() const { return Clip != nullptr; }
	};

	virtual ~IAssetIndexer() {}

	virtual const FAssetIndexingContext& GetIndexingContext() const = 0;
	virtual FSampleInfo GetSampleInfo(float SampleTime) const = 0;
	virtual FSampleInfo GetSampleInfoRelative(float SampleTime, const FSampleInfo& Origin) const = 0;
	virtual const float GetSampleTimeFromDistance(float Distance) const = 0;
	virtual FTransform MirrorTransform(const FTransform& Transform) const = 0;
};

} // namespace UE::PoseSearch


//////////////////////////////////////////////////////////////////////////
// Feature channels interface

UCLASS(Abstract, BlueprintType, EditInlineNew)
class POSESEARCH_API UPoseSearchFeatureChannel : public UObject, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	int32 GetChannelIndex() const { checkSlow(ChannelIdx >= 0); return ChannelIdx; }

	// Called during UPoseSearchSchema::Finalize to prepare the schema for this channel
	virtual void InitializeSchema(UE::PoseSearch::FSchemaInitializer& Initializer) PURE_VIRTUAL(UPoseSearchFeatureChannel::InitializeSchema, );

	// Called at database build time to populate pose vectors with this channel's data
	virtual void IndexAsset(const UE::PoseSearch::IAssetIndexer& Indexer, UE::PoseSearch::FAssetIndexingOutput& IndexingOutput) const PURE_VIRTUAL(UPoseSearchFeatureChannel::IndexAsset, );

	// Return this channel's range of sampling offsets in the requested sampling domain.
	// Returns empty range if the channel has no horizon in the requested domain.
	virtual FFloatRange GetHorizonRange(EPoseSearchFeatureDomain Domain) const PURE_VIRTUAL(UPoseSearchFeatureChannel::GetHorizonRange, return FFloatRange::Empty(); );

	// Return this channel's horizon sampling offsets
	virtual TArrayView<const float> GetSampleOffsets () const PURE_VIRTUAL(UPoseSearchFeatureChannel::GetSampleOffsets, return {}; );

	// Hash channel properties to produce a key for database derived data
	virtual void GenerateDDCKey(FBlake3& InOutKeyHasher) const PURE_VIRTUAL(UPoseSearchFeatureChannel::GenerateDDCKey, );

	// Called at runtime to add this channel's data to the query pose vector
	virtual bool BuildQuery(UE::PoseSearch::FQueryBuildingContext& Context) const PURE_VIRTUAL(UPoseSearchFeatureChannel::BuildQuery, return false;);

	// Draw this channel's data for the given pose vector
	virtual void DebugDraw(const UE::PoseSearch::FDebugDrawParams& DrawParams, const UE::PoseSearch::FFeatureVectorReader& Reader) const PURE_VIRTUAL(UPoseSearchFeatureChannel::DebugDraw, );

private:
	// IBoneReferenceSkeletonProvider interface
	// Note this function is exclusively for FBoneReference details customization
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

private:

	friend class ::UPoseSearchSchema;

	UPROPERTY()
	int32 ChannelIdx = -1;
};



//////////////////////////////////////////////////////////////////////////
// Schema

namespace UE::PoseSearch {

struct POSESEARCH_API FSchemaInitializer
{
public:

	int32 AddBoneReference(const FBoneReference& BoneReference);
	int32 AddFeatureDesc(const FPoseSearchFeatureDesc& FeatureDesc);

private:
	friend class ::UPoseSearchSchema;

	int32 CurrentChannelIdx = 0;

	TArray<FBoneReference> BoneReferences;
	TArray<FPoseSearchFeatureDesc> Features;
};

} // namespace UE::PoseSearch


/**
* Specifies the format of a pose search index. At runtime, queries are built according to the schema for searching.
*/
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental, meta = (DisplayName = "Motion Database Config"))
class POSESEARCH_API UPoseSearchSchema : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	static constexpr int32 DefaultSampleRate = 10;
	static constexpr int32 MaxBoneReferences = MAX_int8;
	static constexpr int32 MaxChannels = MAX_int8;
	static constexpr int32 MaxFeatures = MAX_int8;

	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<USkeleton> Skeleton = nullptr;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "1", ClampMax = "60"), Category = "Schema")
	int32 SampleRate = DefaultSampleRate;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Schema")
	TArray<TObjectPtr<UPoseSearchFeatureChannel>> Channels;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bUseTrajectoryVelocities_DEPRECATED = true;

	UPROPERTY()
	bool bUseTrajectoryPositions_DEPRECATED = true;

	UPROPERTY()
	bool bUseTrajectoryForwardVectors_DEPRECATED = false;

	UPROPERTY()
	TArray<FPoseSearchBone> SampledBones_DEPRECATED;

	UPROPERTY()
	TArray<float> PoseSampleTimes_DEPRECATED;

	UPROPERTY()
	TArray<float> TrajectorySampleTimes_DEPRECATED;

	UPROPERTY()
	TArray<float> TrajectorySampleDistances_DEPRECATED;
#endif // WITH_EDITOR


	// If set, this schema will support mirroring pose search databases
	UPROPERTY(EditAnywhere, Category = "Schema")
	TObjectPtr<UMirrorDataTable> MirrorDataTable;

	UPROPERTY(EditAnywhere, Category = "Schema")
	EPoseSearchDataPreprocessor DataPreprocessor = EPoseSearchDataPreprocessor::Automatic;

	UPROPERTY()
	EPoseSearchDataPreprocessor EffectiveDataPreprocessor = EPoseSearchDataPreprocessor::Invalid;

	UPROPERTY()
	float SamplingInterval = 1.0f / DefaultSampleRate;

	UPROPERTY()
	FPoseSearchFeatureVectorLayout Layout;

	UPROPERTY()
	TArray<FBoneReference> BoneReferences;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndices;

	UPROPERTY(Transient)
	TArray<uint16> BoneIndicesWithParents;

	bool IsValid () const;

	int32 GetNumBones () const { return BoneIndices.Num(); }

	// Returns global range of sampling offsets among all channels in requested sampling domain.
	// Returns empty range if the channel has no horizon in the requested domain.
	virtual FFloatRange GetHorizonRange(EPoseSearchFeatureDomain Domain) const;

	TArrayView<const float> GetChannelSampleOffsets (int32 ChannelIdx) const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostLoad() override;

public: // IBoneReferenceSkeletonProvider
	class USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override { bInvalidSkeletonIsError = false; return Skeleton; }

private:
	void Finalize();
	void ResolveBoneReferences();
};



//////////////////////////////////////////////////////////////////////////
// Search index

USTRUCT()
struct POSESEARCH_API FPoseSearchIndexPreprocessInfo
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumDimensions = 0;

	UPROPERTY()
	TArray<float> TransformationMatrix;

	UPROPERTY()
	TArray<float> InverseTransformationMatrix;

	UPROPERTY()
	TArray<float> SampleMean;

	void Reset()
	{
		NumDimensions = 0;
		TransformationMatrix.Reset();
		InverseTransformationMatrix.Reset();
		SampleMean.Reset();
	}
};


/**
 * This is kept for each pose in the search index along side the feature vector values and is used to influence the search.
 */
USTRUCT()
struct POSESEARCH_API FPoseSearchPoseMetadata
{
	GENERATED_BODY()

	UPROPERTY()
	EPoseSearchPoseFlags Flags = EPoseSearchPoseFlags::None;

	UPROPERTY()
	float CostAddend = 0.0f;
};


/**
* Information about a source animation asset used by a search index.
* Some source animation entries may generate multiple FPoseSearchIndexAsset entries.
**/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndexAsset
{
	GENERATED_BODY()
public:
	FPoseSearchIndexAsset()
	{}

	FPoseSearchIndexAsset(
		ESearchIndexAssetType InType,
		int32 InSourceGroupIdx, 
		int32 InSourceAssetIdx, 
		bool bInMirrored, 
		const FFloatInterval& InSamplingInterval,
		FVector InBlendParameters = FVector::Zero())
		: Type(InType)
		, SourceGroupIdx(InSourceGroupIdx)
		, SourceAssetIdx(InSourceAssetIdx)
		, bMirrored(bInMirrored)
		, BlendParameters(InBlendParameters)
		, SamplingInterval(InSamplingInterval)
	{}

	// Default to Sequence for now for backward compatibility but
	// at some point we might want to change this to Invalid.
	UPROPERTY()
	ESearchIndexAssetType Type = ESearchIndexAssetType::Sequence;

	UPROPERTY()
	int32 SourceGroupIdx = INDEX_NONE;

	// Index of the source asset in search index's container (i.e. UPoseSearchDatabase)
	UPROPERTY()
	int32 SourceAssetIdx = INDEX_NONE;

	UPROPERTY()
	bool bMirrored = false;

	UPROPERTY()
	FVector BlendParameters = FVector::Zero();

	UPROPERTY()
	FFloatInterval SamplingInterval;

	UPROPERTY()
	int32 FirstPoseIdx = INDEX_NONE;

	UPROPERTY()
	int32 NumPoses = 0;

	bool IsPoseInRange(int32 PoseIdx) const
	{
		return (PoseIdx >= FirstPoseIdx) && (PoseIdx < FirstPoseIdx + NumPoses);
	}
};

USTRUCT()
struct POSESEARCH_API FGroupSearchIndex
{
	GENERATED_BODY()

	UE::PoseSearch::FKDTree KDTree;

	UPROPERTY()
	TArray<float> PCAProjectionMatrix;

	UPROPERTY()
	TArray<float> Mean;

	UPROPERTY()
	int32 StartPoseIndex = 0;
	
	UPROPERTY()
	int32 EndPoseIndex = 0;

	UPROPERTY()
	int32 GroupIndex = 0;

	UPROPERTY()
	TArray<float> Weights;
};

/**
* A search index for animation poses. The structure of the search index is determined by its UPoseSearchSchema.
* May represent a single animation (see UPoseSearchSequenceMetaData) or a collection (see UPoseSearchDatabase).
*/
USTRUCT()
struct POSESEARCH_API FPoseSearchIndex
{
	GENERATED_BODY()

	UPROPERTY()
	int32 NumPoses = 0;

	UPROPERTY()
	TArray<float> Values;

	UPROPERTY()
	TArray<float> PCAValues;

	UPROPERTY()
	TArray<FGroupSearchIndex> Groups;

	UPROPERTY()
	TArray<FPoseSearchPoseMetadata> PoseMetadata;

	UPROPERTY()
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY()
	FPoseSearchIndexPreprocessInfo PreprocessInfo;

	UPROPERTY()
	TArray<FPoseSearchIndexAsset> Assets;

	bool IsValid() const;
	bool IsEmpty() const;

	TArrayView<const float> GetPoseValues(int32 PoseIdx) const;

	int32 FindAssetIndex(const FPoseSearchIndexAsset* Asset) const;
	const FPoseSearchIndexAsset* FindAssetForPose(int32 PoseIdx) const;
	float GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* Asset) const;

	void Reset();

	void Normalize (TArrayView<float> PoseVector) const;
	void InverseNormalize (TArrayView<float> PoseVector) const;
};



//////////////////////////////////////////////////////////////////////////
// Database

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelHorizonParams
{
	GENERATED_BODY()

	// Total score contribution of all samples within this horizon, normalized with other horizons
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	// Whether to interpolate samples within this horizon
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced")
	bool bInterpolate = false;

	// Horizon sample weights will be interpolated from InitialValue to 1.0 - InitialValue and then normalized
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate", ClampMin="0.0", ClampMax="1.0"))
	float InitialValue = 0.1f;

	// Curve type for horizon interpolation 
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Advanced", meta = (EditCondition = "bInterpolate"))
	EAlphaBlendOption InterpolationMethod = EAlphaBlendOption::Linear;
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchChannelWeightParams
{
	GENERATED_BODY()

	// Contribution of this score component. Normalized with other channels.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "0.0"))
	float ChannelWeight = 1.0f;

	// History horizon params (for sample offsets <= 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams HistoryParams;

	// Prediction horizon params (for sample offsets > 0)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FPoseSearchChannelHorizonParams PredictionParams;

	// Contribution of each type within this channel
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	TMap<EPoseSearchFeatureType, float> TypeWeights;

	FPoseSearchChannelWeightParams();
};

USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchWeightParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	TArray<FPoseSearchChannelWeightParams> ChannelWeights;

	const FPoseSearchChannelWeightParams* GetChannelWeights(int32 ChannelIdx) const;
};


USTRUCT()
struct POSESEARCH_API FPoseSearchWeights
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<float> Weights;

	bool IsInitialized() const { return !Weights.IsEmpty(); }
	void Init(const FPoseSearchWeightParams& WeightParams, const UPoseSearchSchema* Schema);
};

USTRUCT()
struct POSESEARCH_API FPoseSearchWeightsContext
{
	GENERATED_BODY()

public:
	// Computes and caches new group weights whenever the database changes
	void Update(const UPoseSearchDatabase * Database);

	const FPoseSearchWeights* GetGroupWeights (int32 WeightsGroupIdx) const;
	
private:
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchDatabase> Database = nullptr;

	UPROPERTY(Transient)
	FPoseSearchWeights ComputedDefaultGroupWeights;

	UPROPERTY(Transient)
	TArray<FPoseSearchWeights> ComputedGroupWeights;

#if WITH_EDITOR
	// used to check if the data has changed, which requires the weights to be recomputed
	FIoHash SearchIndexHash = FIoHash::Zero;
#endif
};


/** An entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseSequence
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopAnimation = false;

	UPROPERTY(EditAnywhere, Category = "Sequence")
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// Used for sampling past pose information at the beginning of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with past sampling. When past sampling is used without a lead in sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> LeadInSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopLeadInAnimation = false;

	// Used for sampling future pose information at the end of the main sequence.
	// This setting is intended for transitions between cycles. It is optional and only used
	// for one shot anims with future sampling. When future sampling is used without a follow up sequence,
	// the sampling range of the main sequence will be clamped if necessary.
	UPROPERTY(EditAnywhere, Category="Sequence")
	TObjectPtr<UAnimSequence> FollowUpSequence = nullptr;

	UPROPERTY(EditAnywhere, Category="Sequence")
	bool bLoopFollowUpAnimation = false;

	UPROPERTY(EditAnywhere, Category = "Group")
	FGameplayTagContainer GroupTags;

	FFloatInterval GetEffectiveSamplingRange() const;
};

/** An blend space entry in a UPoseSearchDatabase. */
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchDatabaseBlendSpace
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	TObjectPtr<UBlendSpace> BlendSpace = nullptr;

	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	bool bLoopAnimation = false;

	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;

	// If to use the blendspace grid locations as parameter sample locations.
	// When enabled, NumberOfHorizontalSamples and NumberOfVerticalSamples are ignored.
	UPROPERTY(EditAnywhere, Category = "BlendSpace")
	bool bUseGridForSampling = true;

	UPROPERTY(EditAnywhere, Category = "BlendSpace", meta = (ClampMin = "1", UIMin = "1", UIMax = "25"))
	int32 NumberOfHorizontalSamples = 5;

	UPROPERTY(EditAnywhere, Category = "BlendSpace", meta = (ClampMin = "1", UIMin = "1", UIMax = "25"))
	int32 NumberOfVerticalSamples = 5;

	UPROPERTY(EditAnywhere, Category = "Group")
	FGameplayTagContainer GroupTags;

public:

	void GetBlendSpaceParameterSampleRanges(
		int32& HorizontalBlendNum,
		int32& VerticalBlendNum,
		float& HorizontalBlendMin,
		float& HorizontalBlendMax,
		float& VerticalBlendMin,
		float& VerticalBlendMax) const;
};

USTRUCT()
struct POSESEARCH_API FPoseSearchDatabaseGroup
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bUseGroupWeights = false;

	UPROPERTY(EditAnywhere, Category = "Settings", meta=(EditCondition="bUseGroupWeights", EditConditionHides))
	FPoseSearchWeightParams Weights;
};

/** A data asset for indexing a collection of animation sequences. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental, meta = (DisplayName = "Motion Database"))
class POSESEARCH_API UPoseSearchDatabase : public UDataAsset
{
	GENERATED_BODY()
public:
	// Motion Database Config asset to use with this database.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Database", DisplayName="Config")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchWeightParams DefaultWeights;

	// If there's a mirroring mismatch between the currently playing sequence and a search candidate, this cost will be 
	// added to the candidate, making it less likely to be selected
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Database")
	float MirroringMismatchCost = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY(EditAnywhere, Category = "Database")
	FPoseSearchBlockTransitionParameters BlockTransitionParameters = { 0.0f, 0.2f };

	UPROPERTY(EditAnywhere, Category = "Database")
	TArray<FPoseSearchDatabaseGroup> Groups;

	// Drag and drop animations here to add them in bulk to Sequences
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName="Drag And Drop Anims Here")
	TArray<TObjectPtr<UAnimSequence>> SimpleSequences;

	UPROPERTY(EditAnywhere, Category="Database")
	TArray<FPoseSearchDatabaseSequence> Sequences;

	// Drag and drop blendspaces here to add them in bulk to Blend Spaces
	UPROPERTY(EditAnywhere, Category = "Database", DisplayName = "Drag And Drop Blend Spaces Here")
	TArray<TObjectPtr<UBlendSpace>> SimpleBlendSpaces;

	UPROPERTY(EditAnywhere, Category = "Database")
	TArray<FPoseSearchDatabaseBlendSpace> BlendSpaces;

	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "64"))
	int32 NumberOfPrincipalComponents = 4;

	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "256"))
	int32 KDTreeMaxLeafSize = 8;
	
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", ClampMax = "600", UIMin = "1", UIMax = "600"))
	int32 KDTreeQueryNumNeighbors = 100;

	UPROPERTY(EditAnywhere, Category = "Performance")
	EPoseSearchMode PoseSearchMode = EPoseSearchMode::BruteForce;

	FPoseSearchIndex* GetSearchIndex();
	const FPoseSearchIndex* GetSearchIndex() const;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

	int32 GetPoseIndexFromTime(float AssetTime, const FPoseSearchIndexAsset* SearchIndexAsset) const;
	float GetAssetTime(int32 PoseIdx, const FPoseSearchIndexAsset* SearchIndexAsset = nullptr) const;

	const FPoseSearchDatabaseSequence& GetSequenceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FPoseSearchDatabaseBlendSpace& GetBlendSpaceSourceAsset(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const bool IsSourceAssetLooping(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FGameplayTagContainer* GetSourceAssetGroupTags(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	const FString GetSourceAssetName(const FPoseSearchIndexAsset* SearchIndexAsset) const;
	int32 GetNumberOfPrincipalComponents() const;

public: // UObject
	virtual void PostLoad() override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual bool IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform) override;
#endif

private:
	void CollectSimpleSequences();
	void CollectSimpleBlendSpaces();

public:
	// Populates the FPoseSearchIndex::Assets array by evaluating the data in the Sequences array
	bool TryInitSearchIndexAssets(FPoseSearchIndex& OutSearchIndex);

private:
	FPoseSearchDatabaseDerivedData* PrivateDerivedData;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnDerivedDataRebuildMulticaster);
	FOnDerivedDataRebuildMulticaster OnDerivedDataRebuild;

	DECLARE_MULTICAST_DELEGATE(FOnAssetChangeMulticaster);
	FOnDerivedDataRebuildMulticaster OnAssetChange;

	DECLARE_MULTICAST_DELEGATE(FOnGroupChangeMulticaster);
	FOnDerivedDataRebuildMulticaster OnGroupChange;
#endif // WITH_EDITOR

public:
#if WITH_EDITOR

	typedef FOnDerivedDataRebuildMulticaster::FDelegate FOnDerivedDataRebuild;
	void RegisterOnDerivedDataRebuild(const FOnDerivedDataRebuild& Delegate);
	void UnregisterOnDerivedDataRebuild(void* Unregister);
	void NotifyDerivedDataBuildStarted();

	typedef FOnAssetChangeMulticaster::FDelegate FOnAssetChange;
	void RegisterOnAssetChange(const FOnAssetChange& Delegate);
	void UnregisterOnAssetChange(void* Unregister);
	void NotifyAssetChange();

	typedef FOnGroupChangeMulticaster::FDelegate FOnGroupChange;
	void RegisterOnGroupChange(const FOnGroupChange& Delegate);
	void UnregisterOnGroupChange(void* Unregister);
	void NotifyGroupChange();

	void BeginCacheDerivedData();

	FIoHash GetSearchIndexHash() const;

	bool IsDerivedDataBuildPending() const;
#endif // WITH_EDITOR

	bool IsDerivedDataValid();
};


//////////////////////////////////////////////////////////////////////////
// Sequence metadata

/** Animation metadata object for indexing a single animation. */
UCLASS(BlueprintType, Category = "Animation|Pose Search", Experimental)
class POSESEARCH_API UPoseSearchSequenceMetaData : public UAnimMetaData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Settings")
	TObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FFloatInterval SamplingRange = FFloatInterval(0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "Settings")
	FPoseSearchExtrapolationParameters ExtrapolationParameters;

	UPROPERTY()
	FPoseSearchIndex SearchIndex;

	bool IsValidForIndexing() const;
	bool IsValidForSearch() const;

public: // UObject
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
};


//////////////////////////////////////////////////////////////////////////
// Feature vector reader and builder

namespace UE::PoseSearch {

/** Helper object for extracting features from a float buffer according to the feature vector layout. */
class POSESEARCH_API FFeatureVectorReader
{
public:
	void Init(const FPoseSearchFeatureVectorLayout* Layout);
	void SetValues(TArrayView<const float> Values);
	bool IsValid() const;

	bool GetTransform(FPoseSearchFeatureDesc Feature, FTransform* OutTransform) const;
	bool GetPosition(FPoseSearchFeatureDesc Feature, FVector* OutPosition) const;
	bool GetRotation(FPoseSearchFeatureDesc Feature, FQuat* OutRotation) const;
	bool GetForwardVector(FPoseSearchFeatureDesc Feature, FVector* OutForwardVector) const;
	bool GetLinearVelocity(FPoseSearchFeatureDesc Feature, FVector* OutLinearVelocity) const;
	bool GetAngularVelocity(FPoseSearchFeatureDesc Feature, FVector* OutAngularVelocity) const;
	bool GetVector(FPoseSearchFeatureDesc Feature, FVector* OutVector) const;

	const FPoseSearchFeatureVectorLayout* GetLayout() const { return Layout; }

private:
	const FPoseSearchFeatureVectorLayout* Layout = nullptr;
	TArrayView<const float> Values;
};

} // namespace UE::PoseSearch


/** 
* Helper object for writing features into a float buffer according to a feature vector layout.
* Keeps track of which features are present, allowing the feature vector to be built up piecemeal.
* FFeatureVectorBuilder is used to build search queries at runtime and for adding samples during search index construction.
*/
USTRUCT(BlueprintType, Category = "Animation|Pose Search")
struct POSESEARCH_API FPoseSearchFeatureVectorBuilder
{
	GENERATED_BODY()
public:
	void Init(const UPoseSearchSchema* Schema);
	void Reset();
	void ResetFeatures();

	const UPoseSearchSchema* GetSchema() const { return Schema.Get(); }

	TArrayView<const float> GetValues() const { return Values; }
	TArrayView<const float> GetNormalizedValues() const { return ValuesNormalized; }

	void SetTransform(FPoseSearchFeatureDesc Feature, const FTransform& Transform);
	void SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetTransformVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetPosition(FPoseSearchFeatureDesc Feature, const FVector& Translation);
	void SetRotation(FPoseSearchFeatureDesc Feature, const FQuat& Rotation);
	void SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetLinearVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetAngularVelocity(FPoseSearchFeatureDesc Feature, const FTransform& NextTransform, const FTransform& Transform, const FTransform& PrevTransform, float DeltaTime);
	void SetVector(FPoseSearchFeatureDesc Feature, const FVector& Vector);

	void CopyFromSearchIndex(const FPoseSearchIndex& SearchIndex, int32 PoseIdx);
	void CopyFeature(const FPoseSearchFeatureVectorBuilder& OtherBuilder, int32 FeatureIdx);

	void MergeReplace(const FPoseSearchFeatureVectorBuilder& OtherBuilder);

	bool IsInitialized() const;
	bool IsInitializedForSchema(const UPoseSearchSchema* Schema) const;
	bool IsComplete() const;
	bool IsCompatible(const FPoseSearchFeatureVectorBuilder& OtherBuilder) const;

	const TBitArray<>& GetFeaturesAdded() const;

	void Normalize(const FPoseSearchIndex& ForSearchIndex);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<const UPoseSearchSchema> Schema = nullptr;

	TArray<float> Values;
	TArray<float> ValuesNormalized;
	TBitArray<> FeaturesAdded;
	int32 NumFeaturesAdded = 0;
};



//////////////////////////////////////////////////////////////////////////
// Pose history

namespace UE::PoseSearch {

/**
* Records poses over time in a ring buffer.
* FFeatureVectorBuilder uses this to sample from the present or past poses according to the search schema.
*/
class POSESEARCH_API FPoseHistory
{
public:

	enum class ERootUpdateMode
	{
		RootMotionDelta,
		ComponentTransformDelta,
	};

	void Init(int32 InNumPoses, float InTimeHorizon);
	void Init(const FPoseHistory& History);
	bool TrySamplePose(float SecondsAgo, const FReferenceSkeleton& RefSkeleton, const TArray<FBoneIndexType>& RequiredBones);

	bool Update(
		float SecondsElapsed, 
		const FPoseContext& PoseContext, 
		FTransform ComponentTransform, 
		FText* OutError,
		ERootUpdateMode UpdateMode = ERootUpdateMode::RootMotionDelta);

	float GetSampleTimeInterval() const;
	TArrayView<const FTransform> GetLocalPoseSample() const { return SampledLocalPose; }
	TArrayView<const FTransform> GetComponentPoseSample() const { return SampledComponentPose; }
	TArrayView<const FTransform> GetPrevLocalPoseSample() const { return SampledPrevLocalPose; }
	TArrayView<const FTransform> GetPrevComponentPoseSample() const { return SampledPrevComponentPose; }
	const FTransform& GetRootTransformSample() const { return SampledRootTransform; }
	const FTransform& GetPrevRootTransformSample() const { return SampledPrevRootTransform; }
	float GetTimeHorizon() const { return TimeHorizon; }
	FPoseSearchFeatureVectorBuilder& GetQueryBuilder() { return QueryBuilder; }

private:
	bool TrySampleLocalPose(float Time, const TArray<FBoneIndexType>& RequiredBones, TArray<FTransform>& LocalPose, FTransform& RootTransform);

	struct FPose
	{
		FTransform RootTransform;
		TArray<FTransform> LocalTransforms;
	};

	TRingBuffer<FPose> Poses;
	TRingBuffer<float> Knots;
	TArray<FTransform> SampledLocalPose;
	TArray<FTransform> SampledComponentPose;
	TArray<FTransform> SampledPrevLocalPose;
	TArray<FTransform> SampledPrevComponentPose;
	FTransform SampledRootTransform;
	FTransform SampledPrevRootTransform;

	FPoseSearchFeatureVectorBuilder QueryBuilder;

	float TimeHorizon = 0.0f;
};

class IPoseHistoryProvider : public UE::Anim::IGraphMessage
{
	DECLARE_ANIMGRAPH_MESSAGE(IPoseHistoryProvider);

public:

	virtual const FPoseHistory& GetPoseHistory() const = 0;
	virtual FPoseHistory& GetPoseHistory() = 0;
};



//////////////////////////////////////////////////////////////////////////
// Debug visualization

enum class EDebugDrawFlags : uint32
{
	None = 0,

	// Draw the entire search index as a point cloud
	DrawSearchIndex = 1 << 0,

	/**
	 * Keep rendered data until the next call to FlushPersistentDebugLines().
	 * Combine with DrawSearchIndex to draw the search index only once.
	 */
	Persistent = 1 << 1,
	
	// Label samples with their indices
	DrawSampleLabels = 1 << 2,

	// Fade colors
	DrawSamplesWithColorGradient = 1 << 3,

	// Label bone names
	DrawBoneNames = 1 << 4,

	// Draws simpler shapes to improve performance
	DrawFast = 1 << 5,
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

struct POSESEARCH_API FDebugDrawParams
{
	const UWorld* World = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	const UPoseSearchSequenceMetaData* SequenceMetaData = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::DrawBoneNames;
	uint32 ChannelMask = (uint32)-1;

	float DefaultLifeTime = 5.0f;
	float PointSize = 1.0f;

	FTransform RootTransform = FTransform::Identity;

	// If set, draw the corresponding pose from the search index
	int32 PoseIdx = INDEX_NONE;

	// If set, draw using this uniform color instead of feature-based coloring
	const FLinearColor* Color = nullptr;

	// If set, interpret the buffer as a pose vector and draw it
	TArrayView<const float> PoseVector;

	// Optional prefix for sample labels
	FStringView LabelPrefix;

	bool CanDraw() const;
	const FPoseSearchIndex* GetSearchIndex() const;
	const UPoseSearchSchema* GetSchema() const;
};

struct FPoseCost
{
	float Dissimilarity = MAX_flt;
	float CostAddend = 0.0f;
	float TotalCost = MAX_flt;
	bool operator<(const FPoseCost& Other) const { return TotalCost < Other.TotalCost; }
	bool IsValid() const { return TotalCost != MAX_flt; }

};

/**
* Visualize pose search debug information
*
* @param DrawParams		Visualization options
*/
POSESEARCH_API void Draw(const FDebugDrawParams& DrawParams);



//////////////////////////////////////////////////////////////////////////
// Index building

/**
* Creates a pose search index for an animation sequence
*
* @param Sequence			The input sequence create a search index for
* @param SequenceMetaData	The input sequence indexing info and output search index
*
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(const UAnimSequence* Sequence, UPoseSearchSequenceMetaData* SequenceMetaData);


/**
* Creates a pose search index for a collection of animations
*
* @param Database	The input collection of animations and output search index
*
* @return Whether the index was built successfully
*/
POSESEARCH_API bool BuildIndex(UPoseSearchDatabase* Database, FPoseSearchIndex& OutSearchIndex);


//////////////////////////////////////////////////////////////////////////
// Query building

struct POSESEARCH_API FQueryBuildingContext
{
	FQueryBuildingContext(FPoseSearchFeatureVectorBuilder& InQuery) : Query(InQuery) {}

	FPoseSearchFeatureVectorBuilder& Query;

	const UPoseSearchSchema* Schema = nullptr;
	FPoseHistory* History = nullptr;
	const FTrajectorySampleRange* Trajectory = nullptr;

	bool IsInitialized () const;
};

bool POSESEARCH_API BuildQuery(FQueryBuildingContext& QueryBuildingContext);


//////////////////////////////////////////////////////////////////////////
// Search

struct POSESEARCH_API FSearchResult
{
	FPoseCost PoseCost;
	int32 PoseIdx = INDEX_NONE;
	const FPoseSearchIndexAsset* SearchIndexAsset = nullptr;
	float AssetTime = 0.0f;

	bool IsValid() const { return PoseIdx != INDEX_NONE; }
};

struct POSESEARCH_API FSearchContext
{
	TArrayView<const float> QueryValues;
	EPoseSearchBooleanRequest QueryMirrorRequest = EPoseSearchBooleanRequest::Indifferent;
	const FPoseSearchWeightsContext* WeightsContext = nullptr;
	const FGameplayTagQuery* DatabaseTagQuery = nullptr;
	FDebugDrawParams DebugDrawParams;

	void SetSource(const UPoseSearchDatabase* InSourceDatabase);
	void SetSource(const UAnimSequenceBase* InSourceSequence);
	const FPoseSearchIndex* GetSearchIndex() const;
	float GetMirrorMismatchCost() const;
	const UPoseSearchDatabase* GetSourceDatabase() { return SourceDatabase; }

private:
	const UPoseSearchDatabase* SourceDatabase = nullptr;
	const UAnimSequenceBase* SourceSequence = nullptr;
	const FPoseSearchIndex* SearchIndex = nullptr;
	float MirrorMismatchCost = 0.0f;
};


/**
* Performs a pose search on a UPoseSearchDatabase.
*
* @param SearchContext	Structure containing search parameters
* 
* @return The pose in the database that most closely matches the Query.
*/
POSESEARCH_API FSearchResult Search(FSearchContext& SearchContext);


//////////////////////////////////////////////////////////////////////////
// Pose comparison

/**
* Evaluate pose comparison metric between a pose in the search index and an input query
*
* @param PoseIdx			The index of the pose in the search index to compare to the query
* @param SearchContext		Structure containing search parameters
* @param GroupIdx			Indicates the group for this pose's source asset. Specify INDEX_NONE to lookup the pose's group.
*
* @return Dissimilarity between the two poses
*/

POSESEARCH_API FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, int32 GroupIdx);

/**
 * Cost details for pose analysis in the rewind debugger
 */
struct FPoseCostDetails
{
	FPoseCost PoseCost;

	// Contribution from ModifyCost anim notify
	float NotifyCostAddend = 0.0f;

	// Contribution from mirroring cost
	float MirrorMismatchCostAddend = 0.0f;

	// Cost breakdown per channel (e.g. pose cost, time-based trajectory cost, distance-based trajectory cost, etc.)
	TArray<float> ChannelCosts;

	// Difference vector computed as W*((P-Q)^2) without the cost modifier applied
	// Where P is the pose vector, Q is the query vector, W is the weights vector, and multiplication/exponentiation are element-wise operations
	TArray<float> CostVector;
};

/**
* Evaluate pose comparison metric between a pose in the search index and an input query with cost details
*
* @param SearchIndex		The search index containing the pose to compare to the query
* @param PoseIdx			The index of the pose in the search index to compare to the query
* @param SearchContext		Structure containing search parameters
* &param OutPoseCostDetails	Cost details for analysis in the debugger
*
* @return Dissimilarity between the two poses
*/
POSESEARCH_API FPoseCost ComparePoses(int32 PoseIdx, FSearchContext& SearchContext, FPoseCostDetails& OutPoseCostDetails);

} // namespace UE::PoseSearch