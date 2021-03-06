// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureShareD3D12 : ModuleRules
{
	public TextureShareD3D12(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"Core",
							"Engine",
							"RenderCore",
							"RHI",
							"D3D12RHI",
						});

		// Allow D3D12 Cross GPU Heap resource API (experimental)
		PublicDefinitions.Add("TEXTURESHARE_CROSSGPUHEAP=0");

		///////////////////////////////////////////////////////////////
		// Platform specific defines
		///////////////////////////////////////////////////////////////
		if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
		}
	}
}
