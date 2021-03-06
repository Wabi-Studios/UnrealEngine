// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"

namespace ClothPaintToolCommands
{
	void RegisterClothPaintToolCommands();
}

class FClothPaintToolCommands_Gradient : public TCommands<FClothPaintToolCommands_Gradient>
{
public:

	FClothPaintToolCommands_Gradient()
		: TCommands<FClothPaintToolCommands_Gradient>(TEXT("ClothPainter"), NSLOCTEXT("Contexts", "ClothFillTool", "Cloth Painter - Fill Tool"), NAME_None, FAppStyle::GetAppStyleSetName())
	{

	}

	virtual void RegisterCommands() override;
	static const FClothPaintToolCommands_Gradient& Get();

	/** Applies the gradient when using the gradient cloth paint tool */
	TSharedPtr<FUICommandInfo> ApplyGradient;
};