// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNode_SkeletalControlBase.h"
#include "AnimNode_TwoBoneIK.generated.h"

/**
 * Simple 2 Bone IK Controller.
 */

USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_TwoBoneIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()
	
	/** Name of bone to control. This is the main bone chain to modify from. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=IK)
	FBoneReference IKBone;

	/** Effector Location. Target Location to reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=EndEffector, meta=(PinShownByDefault))
	FVector EffectorLocation;

	/** Joint Target Location. Location used to orient Joint bone. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = JointTarget, meta=(PinShownByDefault))
	FVector JointTargetLocation;

	/** If EffectorLocationSpace is a bone, this is the bone to use. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=EndEffector)
	FName EffectorSpaceBoneName;

	/** Set end bone to use End Effector rotation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=EndEffector)
	uint32 bTakeRotationFromEffectorSpace:1;

	/** Keep local rotation of end bone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=EndEffector)
	uint32 bMaintainEffectorRelRot:1;

	/** Should stretching be allowed, to be prevent over extension */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=IK)
	uint32 bAllowStretching:1;

	/** Limits to use if stretching is allowed - old property DEPRECATED */
	UPROPERTY()
	FVector2D StretchLimits_DEPRECATED;

	/** Limits to use if stretching is allowed. This value determines when to start stretch. For example, 0.9 means once it reaches 90% of the whole length of the limb, it will start apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=IK, meta = (editcondition = "bAllowStretching", ClampMin = "0.0", UIMin = "0.0"))
	float StartStretchRatio;

	/** Limits to use if stretching is allowed. This value determins what is the max stretch scale. For example, 1.5 means it will stretch until 150 % of the whole length of the limb.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK, meta = (editcondition = "bAllowStretching", ClampMin = "0.0", UIMin = "0.0"))
	float MaxStretchScale;

	/** Reference frame of Effector Location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = IK)
	TEnumAsByte<enum EBoneControlSpace> EffectorLocationSpace;

	/** Reference frame of Joint Target Location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=JointTarget)
	TEnumAsByte<enum EBoneControlSpace> JointTargetLocationSpace;

	/** If JointTargetSpaceBoneName is a bone, this is the bone to use. **/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=JointTarget)
	FName JointTargetSpaceBoneName;

	FAnimNode_TwoBoneIK();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateBoneTransforms(USkeletalMeshComponent* SkelComp, FCSPose<FCompactPose>& MeshBases, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

private:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
};