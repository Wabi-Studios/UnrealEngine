// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class Blast : ModuleRules
{
	enum BlastLibraryMode
	{
		Debug,
		Profile,
		Checked,
		Shipping
	}

	BlastLibraryMode GetBlastLibraryMode(UnrealTargetConfiguration Config)
	{
		switch (Config)
		{
			case UnrealTargetConfiguration.Debug:
                if (Target.bDebugBuildsActuallyUseDebugCRT)
                {
                    return BlastLibraryMode.Debug;
                }
                else
                {
                    return BlastLibraryMode.Checked;
                }
			case UnrealTargetConfiguration.Shipping:
			case UnrealTargetConfiguration.Test:
				return BlastLibraryMode.Shipping;
			case UnrealTargetConfiguration.Development:
			case UnrealTargetConfiguration.DebugGame:
			case UnrealTargetConfiguration.Unknown:
			default:
                if(Target.bUseShippingPhysXLibraries)
                {
                    return BlastLibraryMode.Shipping;
                }
                else if (Target.bUseCheckedPhysXLibraries)
                {
                    return BlastLibraryMode.Checked;
                }
                else
                {
                    return BlastLibraryMode.Profile;
                }
		}
	}

	static string GetBlastLibraryConfiguration(BlastLibraryMode Mode)
	{
		switch (Mode)
		{
			case BlastLibraryMode.Debug:
				return "DEBUG";
			case BlastLibraryMode.Checked:
				return "CHECKED";
			case BlastLibraryMode.Profile:
				return "PROFILE";
            case BlastLibraryMode.Shipping:
            default:
				return "";	
		}
	}

	public Blast(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        // Determine which kind of libraries to link against
        BlastLibraryMode LibraryMode = GetBlastLibraryMode(Target.Configuration);
		string LibConfiguration = GetBlastLibraryConfiguration(LibraryMode);

        string BlastDir = ModuleDirectory;

        PublicIncludePaths.AddRange(
			new string[] {
                BlastDir + "/common",
                BlastDir + "/lowlevel/include",
                BlastDir + "/globals/include",
                BlastDir + "/extensions/physx/include",
				BlastDir + "/extensions/authoring/include",
            }
			);

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                            "Engine",
                            "PhysX",
            });


        List < string> BlastLibraries = new List<string>();
		BlastLibraries.AddRange(
			new string[]
			{
                 "NvBlast",
                 "NvBlastGlobals",
                 "NvBlastExtAuthoring"
            });

        string LibSuffix = null;

        // Libraries for windows platform
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibSuffix = "_x64.lib";

            string BlastLibDirFullPath = BlastDir + "/Lib/Win64/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
            PublicLibraryPaths.Add(BlastLibDirFullPath);
        }
        // TODO: Other configurations?

		if(LibSuffix != null)
        {
            foreach (string Lib in BlastLibraries)
            {
                PublicAdditionalLibraries.Add(String.Format("{0}{1}{2}", Lib, LibConfiguration, LibSuffix));
            }
        }
    }
}
