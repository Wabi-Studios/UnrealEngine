// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChooserEditor : ModuleRules
	{
		public ChooserEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
				}
			);
			
			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
				}
			);
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					// ... add other public dependencies that you statically link with here ...
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AssetTools",
					"DataInterface",
					"DataInterfaceGraphEditor", // for DataInterfaceClassFilter, and Widget Factory system (could be moved to a more generic DataInterfaceEditor module
					"Chooser",
					"UnrealEd",
					"EditorWidgets",
					"SlateCore",
					"Slate",
					"PropertyEditor",
					"RigVM",
					"RigVMDeveloper",
					"ControlRig",
					"ControlRigDeveloper",
					"ControlRigEditor",
					"GraphEditor",
					"InputCore",
					"EditorStyle",
					// ... add private dependencies that you statically link with here ...
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
}