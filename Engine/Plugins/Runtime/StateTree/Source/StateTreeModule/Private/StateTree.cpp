// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeLinker.h"
#include "StateTreeNodeBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "AssetRegistry/AssetData.h"

const FGuid FStateTreeCustomVersion::GUID(0x28E21331, 0x501F4723, 0x8110FA64, 0xEA10DA1E);
FCustomVersionRegistration GRegisterStateTreeCustomVersion(FStateTreeCustomVersion::GUID, FStateTreeCustomVersion::LatestVersion, TEXT("StateTreeAsset"));

bool UStateTree::IsReadyToRun() const
{
	// Valid tree must have at least one state and valid instance data.
	return States.Num() > 0 && bIsLinked;
}

#if WITH_EDITOR
void UStateTree::ResetCompiled()
{
	Schema = nullptr;
	States.Reset();
	Transitions.Reset();
	Nodes.Reset();
	DefaultInstanceData.Reset();
	SharedInstanceData.Reset();
	NamedExternalDataDescs.Reset();
	PropertyBindings.Reset();
	Parameters.Reset();

	ParametersDataViewIndex = FStateTreeIndex8::Invalid;
	
	EvaluatorsBegin = 0;
	EvaluatorsNum = 0;

	ResetLinked();
}

void UStateTree::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	const FString SchemaClassName = Schema ? Schema->GetClass()->GetPathName() : TEXT("");
	OutTags.Add(FAssetRegistryTag(UE::StateTree::SchemaTag, SchemaClassName, FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}

void UStateTree::PostLoadAssetRegistryTags(const FAssetData& InAssetData, TArray<FAssetRegistryTag>& OutTagsAndValuesToUpdate) const
{
	Super::PostLoadAssetRegistryTags(InAssetData, OutTagsAndValuesToUpdate);

	static const FName SchemaTag(TEXT("Schema"));
	FString SchemaTagValue = InAssetData.GetTagValueRef<FString>(SchemaTag);
	if (!SchemaTagValue.IsEmpty() && FPackageName::IsShortPackageName(SchemaTagValue))
	{
		FTopLevelAssetPath SchemaTagClassPathName = UClass::TryConvertShortTypeNameToPathName<UStruct>(SchemaTagValue, ELogVerbosity::Warning, TEXT("UStateTree::PostLoadAssetRegistryTags"));
		if (!SchemaTagClassPathName.IsNull())
		{
			OutTagsAndValuesToUpdate.Add(FAssetRegistryTag(SchemaTag, SchemaTagClassPathName.ToString(), FAssetRegistryTag::TT_Alphabetical));
		}
	}
}

#endif // WITH_EDITOR

void UStateTree::PostLoad()
{
	Super::PostLoad();

	const int32 CurrentVersion = GetLinkerCustomVersion(FStateTreeCustomVersion::GUID);

	if (CurrentVersion < FStateTreeCustomVersion::LatestVersion)
	{
#if WITH_EDITOR
		ResetCompiled();
#endif
		UE_LOG(LogStateTree, Error, TEXT("%s: StateTree compiled data in older format. Please recompile the StateTree asset."), *GetName());
		return;
	}
	
	if (!Link())
	{
		UE_LOG(LogStateTree, Error, TEXT("%s failed to link. Asset will not be usable at runtime."), *GetName());	
	}
}

void UStateTree::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FStateTreeCustomVersion::GUID);
	
	// We need to link and rebind property bindings each time a BP is compiled,
	// because property bindings may get invalid, and instance data potentially needs refreshed.
	if (Record.GetUnderlyingArchive().IsModifyingWeakAndStrongReferences())
	{
		if (!Link())
		{
			UE_LOG(LogStateTree, Error, TEXT("%s failed to link. Asset will not be usable at runtime."), *GetName());	
		}
	}
}

void UStateTree::ResetLinked()
{
	bIsLinked = false;
	ExternalDataDescs.Reset();
	ExternalDataBaseIndex = 0;
	NumDataViews = 0;
}

