// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorTests : ModuleRules
{
	public EditorTests(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
		);


		PrivateIncludePaths.AddRange(
			new string[] {
				"EditorTests/Private",
				// ... add other private include paths required here ...
			}
		);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"ApplicationCore",
				"InputCore",
				"LevelEditor",
				"CoreUObject",
                "RenderCore",
				"Engine",
                "NavigationSystem",
                "Slate",
				"SlateCore",
				"AssetTools",
				"MainFrame",
				"MaterialEditor",
				"JsonUtilities",
				"Analytics",
				"ContentBrowser",
				
				"SourceControl",
				"RHI",
				"BlueprintGraph",
				"AddContentDialog",
				"GraphEditor",
				"DirectoryWatcher",
				"Projects",
				"EditorFramework",
				"UnrealEd",
				"AudioEditor",
				"AnimGraphRuntime",
				"MeshMergeUtilities",
				"MaterialBaking",
                "MeshDescription",
				"StaticMeshDescription",
                "MeshBuilder",
                "RawMesh",
				"AutomationController",
				//"SubobjectDataInterface",
			}
		);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}
