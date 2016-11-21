// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "FacialAnimationEditorPrivatePCH.h"
#include "FacialAnimationBulkImporterSettings.h"

void UFacialAnimationBulkImporterSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	SaveConfig();
}