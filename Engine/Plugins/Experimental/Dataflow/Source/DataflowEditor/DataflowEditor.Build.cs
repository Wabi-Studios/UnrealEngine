// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataflowEditor : ModuleRules
	{
        public DataflowEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("DataflowEditor/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				    "Slate",
				    "SlateCore",
				    "Engine",
					"EditorFramework",
					"UnrealEd",
				    "PropertyEditor",
				    "RenderCore",
				    "RHI",
				    "AssetTools",
				    "AssetRegistry",
				    "SceneOutliner",
					"EditorStyle",
					"AssetTools",
					"ToolMenus",
					"LevelEditor",
					"InputCore",
					"AdvancedPreviewScene",
					"GraphEditor",
					"DataflowCore",
					"DataflowEngine",
					"Slate",
				}
			);
		}
	}
}
