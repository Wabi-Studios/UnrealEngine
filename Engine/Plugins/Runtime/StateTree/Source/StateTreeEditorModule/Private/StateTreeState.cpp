// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeState.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeDelegates.h"
#include "CoreMinimal.h"
#include "UObject/Field.h"

//////////////////////////////////////////////////////////////////////////
// FStateTreeStateLink

void FStateTreeStateLink::Set(const EStateTreeTransitionType InType, const UStateTreeState* InState)
{
	Type = InType;
	if (Type == EStateTreeTransitionType::GotoState)
	{
		check(InState);
		Name = InState->Name;
		ID = InState->ID;
	}
}


//////////////////////////////////////////////////////////////////////////
// FStateTreeTransition

FStateTreeTransition::FStateTreeTransition(const EStateTreeTransitionEvent InEvent, const EStateTreeTransitionType InType, const UStateTreeState* InState)
{
	Event = InEvent;
	State.Set(InType, InState);
}


//////////////////////////////////////////////////////////////////////////
// UStateTreeState

UStateTreeState::UStateTreeState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ID(FGuid::NewGuid())
{
	Parameters.ID = FGuid::NewGuid();
}

#if WITH_EDITOR
void UStateTreeState::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;
	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (Property)
	{
		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Name))
		{
			UStateTree* StateTree = GetTypedOuter<UStateTree>();
			if (ensure(StateTree))
			{
				UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Type))
		{
			// Remove any tasks and evaluators when they are not used.
			if (Type == EStateTreeStateType::Group || Type == EStateTreeStateType::Linked)
			{
				Tasks.Reset();
			}

			// If transitioning from linked state, reset the linked state.
			if (Type != EStateTreeStateType::Linked)
			{
				LinkedState = FStateTreeStateLink();
			}

			if (Type == EStateTreeStateType::Linked)
			{
				// Linked parameter layout is fixed, and copied from the linked target state.
				Parameters.bFixedLayout = true;
				UpdateParametersFromLinkedState();
			}
			else if (Type == EStateTreeStateType::Subtree)
			{
				// Subtree parameter layout can be edited
				Parameters.bFixedLayout = false;
			}
			else
			{
				Parameters.Reset();
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedState))
		{
			// When switching to new state, update the parameters.
			if (Type == EStateTreeStateType::Linked)
			{
				UpdateParametersFromLinkedState();
			}
		}

		if (Property->GetOwnerClass() == UStateTreeState::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters))
		{
			if (Type == EStateTreeStateType::Subtree)
			{
				// Broadcast subtree parameter edits so that the linked states can adapt.
				const UStateTree* StateTree = GetTypedOuter<UStateTree>();
				if (ensure(StateTree))
				{
					UE::StateTree::Delegates::OnStateParametersChanged.Broadcast(*StateTree, ID);
				}
			}
		}

		if (MemberProperty)
		{
			// Ensure unique ID on duplicated items.
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (Tasks.IsValidIndex(ArrayIndex))
					{
						if (FStateTreeTaskBase* Task = Tasks[ArrayIndex].Node.GetMutablePtr<FStateTreeTaskBase>())
						{
							Task->Name = FName(Task->Name.ToString() + TEXT(" Duplicate"));
						}
						Tasks[ArrayIndex].ID = FGuid::NewGuid();
					}
				}
				if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions))
				{
					const int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(MemberProperty->GetFName().ToString());
					if (EnterConditions.IsValidIndex(ArrayIndex))
					{
						EnterConditions[ArrayIndex].ID = FGuid::NewGuid();
					}
				}
				// TODO: Transition conditions.
				
			}
		}
	}
}

void UStateTreeState::PostLoad()
{
	Super::PostLoad();

	// Make sure state has transactional flags to make it work with undo (to fix a bug where root states were created without this flag).
	if (!HasAnyFlags(RF_Transactional))
	{
		SetFlags(RF_Transactional);
	}

	// Move deprecated evaluators to editor data.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (Evaluators_DEPRECATED.Num() > 0)
	{
		if (UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
		{
			TreeData->Evaluators.Append(Evaluators_DEPRECATED);
			Evaluators_DEPRECATED.Reset();
		}		
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UStateTreeState::UpdateParametersFromLinkedState()
{
	if (const UStateTreeEditorData* TreeData = GetTypedOuter<UStateTreeEditorData>())
	{
		if (const UStateTreeState* LinkTargetState = TreeData->GetStateByID(LinkedState.ID))
		{
			Parameters.Parameters.MigrateToNewBagInstance(LinkTargetState->Parameters.Parameters);
		}
	}
}

#endif

UStateTreeState* UStateTreeState::GetNextSiblingState() const
{
	if (!Parent)
	{
		return nullptr;
	}
	for (int32 ChildIdx = 0; ChildIdx < Parent->Children.Num(); ChildIdx++)
	{
		if (Parent->Children[ChildIdx] == this)
		{
			const int NextIdx = ChildIdx + 1;
			if (NextIdx < Parent->Children.Num())
			{
				return Parent->Children[NextIdx];
			}
			break;
		}
	}
	return nullptr;
}
