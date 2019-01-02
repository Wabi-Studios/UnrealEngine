// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyPath : ModuleRules
{
	public PropertyPath(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] 
		{ 
			"Core",
			"CoreUObject" 
		});
	}
}
