// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectTemplates/DatasmithObjectTemplate.h"

#include "DatasmithLandscapeTemplate.generated.h"

class UMaterialInterface;

UCLASS()
class DATASMITHCONTENT_API UDatasmithLandscapeTemplate : public UDatasmithObjectTemplate
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UMaterialInterface* LandscapeMaterial;

	UPROPERTY()
	int32 StaticLightingLOD;

	virtual void Apply(UObject* Destination, bool bForce = false) override;
	virtual void Load(const UObject* Source) override;
	virtual bool Equals(const UDatasmithObjectTemplate* Other) const override;
};