// Copyright (c) Microsoft Corporation. All rights reserved.

using System;
using System.IO;
using UnrealBuildTool;
using Microsoft.Win32;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealitySpatialInput : ModuleRules
	{
		private string ModulePath
		{
			get { return ModuleDirectory; }
		}
	 
		private string ThirdPartyPath
		{
			get { return Path.GetFullPath( Path.Combine( ModulePath, "../ThirdParty/" ) ); }
		}
		
        public WindowsMixedRealitySpatialInput(ReadOnlyTargetRules Target) : base(Target)
        {	
            PrivateIncludePathModuleNames.AddRange(
                new string[]
				{
					"TargetPlatform",
                    "InputDevice",
					"HeadMountedDisplay",
					"WindowsMixedRealityHMD"
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"MixedRealityInteropLibrary",
					"HeadMountedDisplay",
					"ProceduralMeshComponent"
                }
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "InputCore",
                    "HeadMountedDisplay",
                    "WindowsMixedRealityHMD",
					"Slate",
					"SlateCore",
				});

            AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

            if (Target.bBuildEditor == true)
            {
				PrivateDependencyModuleNames.Add("EditorFramework");
                PrivateDependencyModuleNames.Add("UnrealEd");
            }

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {	
                bEnableExceptions = true;
            }

			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("WindowsMixedRealityInputSimulation");
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=1");
			}
			else
			{
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=0");
			}
		}
	}
}