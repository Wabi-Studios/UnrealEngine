// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FAdvancedPreviewSceneCommands : public TCommands<FAdvancedPreviewSceneCommands>
{

public:
	FAdvancedPreviewSceneCommands() : TCommands<FAdvancedPreviewSceneCommands>
	(
		"AdvancedPreviewScene",
		NSLOCTEXT("Contexts", "AdvancedPreviewScene", "Advanced Preview Scene"),
		NAME_None,
		FAppStyle::Get().GetStyleSetName()
	)
	{}
	
	/** Toggles environment (sky sphere) visibility */
	TSharedPtr< FUICommandInfo > ToggleEnvironment;

	/** Toggles floor visibility */
	TSharedPtr< FUICommandInfo > ToggleFloor;

	/** Toggles post processing */
	TSharedPtr< FUICommandInfo > TogglePostProcessing;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};
