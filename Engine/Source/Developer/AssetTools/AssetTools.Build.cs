// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetTools : ModuleRules
{
	public AssetTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Developer/AssetTools/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"SlateCore",
				"EditorFramework",
				"UnrealEd",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "CurveAssetEditor",
				"Engine",
                "InputCore",
				"ApplicationCore",
				"Slate",
				"SourceControl",
				"PropertyEditor",
				"Kismet",
				"Landscape",
                "Foliage",
                "Projects",
				"RHI",
				"MaterialEditor",
				"ToolMenus",
				"PhysicsCore",
				"DeveloperSettings",
				"EngineSettings",
				"InterchangeCore",
				"InterchangeEngine",
				"PhysicsUtilities",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Analytics",
				"AssetRegistry",
				"ContentBrowser",
				"CollectionManager",
                "CurveAssetEditor",
				"DesktopPlatform",
				"EditorWidgets",
				"GameProjectGeneration",
                "PropertyEditor",
                "ActorPickerMode",
				"Kismet",
				"MainFrame",
				"MaterialEditor",
				"MessageLog",
				"PackagesDialog",
				"Persona",
				"FontEditor",
                "AudioEditor",
				"SourceControl",
				"Landscape",
                "SkeletonEditor",
                "SkeletalMeshEditor",
                "AnimationEditor",
                "AnimationBlueprintEditor",
                "AnimationModifiers",
			    "TextureEditor",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"ContentBrowser",
				"CollectionManager",
				"CurveTableEditor",
				"DataTableEditor",
				"DesktopPlatform",
				"EditorWidgets",
				"GameProjectGeneration",
                "ActorPickerMode",
				"MainFrame",
				"MessageLog",
				"PackagesDialog",
				"Persona",
				"FontEditor",
                "AudioEditor",
                "SkeletonEditor",
                "SkeletalMeshEditor",
                "AnimationEditor",
                "AnimationBlueprintEditor",
                "AnimationModifiers"
            }
		);
	}
}
