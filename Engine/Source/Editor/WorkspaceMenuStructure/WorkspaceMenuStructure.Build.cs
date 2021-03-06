// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WorkspaceMenuStructure : ModuleRules
{
	public WorkspaceMenuStructure(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Editor/WorkspaceMenuStructure/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"SlateCore",
				"Slate"
			}
		);
	}
}
