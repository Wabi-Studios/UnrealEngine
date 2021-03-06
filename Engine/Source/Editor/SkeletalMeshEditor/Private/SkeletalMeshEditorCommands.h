// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FSkeletalMeshEditorCommands : public TCommands<FSkeletalMeshEditorCommands>
{
public:
	FSkeletalMeshEditorCommands()
		: TCommands<FSkeletalMeshEditorCommands>(TEXT("SkeletalMeshEditor"), NSLOCTEXT("Contexts", "SkeletalMeshEditor", "Skeletal Mesh Editor"), NAME_None, FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:

	// reimport current mesh
	TSharedPtr<FUICommandInfo> ReimportMesh;
	TSharedPtr<FUICommandInfo> ReimportMeshWithNewFile;

	// reimport current mesh
	TSharedPtr<FUICommandInfo> ReimportAllMesh;
	TSharedPtr<FUICommandInfo> ReimportAllMeshWithNewFile;

	// bake materials for this skeletal mesh
	TSharedPtr<FUICommandInfo> BakeMaterials;
};
