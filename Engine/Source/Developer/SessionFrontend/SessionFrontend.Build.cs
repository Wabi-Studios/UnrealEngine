// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SessionFrontend : ModuleRules
{
	public SessionFrontend(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Slate",
                
			}
		);

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"DesktopPlatform",
				"ApplicationCore",
                "InputCore",
				"Json",
                "SessionServices",
				"SlateCore",

				// @todo gmp: remove these dependencies by making the session front-end extensible
				"AutomationWindow",
				"ScreenShotComparison",
				"ScreenShotComparisonTools",
				"TargetPlatform",
                "WorkspaceMenuStructure",
			}
		);

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.Add("Profiler");
		}

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
				"TargetDeviceServices",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Developer/SessionFrontend/Private",
				"Developer/SessionFrontend/Private/Models",
				"Developer/SessionFrontend/Private/Widgets",
				"Developer/SessionFrontend/Private/Widgets/Browser",
				"Developer/SessionFrontend/Private/Widgets/Console",
			}
		);
	}
}
