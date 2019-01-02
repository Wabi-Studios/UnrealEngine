// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Gauntlet : ModuleRules
	{
		public Gauntlet(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
				});

			PublicIncludePaths.AddRange(
				new string[]
				{
				}
			);
		}
	}
}