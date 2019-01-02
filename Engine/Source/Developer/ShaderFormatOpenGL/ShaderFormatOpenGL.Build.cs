// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ShaderFormatOpenGL : ModuleRules
{
	public ShaderFormatOpenGL(ReadOnlyTargetRules Target) : base(Target)
	{

		PrivateIncludePathModuleNames.Add("TargetPlatform");

		PrivateIncludePaths.Add("Runtime/OpenGLDrv/Private");
		PrivateIncludePaths.Add("Runtime/OpenGLDrv/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"RenderCore",
				"ShaderCompilerCommon",
				"ShaderPreprocessor"
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, 
			"OpenGL",
			"HLSLCC"
			);

        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
        }
	}
}
