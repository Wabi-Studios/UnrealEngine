// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ReplicationGraph : ModuleRules
{
	public ReplicationGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(ModuleDirectory + "/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"NetCore",
				"Engine",
				"EngineSettings",
				"PerfCounters"
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Private"
			});     
		}
}
