﻿// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Vulkan : ModuleRules
{
	public Vulkan(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			string RootPath = Target.UEThirdPartySourceDirectory + "Vulkan";

			PublicSystemIncludePaths.Add(RootPath + "/Include");
			PublicSystemIncludePaths.Add(RootPath + "/Include/vulkan");

			// Let's always delay load the vulkan dll as not everyone has it installed
			PublicDelayLoadDLLs.Add("vulkan-1.dll");
		}
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            string RootPath = Target.UEThirdPartySourceDirectory + "Vulkan";

            PublicSystemIncludePaths.Add(RootPath + "/Include");
            PublicSystemIncludePaths.Add(RootPath + "/Include/vulkan");
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) || Target.Platform == UnrealTargetPlatform.Lumin)
		{
			// no need to add the library, should be loaded via SDL
			string RootPath = Target.UEThirdPartySourceDirectory + "Vulkan";

			PublicSystemIncludePaths.Add(RootPath + "/Include");
			PublicSystemIncludePaths.Add(RootPath + "/Include/vulkan");
		}
	}
}