bool UStateTree::Link()
{
	// Initialize the instance data default value.
	// This data will be used to allocate runtime instance on all StateTree users.
	ResetLinked();

	if (!DefaultInstanceData.IsValid())
	{
		UE_LOG(LogStateTree, Error, TEXT("%s: StartTree does not have instance data. Please recompile the StateTree asset."), *GetName());
		return false;
	}
	
	// Update property bag structs before resolving binding.
	const TArrayView<FStateTreeBindableStructDesc> SourceStructs = PropertyBindings.GetSourceStructs();
	const TArrayView<FStateTreePropCopyBatch> CopyBatches = PropertyBindings.GetCopyBatches();

	if (ParametersDataViewIndex.IsValid() && SourceStructs.IsValidIndex(ParametersDataViewIndex.Get()))
	{
		SourceStructs[ParametersDataViewIndex.Get()].Struct = Parameters.GetPropertyBagStruct();
	}
	
	for (const FCompactStateTreeState& State : States)
	{
		if (State.Type == EStateTreeStateType::Subtree)
		{
			if (State.ParameterInstanceIndex.IsValid() == false
				|| State.ParameterDataViewIndex.IsValid() == false)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: Data for state '%s' is malformed. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString());
				return false;
			}

			// Subtree is a bind source, update bag struct.
			const FCompactStateTreeParameters& Params = DefaultInstanceData.GetMutableStruct(State.ParameterInstanceIndex.Get()).GetMutable<FCompactStateTreeParameters>();
			FStateTreeBindableStructDesc& Desc = SourceStructs[State.ParameterDataViewIndex.Get()];
			Desc.Struct = Params.Parameters.GetPropertyBagStruct();
		}
		else if (State.Type == EStateTreeStateType::Linked && State.LinkedState.IsValid())
		{
			const FCompactStateTreeState& LinkedState = States[State.LinkedState.Index];

			if (State.ParameterInstanceIndex.IsValid() == false
				|| LinkedState.ParameterInstanceIndex.IsValid() == false)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: Data for state '%s' is malformed. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString());
				return false;
			}

			const FCompactStateTreeParameters& Params = DefaultInstanceData.GetMutableStruct(State.ParameterInstanceIndex.Get()).GetMutable<FCompactStateTreeParameters>();

			// Check that the bag in linked state matches.
			const FCompactStateTreeParameters& LinkedStateParams = DefaultInstanceData.GetMutableStruct(LinkedState.ParameterInstanceIndex.Get()).GetMutable<FCompactStateTreeParameters>();

			if (LinkedStateParams.Parameters.GetPropertyBagStruct() != Params.Parameters.GetPropertyBagStruct())
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: The parameters on state '%s' does not match the linked state parameters in state '%s'. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString(), *LinkedState.Name.ToString());
				return false;
			}

			FStateTreePropCopyBatch& Batch = CopyBatches[Params.BindingsBatch.Get()];
			Batch.TargetStruct.Struct = Params.Parameters.GetPropertyBagStruct();
		}
	}
	
	// Resolves property paths used by bindings a store property pointers
	if (!PropertyBindings.ResolvePaths())
	{
		return false;
	}

	// Resolves nodes references to other StateTree data
	FStateTreeLinker Linker(Schema);
	Linker.SetExternalDataBaseIndex(PropertyBindings.GetSourceStructNum());
	
	for (int32 Index = 0; Index < Nodes.Num(); Index++)
	{
		const FStructView Node = Nodes[Index];
		if (FStateTreeNodeBase* NodePtr = Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			Linker.SetCurrentInstanceDataType(NodePtr->GetInstanceDataType(), NodePtr->DataViewIndex.Get());
			const bool bLinkSucceeded = NodePtr->Link(Linker);
			if (!bLinkSucceeded || Linker.GetStatus() == EStateTreeLinkerStatus::Failed)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: node '%s' failed to resolve its references."), *GetName(), *NodePtr->StaticStruct()->GetName());
				return false;
			}
		}
	}

	// Link succeeded, setup tree to be ready to run
	ExternalDataBaseIndex = PropertyBindings.GetSourceStructNum();
	ExternalDataDescs = Linker.GetExternalDataDescs();
	NumDataViews = ExternalDataBaseIndex + ExternalDataDescs.Num();

	bIsLinked = true;
	
	return true;
}

