// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "VisualLogger/VisualLogger.h"
#include "AIController.h"

UBTTask_RunBehavior::UBTTask_RunBehavior(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NodeName = "Run Behavior";
}

EBTNodeResult::Type UBTTask_RunBehavior::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UE_CVLOG(BehaviorAsset == nullptr, OwnerComp.GetAIOwner(), LogBehaviorTree, Error, TEXT("\'%s\' is missing BehaviorAsset!"), *GetNodeName());

	const bool bPushed = BehaviorAsset != nullptr && OwnerComp.PushInstance(*BehaviorAsset);
	if (bPushed && OwnerComp.InstanceStack.Num() > 0)
	{
		FBehaviorTreeInstance& MyInstance = OwnerComp.InstanceStack[OwnerComp.InstanceStack.Num() - 1];
		MyInstance.DeactivationNotify.BindUObject(this, &UBTTask_RunBehavior::OnSubtreeDeactivated);
		// unbinding is not required, MyInstance will be destroyed after firing that delegate (usually by UBehaviorTreeComponent::ProcessPendingExecution) 

		return EBTNodeResult::InProgress;
	}

	return EBTNodeResult::Failed;
}

void UBTTask_RunBehavior::OnSubtreeDeactivated(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type NodeResult)
{
	const int32 MyInstanceIdx = OwnerComp.FindInstanceContainingNode(this);
	uint8* NodeMemory = OwnerComp.GetNodeMemory(this, MyInstanceIdx);

	UE_VLOG(OwnerComp.GetOwner(), LogBehaviorTree, Verbose, TEXT("OnSubtreeDeactivated: %s (result: %s)"),
		*UBehaviorTreeTypes::DescribeNodeHelper(this), *UBehaviorTreeTypes::DescribeNodeResult(NodeResult));

	OnTaskFinished(OwnerComp, NodeMemory, NodeResult);
}

FString UBTTask_RunBehavior::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s: %s"), *Super::GetStaticDescription(), *GetNameSafe(BehaviorAsset));
}

#if WITH_EDITOR

FName UBTTask_RunBehavior::GetNodeIconName() const
{
	return FName("BTEditor.Graph.BTNode.Task.RunBehavior.Icon");
}

#endif	// WITH_EDITOR
