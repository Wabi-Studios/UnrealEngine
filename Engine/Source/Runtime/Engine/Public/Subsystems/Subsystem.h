// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Subsystem.generated.h"

class FSubsystemCollectionBase;

/** Subsystems are auto instanced classes that share the lifetime of certain engine constructs
 * 
 *	Currently supported Subsystem lifetimes are:
 *		GameInstance -> inherit UGameInstanceSubsystem
 *		LocalPlayer  -> inherit ULocalPlayerSubsystem
 *
 *
 *	Normal Example:
 *		class UMySystem : public UGameInstanceSubsystem
 *	Which can be accessed by:
 *		UGameInstance* GameInstance = ...;
 *		UMySystem* MySystem = GameInstance->GetSubsystem<UMySystem>();
 *
 *	or the following if you need protection from a null GameInstance
 *		UGameInstance* GameInstance = ...;
 *		UMyGameSubsystem* MySubsystem = UGameInstance::GetSubsystem<MyGameSubsystem>(GameInstance);
 *
 *
 *	You can get also define interfaces that can have multiple implementations.
 *	Interface Example :
 *      MySystemInterface
 *    With 2 concrete derivative classes:
 *      MyA : public MySystemInterface
 *      MyB : public MySystemInterface
 *
 *	Which can be accessed by:
 *		UGameInstance* GameInstance = ...;
 *		const TArray<UMyGameSubsystem*>& MySubsystems = GameInstance->GetSubsystemArray<MyGameSubsystem>();
 *
 */

UCLASS(Abstract)
class ENGINE_API USubsystem : public UObject
{
	GENERATED_BODY()

public:
	USubsystem();

	/** Override to control if the Subsystem should be created at all.
	 * For example you could only have your system created on servers.
	 * It's important to note that if using this is becomes very important to null check whenever getting the Subsystem.
	 *
	 * Note: This function is called on the CDO prior to instances being created!
	 *
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const { return true; }

	/** Implement this for initialization of instances of the system */
	virtual void Initialize(FSubsystemCollectionBase& Collection) {}

	/** Implement this for deinitialization of instances of the system */
	virtual void Deinitialize() {}

};
