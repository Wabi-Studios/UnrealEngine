// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorUtils.h"

#include "AssetTypeCategories.h"
#include "UObject/NoExportTypes.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "Engine/Blueprint.h"

bool PCGEditorUtils::IsAssetPCGBlueprint(const FAssetData& InAssetData)
{
	FString InNativeParentClassName = InAssetData.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
	FString TargetNativeParentClassName = UPCGBlueprintElement::GetParentClassName();

	return InAssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName() && InNativeParentClassName == TargetNativeParentClassName;
}