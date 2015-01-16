// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "BlueprintUtilities.h"
#include "Engine/InputDelegateBinding.h"
#include "Engine/TimelineTemplate.h"

#if WITH_EDITOR
#include "BlueprintEditorUtils.h"
#include "KismetEditorUtilities.h"
#endif //WITH_EDITOR


UBlueprintGeneratedClass::UBlueprintGeneratedClass(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumReplicatedProperties = 0;
}

void UBlueprintGeneratedClass::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// Default__BlueprintGeneratedClass uses its own AddReferencedObjects function.
		ClassAddReferencedObjects = &UBlueprintGeneratedClass::AddReferencedObjects;
	}
}

void UBlueprintGeneratedClass::PostLoad()
{
	Super::PostLoad();

	// Make sure the class CDO has been generated
	UObject* ClassCDO = GetDefaultObject();

	// Go through the CDO of the class, and make sure we don't have any legacy components that aren't instanced hanging on.
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(ClassCDO, SubObjects);

	for( auto SubObjIt = SubObjects.CreateIterator(); SubObjIt; ++SubObjIt )
	{
		UObject* CurrObj = *SubObjIt;
		if( !CurrObj->IsDefaultSubobject() && !CurrObj->IsRooted() )
		{
			CurrObj->MarkPendingKill();
		}
	}
#if WITH_EDITORONLY_DATA
	if (GetLinkerUE4Version() < VER_UE4_CLASS_NOTPLACEABLE_ADDED)
	{
		// Make sure the placeable flag is correct for all blueprints
		UBlueprint* Blueprint = Cast<UBlueprint>(ClassGeneratedBy);
		if (ensure(Blueprint) && Blueprint->BlueprintType != BPTYPE_MacroLibrary)
		{
			ClassFlags &= ~CLASS_NotPlaceable;
		}
	}

	if (const UPackage* Package = GetOutermost())
	{
		if (Package->PackageFlags & PKG_ForDiffing)
		{
			ClassFlags |= CLASS_Deprecated;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR

UClass* UBlueprintGeneratedClass::GetAuthoritativeClass()
{
	UBlueprint* GeneratingBP = CastChecked<UBlueprint>(ClassGeneratedBy);

	check(GeneratingBP);

	return (GeneratingBP->GeneratedClass != NULL) ? GeneratingBP->GeneratedClass : this;
}

bool FStructUtils::ArePropertiesTheSame(const UProperty* A, const UProperty* B, bool bCheckPropertiesNames)
{
	if (A == B)
	{
		return true;
	}

	if (!A != !B) //one of properties is null
	{
		return false;
	}

	if (bCheckPropertiesNames && (A->GetFName() != B->GetFName()))
	{
		return false;
	}

	if (A->GetSize() != B->GetSize())
	{
		return false;
	}

	if (A->GetOffset_ForGC() != B->GetOffset_ForGC())
	{
		return false;
	}

	if (!A->SameType(B))
	{
		return false;
	}

	return true;
}

bool FStructUtils::TheSameLayout(const UStruct* StructA, const UStruct* StructB, bool bCheckPropertiesNames)
{
	bool bResult = false;
	if (StructA && StructB)
	{
		const UProperty* PropertyA = StructA->PropertyLink;
		const UProperty* PropertyB = StructB->PropertyLink;

		bResult = true;
		while (bResult && (PropertyA != PropertyB))
		{
			bResult = ArePropertiesTheSame(PropertyA, PropertyB, bCheckPropertiesNames);
			PropertyA = PropertyA ? PropertyA->PropertyLinkNext : NULL;
			PropertyB = PropertyB ? PropertyB->PropertyLinkNext : NULL;
		}
	}
	return bResult;
}

struct FConditionalRecompileClassHepler
{
	enum ENeededAction
	{
		None,
		StaticLink,
		Recompile,
	};

	static bool HasTheSameLayoutAsParent(const UStruct* Struct)
	{
		const UStruct* Parent = Struct ? Struct->GetSuperStruct() : NULL;
		return FStructUtils::TheSameLayout(Struct, Parent);
	}

	static ENeededAction IsConditionalRecompilationNecessary(const UBlueprint* GeneratingBP)
	{
		if (FBlueprintEditorUtils::IsInterfaceBlueprint(GeneratingBP))
		{
			return ENeededAction::None;
		}

		if (FBlueprintEditorUtils::IsDataOnlyBlueprint(GeneratingBP))
		{
			// If my parent is native, my layout wasn't changed.
			const UClass* ParentClass = *GeneratingBP->ParentClass;
			if (!GeneratingBP->GeneratedClass || (GeneratingBP->GeneratedClass->GetSuperClass() != ParentClass))
			{
				return ENeededAction::Recompile;
			}

			if (ParentClass && ParentClass->HasAllClassFlags(CLASS_Native))
			{
				return ENeededAction::None;
			}

			if (HasTheSameLayoutAsParent(*GeneratingBP->GeneratedClass))
			{
				return ENeededAction::StaticLink;
			}
			else
			{
				UE_LOG(LogBlueprint, Log, TEXT("During ConditionalRecompilation the layout of DataOnly BP should not be changed. It will be handled, but it's bad for performence. Blueprint %s"), *GeneratingBP->GetName());
			}
		}

		return ENeededAction::Recompile;
	}
};

void UBlueprintGeneratedClass::ConditionalRecompileClass(TArray<UObject*>* ObjLoaded)
{
	UBlueprint* GeneratingBP = Cast<UBlueprint>(ClassGeneratedBy);
	if (GeneratingBP && (GeneratingBP->SkeletonGeneratedClass != this))
	{
		const auto NecessaryAction = FConditionalRecompileClassHepler::IsConditionalRecompilationNecessary(GeneratingBP);
		if (FConditionalRecompileClassHepler::Recompile == NecessaryAction)
		{
			const bool bWasRegenerating = GeneratingBP->bIsRegeneratingOnLoad;
			GeneratingBP->bIsRegeneratingOnLoad = true;

			// Make sure that nodes are up to date, so that we get any updated blueprint signatures
			FBlueprintEditorUtils::RefreshExternalBlueprintDependencyNodes(GeneratingBP);

			if ((GeneratingBP->Status != BS_Error) && (GeneratingBP->BlueprintType != EBlueprintType::BPTYPE_MacroLibrary))
			{
				FKismetEditorUtilities::RecompileBlueprintBytecode(GeneratingBP, ObjLoaded);
			}

			GeneratingBP->bIsRegeneratingOnLoad = bWasRegenerating;
		}
		else if (FConditionalRecompileClassHepler::StaticLink == NecessaryAction)
		{
			StaticLink(true);
			if (*GeneratingBP->SkeletonGeneratedClass)
			{
				GeneratingBP->SkeletonGeneratedClass->StaticLink(true);
			}
		}
	}
}
#endif //WITH_EDITOR

bool UBlueprintGeneratedClass::IsFunctionImplementedInBlueprint(FName InFunctionName) const
{
	UFunction* Function = FindFunctionByName(InFunctionName);
	return Function && Function->GetOuter() && Function->GetOuter()->IsA(UBlueprintGeneratedClass::StaticClass());
}

UDynamicBlueprintBinding* UBlueprintGeneratedClass::GetDynamicBindingObject(UClass* Class) const
{
	UDynamicBlueprintBinding* DynamicBindingObject = NULL;
	for (int32 Index = 0; Index < DynamicBindingObjects.Num(); ++Index)
	{
		if (DynamicBindingObjects[Index]->GetClass() == Class)
		{
			DynamicBindingObject = DynamicBindingObjects[Index];
			break;
		}
	}
	return DynamicBindingObject;
}

void UBlueprintGeneratedClass::BindDynamicDelegates(UObject* InInstance) const
{
	if(!InInstance->IsA(this))
	{
		UE_LOG(LogBlueprint, Warning, TEXT("BindComponentDelegates: '%s' is not an instance of '%s'."), *InInstance->GetName(), *GetName());
		return;
	}

	for (int32 Index = 0; Index < DynamicBindingObjects.Num(); ++Index)
	{
		if ( ensure(DynamicBindingObjects[Index] != NULL) )
		{
			DynamicBindingObjects[Index]->BindDynamicDelegates(InInstance);
		}
	}

	// call on super class, if it's a BlueprintGeneratedClass
	UBlueprintGeneratedClass* BGClass = Cast<UBlueprintGeneratedClass>(SuperStruct);
	if(BGClass != NULL)
	{
		BGClass->BindDynamicDelegates(InInstance);
	}
}

bool UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(const UClass* InClass, TArray<const UBlueprintGeneratedClass*>& OutBPGClasses)
{
	OutBPGClasses.Empty();
	bool bNoErrors = true;
	while(const UBlueprintGeneratedClass* BPGClass = Cast<const UBlueprintGeneratedClass>(InClass))
	{
#if WITH_EDITORONLY_DATA
		const UBlueprint* BP = Cast<const UBlueprint>(BPGClass->ClassGeneratedBy);
		bNoErrors &= (NULL != BP) && (BP->Status != BS_Error);
#endif
		OutBPGClasses.Add(BPGClass);
		InClass = BPGClass->GetSuperClass();
	}
	return bNoErrors;
}

UActorComponent* UBlueprintGeneratedClass::FindComponentTemplateByName(const FName& TemplateName)
{
	for(int32 i = 0; i < ComponentTemplates.Num(); i++)
	{
		UActorComponent* Template = ComponentTemplates[i];
		if(Template != NULL && Template->GetFName() == TemplateName)
		{
			return Template;
		}
	}

	return NULL;
}

void UBlueprintGeneratedClass::CreateComponentsForActor(AActor* Actor) const
{
	check(Actor != NULL);
	check(!Actor->IsTemplate());
	check(!Actor->IsPendingKill());

	// Iterate over each timeline template
	for(int32 i=0; i<Timelines.Num(); i++)
	{
		const UTimelineTemplate* TimelineTemplate = Timelines[i];

		// Not fatal if NULL, but shouldn't happen and ignored if not wired up in graph
		if(!TimelineTemplate||!TimelineTemplate->bValidatedAsWired)
		{
			continue;
		}

		FName NewName = *(FString::Printf(TEXT("TimelineComp__%d"), Actor->BlueprintCreatedComponents.Num() ) );
		UTimelineComponent* NewTimeline = NewNamedObject<UTimelineComponent>(Actor, NewName);
		NewTimeline->bCreatedByConstructionScript = true; // Indicate it comes from a blueprint so it gets cleared when we rerun construction scripts
		Actor->BlueprintCreatedComponents.Add(NewTimeline); // Add to array so it gets saved
		NewTimeline->SetNetAddressable();	// This component has a stable name that can be referenced for replication

		NewTimeline->SetPropertySetObject(Actor); // Set which object the timeline should drive properties on
		NewTimeline->SetDirectionPropertyName(TimelineTemplate->GetDirectionPropertyName());

		NewTimeline->SetTimelineLength(TimelineTemplate->TimelineLength); // copy length
		NewTimeline->SetTimelineLengthMode(TimelineTemplate->LengthMode);

		// Find property with the same name as the template and assign the new Timeline to it
		UClass* ActorClass = Actor->GetClass();
		UObjectPropertyBase* Prop = FindField<UObjectPropertyBase>( ActorClass, *UTimelineTemplate::TimelineTemplateNameToVariableName(TimelineTemplate->GetFName()) );
		if(Prop)
		{
			Prop->SetObjectPropertyValue_InContainer(Actor, NewTimeline);
		}

		// Event tracks
		// In the template there is a track for each function, but in the runtime Timeline each key has its own delegate, so we fold them together
		for(int32 TrackIdx=0; TrackIdx<TimelineTemplate->EventTracks.Num(); TrackIdx++)
		{
			const FTTEventTrack* EventTrackTemplate = &TimelineTemplate->EventTracks[TrackIdx];
			if(EventTrackTemplate->CurveKeys != NULL)
			{
				// Create delegate for all keys in this track
				FScriptDelegate EventDelegate;
				EventDelegate.BindUFunction(Actor, TimelineTemplate->GetEventTrackFunctionName(TrackIdx));

				// Create an entry in Events for each key of this track
				for (auto It(EventTrackTemplate->CurveKeys->FloatCurve.GetKeyIterator()); It; ++It)
				{
					NewTimeline->AddEvent(It->Time, FOnTimelineEvent(EventDelegate));
				}
			}
		}

		// Float tracks
		for(int32 TrackIdx=0; TrackIdx<TimelineTemplate->FloatTracks.Num(); TrackIdx++)
		{
			const FTTFloatTrack* FloatTrackTemplate = &TimelineTemplate->FloatTracks[TrackIdx];
			if(FloatTrackTemplate->CurveFloat != NULL)
			{
				NewTimeline->AddInterpFloat(FloatTrackTemplate->CurveFloat, FOnTimelineFloat(), TimelineTemplate->GetTrackPropertyName(FloatTrackTemplate->TrackName));
			}
		}

		// Vector tracks
		for(int32 TrackIdx=0; TrackIdx<TimelineTemplate->VectorTracks.Num(); TrackIdx++)
		{
			const FTTVectorTrack* VectorTrackTemplate = &TimelineTemplate->VectorTracks[TrackIdx];
			if(VectorTrackTemplate->CurveVector != NULL)
			{
				NewTimeline->AddInterpVector(VectorTrackTemplate->CurveVector, FOnTimelineVector(), TimelineTemplate->GetTrackPropertyName(VectorTrackTemplate->TrackName));
			}
		}

		// Linear color tracks
		for(int32 TrackIdx=0; TrackIdx<TimelineTemplate->LinearColorTracks.Num(); TrackIdx++)
		{
			const FTTLinearColorTrack* LinearColorTrackTemplate = &TimelineTemplate->LinearColorTracks[TrackIdx];
			if(LinearColorTrackTemplate->CurveLinearColor != NULL)
			{
				NewTimeline->AddInterpLinearColor(LinearColorTrackTemplate->CurveLinearColor, FOnTimelineLinearColor(), TimelineTemplate->GetTrackPropertyName(LinearColorTrackTemplate->TrackName));
			}
		}

		// Set up delegate that gets called after all properties are updated
		FScriptDelegate UpdateDelegate;
		UpdateDelegate.BindUFunction(Actor, TimelineTemplate->GetUpdateFunctionName());
		NewTimeline->SetTimelinePostUpdateFunc(FOnTimelineEvent(UpdateDelegate));

		// Set up finished delegate that gets called after all properties are updated
		FScriptDelegate FinishedDelegate;
		FinishedDelegate.BindUFunction(Actor, TimelineTemplate->GetFinishedFunctionName());
		NewTimeline->SetTimelineFinishedFunc(FOnTimelineEvent(FinishedDelegate));

		NewTimeline->RegisterComponent();

		// Start playing now, if desired
		if(TimelineTemplate->bAutoPlay)
		{
			// Needed for autoplay timelines in cooked builds, since they won't have Activate() called via the Play call below
			NewTimeline->bAutoActivate = true;
			NewTimeline->Play();
		}

		// Set to loop, if desired
		if(TimelineTemplate->bLoop)
		{
			NewTimeline->SetLooping(true);
		}

		// Set replication, if desired
		if(TimelineTemplate->bReplicated)
		{
			NewTimeline->SetIsReplicated(true);
		}
	}
}

uint8* UBlueprintGeneratedClass::GetPersistentUberGraphFrame(UObject* Obj, UFunction* FuncToCheck) const
{
	if (Obj && UsePersistentUberGraphFrame() && UberGraphFramePointerProperty && UberGraphFunction)
	{
		if (UberGraphFunction == FuncToCheck)
		{
			auto PointerToUberGraphFrame = UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(Obj);
			checkSlow(PointerToUberGraphFrame);
			ensure(PointerToUberGraphFrame->RawPointer);
			return PointerToUberGraphFrame->RawPointer;
		}
	}
	auto ParentClass = GetSuperClass();
	checkSlow(ParentClass);
	return ParentClass->GetPersistentUberGraphFrame(Obj, FuncToCheck);
}

void UBlueprintGeneratedClass::CreatePersistentUberGraphFrame(UObject* Obj) const
{
	checkSlow(!UberGraphFramePointerProperty == !UberGraphFunction);
	if (Obj && UsePersistentUberGraphFrame() && UberGraphFramePointerProperty && UberGraphFunction)
	{
		uint8* FrameMemory = NULL;
		const bool bUberGraphFunctionIsReady = !WITH_EDITOR // no cyclic dependency problems
			|| UberGraphFunction->HasAllFlags(RF_LoadCompleted); // is fully loaded
		if (bUberGraphFunctionIsReady)
		{
			FrameMemory = (uint8*)FMemory::Malloc(UberGraphFunction->GetStructureSize());
			FMemory::Memzero(FrameMemory, UberGraphFunction->GetStructureSize());
			for (UProperty* Property = UberGraphFunction->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				Property->InitializeValue_InContainer(FrameMemory);
			}
		}
		else
		{
			UE_LOG(LogBlueprint, Log, TEXT("Function '%s' is not ready to create frame."), *GetPathNameSafe(UberGraphFunction));
		}
		
		auto PointerToUberGraphFrame = UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(Obj);
		checkSlow(PointerToUberGraphFrame && !PointerToUberGraphFrame->RawPointer);
		PointerToUberGraphFrame->RawPointer = FrameMemory;
	}

	auto ParentClass = GetSuperClass();
	checkSlow(ParentClass);
	return ParentClass->CreatePersistentUberGraphFrame(Obj);
}

void UBlueprintGeneratedClass::DestroyPersistentUberGraphFrame(UObject* Obj) const
{
	checkSlow(!UberGraphFramePointerProperty == !UberGraphFunction);
	if (Obj && UsePersistentUberGraphFrame() && UberGraphFramePointerProperty && UberGraphFunction)
	{
		auto PointerToUberGraphFrame = UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(Obj);
		checkSlow(PointerToUberGraphFrame);
		auto FrameMemory = PointerToUberGraphFrame->RawPointer;
		PointerToUberGraphFrame->RawPointer = NULL;
		if (FrameMemory)
		{
			for (UProperty* Property = UberGraphFunction->PropertyLink; Property; Property = Property->PropertyLinkNext)
			{
				Property->DestroyValue_InContainer(FrameMemory);
			}
			FMemory::Free(FrameMemory);
		}
		else
		{
			UE_LOG(LogBlueprint, Log, TEXT("Object '%s' had no Uber Graph Persistent Frame"), *GetPathNameSafe(Obj));
		}
	}

	auto ParentClass = GetSuperClass();
	checkSlow(ParentClass);
	return ParentClass->DestroyPersistentUberGraphFrame(Obj);
}

void UBlueprintGeneratedClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	// Ensure that function netflags equate to any super function in a parent BP prior to linking; it may have been changed by the user
	// and won't be reflected in the child class until it is recompiled. Without this, UClass::Link() will assert if they are out of sync.
	for(UField* Field = Children; Field; Field = Field->Next)
	{
		Ar.Preload(Field);

		UFunction* Function = dynamic_cast<UFunction*>(Field);
		if(Function != nullptr)
		{
			UFunction* ParentFunction = Function->GetSuperFunction();
			if(ParentFunction != nullptr)
			{
				const uint32 ParentNetFlags = (ParentFunction->FunctionFlags & FUNC_NetFuncFlags);
				if(ParentNetFlags != (Function->FunctionFlags & FUNC_NetFuncFlags))
				{
					Function->FunctionFlags &= ~FUNC_NetFuncFlags;
					Function->FunctionFlags |= ParentNetFlags;
				}
			}
		}
	}

	Super::Link(Ar, bRelinkExistingProperties);

	if (UsePersistentUberGraphFrame() && UberGraphFunction)
	{
		for (auto Property : TFieldRange<UStructProperty>(this, EFieldIteratorFlags::ExcludeSuper))
		{
			if (Property->GetFName() == GetUberGraphFrameName())
			{
				UberGraphFramePointerProperty = Property;
				break;
			}
		}
		checkSlow(UberGraphFramePointerProperty);
	}
}

void UBlueprintGeneratedClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);

	UberGraphFramePointerProperty = NULL;
	UberGraphFunction = NULL;
}

