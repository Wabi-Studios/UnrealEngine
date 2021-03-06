// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImportTestFunctions/AnimationImportTestFunctions.h"
#include "Engine/SkeletalMesh.h"


UClass* UAnimationImportTestFunctions::GetAssociatedAssetType() const
{
	return USkeletalMesh::StaticClass();
}
