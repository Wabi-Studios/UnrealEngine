// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

#include "Factories/Factory.h"

#include "DataLayerFactory.generated.h"

UCLASS(hidecategories = Object, MinimalAPI)
class UDataLayerFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// Begin UFactory Interface	
};