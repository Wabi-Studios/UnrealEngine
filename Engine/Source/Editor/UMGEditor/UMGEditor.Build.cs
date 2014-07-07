// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UMGEditor : ModuleRules
{
	public UMGEditor(TargetInfo Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/UMGEditor/Private", // For PCH includes (because they don't work with relative paths, yet)
				"Editor/UMGEditor/Private/Templates",
				"Editor/UMGEditor/Private/Extensions",
				"Editor/UMGEditor/Private/Customizations",
				"Editor/UMGEditor/Private/BlueprintModes",
				"Editor/UMGEditor/Private/TabFactory",
				"Editor/UMGEditor/Private/Designer",
				"Editor/UMGEditor/Private/Hierarchy",
				"Editor/UMGEditor/Private/Palette",
				"Editor/UMGEditor/Private/Details",
				"Editor/UMGEditor/Private/DragDrop",
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"UMG",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"InputCore",
				"Slate",
				"Engine",
				"AssetTools",
				"UnrealEd", // for FAssetEditorManager
				"KismetWidgets",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"Kismet",  // for FWorkflowCentricApplication
				"PropertyEditor",
				"UMG",
				"EditorStyle",
				"Slate",
				"SlateCore",
				"MovieSceneCore",
				"Sequencer",
			}
			);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
				"AssetTools",
				"UMG",
				"Sequencer",
			}
			);
	}
}
