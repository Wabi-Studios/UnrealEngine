// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Templates/WidgetTemplateClass.h"

class UWidgetTree;

/**
 * A template for classes generated by UTexture or UMaterial classes, or implements a USlateTextureAtlasInterface
 */

class UMGEDITOR_API FWidgetTemplateImageClass : public FWidgetTemplateClass
{

public:
	/**
	 * Constructor.
	 * @param	InAssetData		The asset data used to create the widget
	 */
	FWidgetTemplateImageClass(const FAssetData& InAssetData);

	virtual ~FWidgetTemplateImageClass();

	/** Creates an instance of the widget for the tree */
	virtual UWidget* Create(UWidgetTree* WidgetTree) override;

	/** Returns true if the supplied class is supported by this template */
	static bool Supports(UClass* InClass);
};
