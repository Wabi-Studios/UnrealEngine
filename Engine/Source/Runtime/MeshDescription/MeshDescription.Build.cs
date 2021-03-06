// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MeshDescription : ModuleRules
	{
		public MeshDescription(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateIncludePaths.Add("Runtime/MeshDescription/Private");
            PublicIncludePaths.Add("Runtime/MeshDescription/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject"
				}
			);

			if (Target.bBuildWithEditorOnlyData)
			{
				PrivateDependencyModuleNames.Add("DerivedDataCache");
			}
		}
	}
}
