// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_CustomProperty.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedInputPose.h"

FAnimNode_CustomProperty::FAnimNode_CustomProperty()
	: FAnimNode_Base()
	, TargetInstance(nullptr)
#if WITH_EDITOR
	, bReinitializeProperties(false)
#endif // WITH_EDITOR
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FAnimNode_CustomProperty::HandleObjectsReplaced);
#endif // WITH_EDITOR
}

FAnimNode_CustomProperty::~FAnimNode_CustomProperty()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
#endif // WITH_EDITOR
}

void FAnimNode_CustomProperty::SetTargetInstance(UObject* InInstance)
{
	TargetInstance = InInstance;
}

void FAnimNode_CustomProperty::PropagateInputProperties(const UObject* InSourceInstance)
{
	if(TargetInstance)
	{
		// First copy properties
		check(SourceProperties.Num() == DestProperties.Num());
		for(int32 PropIdx = 0; PropIdx < SourceProperties.Num(); ++PropIdx)
		{
			FProperty* CallerProperty = SourceProperties[PropIdx];
			FProperty* SubProperty = DestProperties[PropIdx];

			if(CallerProperty && SubProperty)
			{
#if WITH_EDITOR
				if (ensure(CallerProperty->SameType(SubProperty)))
#endif
				{
					const uint8* SrcPtr = CallerProperty->ContainerPtrToValuePtr<uint8>(InSourceInstance);
					uint8* DestPtr = SubProperty->ContainerPtrToValuePtr<uint8>(TargetInstance);

					CallerProperty->CopyCompleteValue(DestPtr, SrcPtr);
				}
			}
		}
	}
}

void FAnimNode_CustomProperty::PreUpdate(const UAnimInstance* InAnimInstance) 
{
	FAnimNode_Base::PreUpdate(InAnimInstance);

#if WITH_EDITOR
	if (bReinitializeProperties)
	{
		InitializeProperties(InAnimInstance, GetTargetClass());
		bReinitializeProperties = false;
	}
#endif// WITH_EDITOR
}

void FAnimNode_CustomProperty::InitializeProperties(const UObject* InSourceInstance, UClass* InTargetClass)
{
	if(InTargetClass)
	{
		UClass* SourceClass = InSourceInstance->GetClass();

		// Build property lists
		SourceProperties.Reset(SourcePropertyNames.Num());
		DestProperties.Reset(SourcePropertyNames.Num());

		check(SourcePropertyNames.Num() == DestPropertyNames.Num());

		for(int32 Idx = 0; Idx < SourcePropertyNames.Num(); ++Idx)
		{
			const FName& DestName = DestPropertyNames[Idx];

			if (FProperty* DestProperty = FindFProperty<FProperty>(InTargetClass, DestName))
			{
				const FName& SourceName = SourcePropertyNames[Idx];
				FProperty* SourceProperty = FindFProperty<FProperty>(SourceClass, SourceName);

				if (SourceProperty
#if WITH_EDITOR
					// This type check can fail when anim blueprints are in an error state:
					&& SourceProperty->SameType(DestProperty)
#endif
					)
				{
					SourceProperties.Add(SourceProperty);
					DestProperties.Add(DestProperty);
				}
			}
		}
	}
}

#if WITH_EDITOR
void FAnimNode_CustomProperty::HandleObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap)
{
	if (UObject* ThisTargetInstance = GetTargetInstance<UObject>())
	{
		UObject* const* ReinstancedTarget = OldToNewInstanceMap.Find(ThisTargetInstance);
		if (ReinstancedTarget)
		{
			// recache the properties
			bReinitializeProperties = true;
		}
	}
}
#endif	// #if WITH_EDITOR
