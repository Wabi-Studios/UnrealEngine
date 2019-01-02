// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine/RectLight.h"
#include "Components/RectLightComponent.h"

ARectLight::ARectLight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URectLightComponent>(TEXT("LightComponent0")))
{
	RectLightComponent = CastChecked<URectLightComponent>(GetLightComponent());
	RectLightComponent->Mobility = EComponentMobility::Stationary;

	RootComponent = RectLightComponent;
}

void ARectLight::PostLoad()
{
	Super::PostLoad();

	if (GetLightComponent()->Mobility == EComponentMobility::Static)
	{
		GetLightComponent()->LightFunctionMaterial = NULL;
	}
}

#if WITH_EDITOR
void ARectLight::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	const FVector ModifiedScale = DeltaScale * ( AActor::bUsePercentageBasedScaling ? 10000.0f : 100.0f );

	FMath::ApplyScaleToFloat( RectLightComponent->AttenuationRadius, ModifiedScale, 1.0f );

	PostEditChange();
}
#endif

