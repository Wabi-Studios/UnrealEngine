// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigUnit.generated.h"

struct FRigUnitContext;

/** 
 * Current state of execution type
 */
UENUM()
enum class EUnitExecutionType : uint8
{
	Always,
	InEditingTime, // in control rig editor
	Disable, // disable completely - good for debugging
	Max UMETA(Hidden),
};

/** Base class for all rig units */
USTRUCT(BlueprintType, meta=(Abstract))
struct CONTROLRIG_API FRigUnit
{
	GENERATED_BODY()

	FRigUnit()
		:ExecutionType(EUnitExecutionType::Always)
	{}

	/** Virtual destructor */
	virtual ~FRigUnit() {}

	/** Execute logic for this rig unit */
	virtual void Execute(const FRigUnitContext& InContext) {}

	/* 
	 * This is property name given by ControlRig as transient when initialized, so only available in run-time
	 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, transient, Category=FRigUnit)
	FName RigUnitName;
	
	/* 
	 * This is struct name given by ControlRig as transient when initialized, so only available in run-time
	 */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, transient, Category=FRigUnit)
	FName RigUnitStructName;

	UPROPERTY(EditAnywhere, Category = FRigUnit)
	EUnitExecutionType ExecutionType;
};

// this will have to change in the future and move to editor, I assume the errors will be saved in the rig unit and it will print fromthe editor module
namespace UnitLogHelpers
{
	CONTROLRIG_API void PrintMissingHierarchy(const FName& InputName);
	CONTROLRIG_API void PrintUnimplemented(const FName& InputName);
}
 