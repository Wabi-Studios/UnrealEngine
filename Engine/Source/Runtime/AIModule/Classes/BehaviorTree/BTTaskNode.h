// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BehaviorTree/BTNode.h"
#include "GameplayTaskOwnerInterface.h"
#include "BTTaskNode.generated.h"

class UBehaviorTreeComponent;
struct FAIMessage;

/** 
 * Task are leaf nodes of behavior tree, which perform actual actions
 *
 * Because some of them can be instanced for specific AI, following virtual functions are not marked as const:
 *  - ExecuteTask
 *  - AbortTask
 *  - TickTask
 *  - OnMessage
 *
 * If your node is not being instanced (default behavior), DO NOT change any properties of object within those functions!
 * Template nodes are shared across all behavior tree components using the same tree asset and must store
 * their runtime properties in provided NodeMemory block (allocation size determined by GetInstanceMemorySize() )
 *
 */

UCLASS(Abstract)
class AIMODULE_API UBTTaskNode : public UBTNode, public IGameplayTaskOwnerInterface
{
	GENERATED_UCLASS_BODY()

	/** starts this task, should return Succeeded, Failed or InProgress
	 *  (use FinishLatentTask() when returning InProgress)
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

protected:
	/** aborts this task, should return Aborted or InProgress
	 *  (use FinishLatentAbort() when returning InProgress)
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

public:
#if WITH_EDITOR
	virtual FName GetNodeIconName() const override;
#endif // WITH_EDITOR

	/** message observer's hook */
	void ReceivedMessage(UBrainComponent* BrainComp, const FAIMessage& Message);

	/** wrapper for node instancing: ExecuteTask */
	EBTNodeResult::Type WrappedExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: AbortTask */
	EBTNodeResult::Type WrappedAbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const;

	/** wrapper for node instancing: TickTask */
	void WrappedTickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) const;

	/** wrapper for node instancing: OnTaskFinished */
	void WrappedOnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) const;

	/** helper function: finish latent executing */
	void FinishLatentTask(UBehaviorTreeComponent& OwnerComp, EBTNodeResult::Type TaskResult) const;

	/** helper function: finishes latent aborting */
	void FinishLatentAbort(UBehaviorTreeComponent& OwnerComp) const;

protected:

	/** if set, TickTask will be called */
	uint8 bNotifyTick : 1;

	/** if set, OnTaskFinished will be called */
	uint8 bNotifyTaskFinished : 1;

	/** set to true if task owns any GameplayTasks. Note this requires tasks to be created via NewBTAITask
	 *	Otherwise specific BT task node class is responsible for ending the gameplay tasks on node finish */
	uint8 bOwnsGameplayTasks : 1;

	/** ticks this task 
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds);

	/** message handler, default implementation will finish latent execution/abortion
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	virtual void OnMessage(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, FName Message, int32 RequestID, bool bSuccess);

	/** called when task execution is finished
	 * this function should be considered as const (don't modify state of object) if node is not instanced! */
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult);

	/** register message observer */
	void WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType) const;
	void WaitForMessage(UBehaviorTreeComponent& OwnerComp, FName MessageType, int32 RequestID) const;
	
	/** unregister message observers */
	void StopWaitingForMessages(UBehaviorTreeComponent& OwnerComp) const;

	template <class T>
	T* NewBTAITask(UBehaviorTreeComponent& BTComponent)
	{
		T* NewAITask = NewObject<T>();
		AAIController* AIController = BTComponent.GetAIOwner();
		check(AIController && "Can\'t spawn an AI task without AI controller!");
		NewAITask->InitAITask(*AIController, *this, GetDefaultPriority());
		bOwnsGameplayTasks = true;
		
		return NewAITask;
	}

	//----------------------------------------------------------------------//
	// IGameplayTaskOwnerInterface
	//----------------------------------------------------------------------//
	virtual UGameplayTasksComponent* GetGameplayTasksComponent(const UGameplayTask& Task) const override;
	virtual void OnTaskActivated(UGameplayTask& Task) override;
	virtual void OnTaskDeactivated(UGameplayTask& Task) override;
	virtual void OnTaskInitialized(UGameplayTask& Task) override;
	virtual AActor* GetOwnerActor(const UGameplayTask* Task) const;
	virtual AActor* GetAvatarActor(const UGameplayTask* Task) const;
	virtual uint8 GetDefaultPriority() const;

	UBehaviorTreeComponent* GetBTComponentForTask(UGameplayTask& Task) const;

	//----------------------------------------------------------------------//
	// DEPRECATED
	//----------------------------------------------------------------------//
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	EBTNodeResult::Type WrappedExecuteTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	EBTNodeResult::Type WrappedAbortTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void WrappedTickTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, float DeltaSeconds) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void WrappedOnTaskFinished(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void FinishLatentTask(UBehaviorTreeComponent* OwnerComp, EBTNodeResult::Type TaskResult) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void FinishLatentAbort(UBehaviorTreeComponent* OwnerComp) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory);
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory);
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	virtual void TickTask(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, float DeltaSeconds);
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	virtual void OnMessage(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, FName Message, int32 RequestID, bool bSuccess);
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	virtual void OnTaskFinished(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult);
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void WaitForMessage(UBehaviorTreeComponent* OwnerComp, FName MessageType) const;
	DEPRECATED(4.7, "This version is deprecated. Please use the one taking reference to UBehaviorTreeComponent rather than a pointer.")
	void WaitForMessage(UBehaviorTreeComponent* OwnerComp, FName MessageType, int32 RequestID) const;
};
