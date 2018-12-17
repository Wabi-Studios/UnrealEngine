// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AdvancedPreviewSceneCommands.h"

#define LOCTEXT_NAMESPACE "AdvancedPreviewSceneCommands"

void FAdvancedPreviewSceneCommands::RegisterCommands()
{
	UI_COMMAND(ToggleEnvironment, "Toggle Environment", "Toggles Environment visibility", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::I));
	UI_COMMAND(ToggleFloor, "Toggle Floor", "Toggles floor visibility", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::O));
	UI_COMMAND(TogglePostProcessing, "Toggle Post Processing", "Toggles whether Post Processing is enabled", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::P));
}

#undef LOCTEXT_NAMESPACE
