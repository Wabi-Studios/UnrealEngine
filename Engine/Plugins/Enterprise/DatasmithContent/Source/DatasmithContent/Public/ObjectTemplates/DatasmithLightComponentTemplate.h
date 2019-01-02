// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectTemplates/DatasmithObjectTemplate.h"

#include "DatasmithLightComponentTemplate.generated.h"

UCLASS()
class DATASMITHCONTENT_API UDatasmithLightComponentTemplate : public UDatasmithObjectTemplate
{
	GENERATED_BODY()

public:
	UDatasmithLightComponentTemplate();

	UPROPERTY()
	uint8 bVisible : 1;

	UPROPERTY()
	uint32 CastShadows : 1;

	UPROPERTY()
	uint32 bUseTemperature : 1;

	UPROPERTY()
	uint32 bUseIESBrightness : 1;

	UPROPERTY()
	float Intensity;

	UPROPERTY()
	float Temperature;

	UPROPERTY()
	float IESBrightnessScale;

	UPROPERTY()
	FLinearColor LightColor;

	UPROPERTY()
	class UMaterialInterface* LightFunctionMaterial;

	UPROPERTY()
	class UTextureLightProfile* IESTexture;

	virtual void Apply( UObject* Destination, bool bForce = false ) override;
	virtual void Load( const UObject* Source ) override;
	virtual bool Equals( const UDatasmithObjectTemplate* Other ) const override;
};
