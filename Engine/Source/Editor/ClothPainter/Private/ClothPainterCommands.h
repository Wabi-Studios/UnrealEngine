// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"

class FClothPainterCommands : public TCommands<FClothPainterCommands>
{
public:
	FClothPainterCommands()
		: TCommands<FClothPainterCommands>(TEXT("ClothPainterTools"), NSLOCTEXT("Contexts", "ClothPainter", "Cloth Painter"), NAME_None, FAppStyle::GetAppStyleSetName())
	{

	}

	virtual void RegisterCommands() override;
	static const FClothPainterCommands& Get();

	/** Clothing commands */
	TSharedPtr<FUICommandInfo> TogglePaintMode;
};