#if WITH_EDITOR

void FStateTreeMemoryUsage::AddUsage(FConstStructView View)
{
	if (const UScriptStruct* ScriptStruct = View.GetScriptStruct())
	{
		EstimatedMemoryUsage = Align(EstimatedMemoryUsage, ScriptStruct->GetMinAlignment());
		EstimatedMemoryUsage += ScriptStruct->GetStructureSize();
	}
}

void FStateTreeMemoryUsage::AddUsage(const UObject* Object)
{
	if (Object != nullptr)
	{
		check(Object->GetClass());
		EstimatedMemoryUsage += Object->GetClass()->GetStructureSize();
	}
}

TArray<FStateTreeMemoryUsage> UStateTree::CalculateEstimatedMemoryUsage() const
{
	TArray<FStateTreeMemoryUsage> MemoryUsages;
	TArray<TPair<int32, int32>> StateLinks;

	if (States.IsEmpty() || !Nodes.IsValid() || !DefaultInstanceData.IsValid())
	{
		return MemoryUsages;
	}

	const int32 TreeMemUsageIndex = MemoryUsages.Emplace(TEXT("State Tree Max"));
	const int32 EvalMemUsageIndex = MemoryUsages.Emplace(TEXT("Evaluators"));
	const int32 SharedMemUsageIndex = MemoryUsages.Emplace(TEXT("Shared Data"));

	auto GetRootStateHandle = [this](const FStateTreeStateHandle InState) -> FStateTreeStateHandle
	{
		FStateTreeStateHandle Result = InState;
		while (Result.IsValid() && States[Result.Index].Parent.IsValid())
		{
			Result = States[Result.Index].Parent;
		}
		return Result;		
	};

	auto GetUsageIndexForState = [&MemoryUsages, this](const FStateTreeStateHandle InStateHandle) -> int32
	{
		check(InStateHandle.IsValid());
		
		const int32 FoundMemUsage = MemoryUsages.IndexOfByPredicate([InStateHandle](const FStateTreeMemoryUsage& MemUsage) { return MemUsage.Handle == InStateHandle; });
		if (FoundMemUsage != INDEX_NONE)
		{
			return FoundMemUsage;
		}

		const FCompactStateTreeState& CompactState = States[InStateHandle.Index];
		
		return MemoryUsages.Emplace(TEXT("State ") + CompactState.Name.ToString(), InStateHandle);
	};

	for (int32 Index = 0; Index < States.Num(); Index++)
	{
		const FStateTreeStateHandle StateHandle((uint16)Index);
		const FCompactStateTreeState& CompactState = States[Index];
		const FStateTreeStateHandle ParentHandle = GetRootStateHandle(StateHandle);
		const int32 ParentUsageIndex = GetUsageIndexForState(ParentHandle);
		FStateTreeMemoryUsage& MemUsage = MemoryUsages[ParentUsageIndex];

		MemUsage.NodeCount += CompactState.TasksNum;

		if (CompactState.Type == EStateTreeStateType::Linked)
		{
			const int32 LinkedUsageIndex = GetUsageIndexForState(CompactState.LinkedState);
			StateLinks.Emplace(ParentUsageIndex, LinkedUsageIndex);

			MemUsage.NodeCount++;
			MemUsage.AddUsage(DefaultInstanceData.GetStruct(CompactState.ParameterInstanceIndex.Get()));
		}
		
		for (int32 TaskIndex = CompactState.TasksBegin; TaskIndex < (CompactState.TasksBegin + CompactState.TasksNum); TaskIndex++)
		{
			const FStateTreeTaskBase& Task = Nodes[TaskIndex].Get<FStateTreeTaskBase>();
			if (Task.bInstanceIsObject == false)
			{
				MemUsage.NodeCount++;
				MemUsage.AddUsage(DefaultInstanceData.GetStruct(Task.InstanceIndex.Get()));
			}
			else
			{
				MemUsage.NodeCount++;
				MemUsage.AddUsage(DefaultInstanceData.GetMutableObject(Task.InstanceIndex.Get()));
			}
		}
	}

	// Accumulate linked states.
	for (int32 Index = StateLinks.Num() - 1; Index >= 0; Index--)
	{
		FStateTreeMemoryUsage& ParentUsage = MemoryUsages[StateLinks[Index].Get<0>()];
		const FStateTreeMemoryUsage& LinkedUsage = MemoryUsages[StateLinks[Index].Get<1>()];
		const int32 LinkedTotalUsage = LinkedUsage.EstimatedMemoryUsage + LinkedUsage.EstimatedChildMemoryUsage;
		if (LinkedTotalUsage > ParentUsage.EstimatedChildMemoryUsage)
		{
			ParentUsage.EstimatedChildMemoryUsage = LinkedTotalUsage;
			ParentUsage.ChildNodeCount = LinkedUsage.NodeCount + LinkedUsage.ChildNodeCount;
		}
	}

	// Evaluators
	FStateTreeMemoryUsage& EvalMemUsage = MemoryUsages[EvalMemUsageIndex];
	for (int32 EvalIndex = EvaluatorsBegin; EvalIndex < (EvaluatorsBegin + EvaluatorsNum); EvalIndex++)
	{
		const FStateTreeEvaluatorBase& Eval = Nodes[EvalIndex].Get<FStateTreeEvaluatorBase>();
		if (Eval.bInstanceIsObject == false)
		{
			EvalMemUsage.AddUsage(DefaultInstanceData.GetMutableStruct(Eval.InstanceIndex.Get()));
		}
		else
		{
			EvalMemUsage.AddUsage(DefaultInstanceData.GetMutableObject(Eval.InstanceIndex.Get()));
		}
	}

	// Estimate highest combined usage.
	FStateTreeMemoryUsage& TreeMemUsage = MemoryUsages[TreeMemUsageIndex];

	// Exec state
	TreeMemUsage.AddUsage(DefaultInstanceData.GetStruct(0));
	TreeMemUsage.NodeCount++;

	TreeMemUsage.EstimatedMemoryUsage += EvalMemUsage.EstimatedMemoryUsage;
	TreeMemUsage.NodeCount += EvalMemUsage.NodeCount;

	// FStateTreeInstanceData overhead.
	TreeMemUsage.EstimatedMemoryUsage += sizeof(FStateTreeInstanceData);
	// FInstancedStructArray overhead.
	TreeMemUsage.EstimatedMemoryUsage += TreeMemUsage.NodeCount * 16;
	
	int32 MaxSubtreeUsage = 0;
	int32 MaxSubtreeNodeCount = 0;
	
	for (const FStateTreeMemoryUsage& MemUsage : MemoryUsages)
	{
		if (MemUsage.Handle.IsValid())
		{
			const int32 TotalUsage = MemUsage.EstimatedMemoryUsage + MemUsage.EstimatedChildMemoryUsage;
			if (TotalUsage > MaxSubtreeUsage)
			{
				MaxSubtreeUsage = TotalUsage;
				MaxSubtreeNodeCount = MemUsage.NodeCount + MemUsage.ChildNodeCount;
			}
		}
	}

	TreeMemUsage.EstimatedMemoryUsage += MaxSubtreeUsage;
	TreeMemUsage.NodeCount += MaxSubtreeNodeCount;

	if (SharedInstanceData.IsValid())
	{
		FStateTreeMemoryUsage& SharedMemUsage = MemoryUsages[SharedMemUsageIndex];
		SharedMemUsage.NodeCount = SharedInstanceData.GetNumItems();
		SharedMemUsage.EstimatedMemoryUsage = SharedInstanceData.GetEstimatedMemoryUsage();
	}

	return MemoryUsages;
}
#endif // WITH_EDITOR
