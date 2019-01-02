// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OculusHMD_Settings.h"

#if OCULUS_HMD_SUPPORTED_PLATFORMS

namespace OculusHMD
{

//-------------------------------------------------------------------------------------------------
// FSettings
//-------------------------------------------------------------------------------------------------

FSettings::FSettings() :
	BaseOffset(0, 0, 0)
	, BaseOrientation(FQuat::Identity)
	, PixelDensity(1.0f)
	, PixelDensityMin(0.5f)
	, PixelDensityMax(1.0f)
	, SystemHeadset(ovrpSystemHeadset_None)
{
	Flags.Raw = 0;
	Flags.bHMDEnabled = true;
	Flags.bChromaAbCorrectionEnabled = true;
	Flags.bUpdateOnRT = true;
	Flags.bHQBuffer = false;
	Flags.bDirectMultiview = true;
	Flags.bIsUsingDirectMultiview = false;
#if PLATFORM_ANDROID
	Flags.bCompositeDepth = false;
#else
	Flags.bCompositeDepth = true;
#endif
	Flags.bSupportsDash = false;
	EyeRenderViewport[0] = EyeRenderViewport[1] = EyeRenderViewport[2] = FIntRect(0, 0, 0, 0);

	RenderTargetSize = FIntPoint(0, 0);
}

TSharedPtr<FSettings, ESPMode::ThreadSafe> FSettings::Clone() const
{
	TSharedPtr<FSettings, ESPMode::ThreadSafe> NewSettings = MakeShareable(new FSettings(*this));
	return NewSettings;
}

void FSettings::SetPixelDensity(float NewPixelDensity)
{
	if (Flags.bPixelDensityAdaptive)
	{
		PixelDensity = FMath::Clamp(NewPixelDensity, PixelDensityMin, PixelDensityMax);
	}
	else
	{
		PixelDensity = FMath::Clamp(NewPixelDensity, ClampPixelDensityMin, ClampPixelDensityMax);
	}
}

void FSettings::SetPixelDensityMin(float NewPixelDensityMin)
{
	PixelDensityMin = FMath::Clamp(NewPixelDensityMin, ClampPixelDensityMin, ClampPixelDensityMax);
	PixelDensityMax = FMath::Max(PixelDensityMin, PixelDensityMax);
	SetPixelDensity(PixelDensity);
}

void FSettings::SetPixelDensityMax(float NewPixelDensityMax)
{
	PixelDensityMax = FMath::Clamp(NewPixelDensityMax, ClampPixelDensityMin, ClampPixelDensityMax);
	PixelDensityMin = FMath::Min(PixelDensityMin, PixelDensityMax);
	SetPixelDensity(PixelDensity);
}


} // namespace OculusHMD

#endif //OCULUS_HMD_SUPPORTED_PLATFORMS