void UBlueprintGeneratedClass::Bind()
{
	Super::Bind();

	if (UsePersistentUberGraphFrame() && UberGraphFunction)
	{
		ClassAddReferencedObjects = &UBlueprintGeneratedClass::AddReferencedObjectsInUbergraphFrame;
	}
}

void UBlueprintGeneratedClass::AddReferencedObjectsInUbergraphFrame(UObject* InThis, FReferenceCollector& Collector)
{
	checkSlow(InThis);
	for (UClass* CurrentClass = InThis->GetClass(); CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		if (auto BPGC = Cast<UBlueprintGeneratedClass>(CurrentClass))
		{
			if (BPGC->UberGraphFramePointerProperty)
			{
				checkSlow(BPGC->UberGraphFunction);
				auto PointerToUberGraphFrame = BPGC->UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(InThis);
				checkSlow(PointerToUberGraphFrame)
				if (PointerToUberGraphFrame->RawPointer)
				{
					FSimpleObjectReferenceCollectorArchive ObjectReferenceCollector(InThis, Collector);
					BPGC->UberGraphFunction->SerializeBin(ObjectReferenceCollector, PointerToUberGraphFrame->RawPointer, 0);
				}
			}
		}
		else if (CurrentClass->HasAllClassFlags(CLASS_Native))
		{
			CurrentClass->CallAddReferencedObjects(InThis, Collector);
			break;
		}
		else
		{
			checkSlow(false);
		}
	}
}

FName UBlueprintGeneratedClass::GetUberGraphFrameName()
{
	static const FName UberGraphFrameName(TEXT("UberGraphFrame"));
	return UberGraphFrameName;
}

bool UBlueprintGeneratedClass::UsePersistentUberGraphFrame()
{
#if USE_UBER_GRAPH_PERSISTENT_FRAME
	static const FBoolConfigValueHelper PersistentUberGraphFrame(TEXT("Kismet"), TEXT("bPersistentUberGraphFrame"), GEngineIni);
	return PersistentUberGraphFrame;
#else
	return false;
#endif
}