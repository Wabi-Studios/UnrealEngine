// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealPak : ModuleRules
{
	public UnrealPak(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "PakFile", "Json", "Projects", "PakFileUtilities", "RSA", "ApplicationCore" });

		PrivateIncludePaths.Add("Runtime/Launch/Private");      // For LaunchEngineLoop.cpp include

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "Json"
        });
    }
}
