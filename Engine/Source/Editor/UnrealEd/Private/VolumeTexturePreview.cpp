// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	VolumeTexturePreview.h: Implementation for previewing Volume textures.
==============================================================================*/

#include "VolumeTexturePreview.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "SimpleElementShaders.h"
#include "ShaderParameterUtils.h"
#include "PipelineStateCache.h"
#include "Editor.h"

UNREALED_API void GetBestFitForNumberOfTiles(int32 InSize, int32& OutNumTilesX, int32& OutNumTilesY)
{
	const float Ratios[] = { 1.f, 1.2f, 1.25f, 1.33f, 1.5f, 1.77f, 2.f, 3.f };

	OutNumTilesX = InSize;
	OutNumTilesY = 1;

	int32 OutError = InSize;

	for (float Ratio : Ratios)
	{
		int32 NumTilesY = (int32)FMath::RoundToInt(FMath::Sqrt((float)InSize / Ratio));
		int32 NumTilesX = (int32)FMath::RoundToInt((float)NumTilesY * Ratio);
		int32 Error = NumTilesX * NumTilesY - InSize;

		if (Error >= 0 && Error < OutError)
		{
			OutError = Error;
			OutNumTilesX = NumTilesX;
			OutNumTilesY = NumTilesY;
		}
	}
}

/*------------------------------------------------------------------------------
	Batched element shaders for previewing 2d textures.
------------------------------------------------------------------------------*/
/**
 * Simple pixel shader for previewing volume textures at a specified mip level
 */
class FSimpleElementVolumeTexturePreviewPS : public FGlobalShader
{
public:

	FSimpleElementVolumeTexturePreviewPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		InTexture.Bind(Initializer.ParameterMap,TEXT("InTexture"), SPF_Mandatory);
		InTextureSampler.Bind(Initializer.ParameterMap,TEXT("InTextureSampler"));	
		BadTexture.Bind(Initializer.ParameterMap,TEXT("BadTexture"));
		BadTextureSampler.Bind(Initializer.ParameterMap,TEXT("BadTextureSampler"));	
		TextureComponentReplicate.Bind(Initializer.ParameterMap,TEXT("TextureComponentReplicate"));
		TextureComponentReplicateAlpha.Bind(Initializer.ParameterMap,TEXT("TextureComponentReplicateAlpha"));
		ColorWeights.Bind(Initializer.ParameterMap,TEXT("ColorWeights"));
		PackedParameters.Bind(Initializer.ParameterMap,TEXT("PackedParams"));
		NumTilesPerSideParameter.Bind(Initializer.ParameterMap,TEXT("NumTilesPerSide"));
		TraceVolumeScalingParameter.Bind(Initializer.ParameterMap,TEXT("TraceVolumeScaling"));
		TextureDimensionParameter.Bind(Initializer.ParameterMap,TEXT("TextureDimension"));
		TraceViewMatrixParameter.Bind(Initializer.ParameterMap,TEXT("TraceViewMatrix"));
	}

	FSimpleElementVolumeTexturePreviewPS() {}

	/** Should the shader be cached? Always. */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4) && !IsConsolePlatform(Parameters.Platform);
	}
	
	void SetParameters(FRHICommandList& RHICmdList, const FTexture* TextureValue, int32 SizeZ, const FMatrix& ColorWeightsValue, float GammaValue, float MipLevel, float Opacity, const FRotator& TraceOrientation)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), InTexture, InTextureSampler, TextureValue);
		if (GEditor && GEditor->Bad)
		{
			SetTextureParameter(RHICmdList, GetPixelShader(), BadTexture, BadTextureSampler, GEditor->Bad->Resource);
		}
		else
		{
			SetTextureParameter(RHICmdList, GetPixelShader(), BadTexture, GWhiteTexture->TextureRHI);
		}
		SetShaderValue(RHICmdList, GetPixelShader(),ColorWeights,ColorWeightsValue);

		const int32 MipSizeZ = FMath::Max<int32>(SizeZ >> FMath::FloorToInt(MipLevel), 1);
		FVector4 PackedParametersValue(GammaValue, MipLevel, (float)MipSizeZ, Opacity);
		SetShaderValue(RHICmdList, GetPixelShader(), PackedParameters, PackedParametersValue);

		int32 NumTilesX = 0;
		int32 NumTilesY = 0;
		GetBestFitForNumberOfTiles(MipSizeZ, NumTilesX, NumTilesY);
		SetShaderValue(RHICmdList, GetPixelShader(), NumTilesPerSideParameter, FVector4((float)NumTilesX, (float)NumTilesY, 0 ,0));

		SetShaderValue(RHICmdList, GetPixelShader(),TextureComponentReplicate,TextureValue->bGreyScaleFormat ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,0));
		SetShaderValue(RHICmdList, GetPixelShader(),TextureComponentReplicateAlpha,TextureValue->bGreyScaleFormat ? FLinearColor(1,0,0,0) : FLinearColor(0,0,0,1));

		const FVector TextureDimension((float)TextureValue->GetSizeX(), (float)TextureValue->GetSizeY(), (float)SizeZ);
		const float OneOverMinDimension = 1.f / FMath::Max(TextureDimension.GetMin(), 1.f);
		SetShaderValue(RHICmdList, GetPixelShader(), TraceVolumeScalingParameter, FVector4(
				TextureDimension.X * OneOverMinDimension, 
				TextureDimension.Y * OneOverMinDimension, 
				TextureDimension.Z * OneOverMinDimension, 
				TextureDimension.GetMax() * OneOverMinDimension * .5f) // Extent
			);

		SetShaderValue(RHICmdList, GetPixelShader(), TextureDimensionParameter, FVector(TextureDimension.X, TextureDimension.Y, TextureDimension.Z));

		SetShaderValue(RHICmdList, GetPixelShader(), TraceViewMatrixParameter, FMatrix(FRotationMatrix::Make(TraceOrientation)));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << InTexture;
		Ar << InTextureSampler;
		Ar << BadTexture;
		Ar << BadTextureSampler;
		Ar << TextureComponentReplicate;
		Ar << TextureComponentReplicateAlpha;
		Ar << ColorWeights;
		Ar << PackedParameters;
		Ar << NumTilesPerSideParameter;
		Ar << TraceVolumeScalingParameter;
		Ar << TextureDimensionParameter;
		Ar << TraceViewMatrixParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InTextureSampler;
	FShaderResourceParameter BadTexture;
	FShaderResourceParameter BadTextureSampler;
	FShaderParameter TextureComponentReplicate;
	FShaderParameter TextureComponentReplicateAlpha;
	FShaderParameter ColorWeights; 
	FShaderParameter PackedParameters;
	FShaderParameter NumTilesPerSideParameter;
	FShaderParameter TraceVolumeScalingParameter;
	FShaderParameter TextureDimensionParameter;
	FShaderParameter TraceViewMatrixParameter;
};

class FVolumeTextureTilePreviewPS : public FSimpleElementVolumeTexturePreviewPS
{
	DECLARE_SHADER_TYPE(FVolumeTextureTilePreviewPS,Global);
public:
	FVolumeTextureTilePreviewPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSimpleElementVolumeTexturePreviewPS(Initializer) {}
	FVolumeTextureTilePreviewPS() {}
};

class FVolumeTextureTracePreviewPS : public FSimpleElementVolumeTexturePreviewPS
{
	DECLARE_SHADER_TYPE(FVolumeTextureTracePreviewPS,Global);
public:
	FVolumeTextureTracePreviewPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FSimpleElementVolumeTexturePreviewPS(Initializer) {}
	FVolumeTextureTracePreviewPS() {}
};


IMPLEMENT_SHADER_TYPE(,FVolumeTextureTilePreviewPS,TEXT("/Engine/Private/SimpleElementVolumeTexturePreviewPixelShader.usf"),TEXT("TileMain"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(,FVolumeTextureTracePreviewPS,TEXT("/Engine/Private/SimpleElementVolumeTexturePreviewPixelShader.usf"),TEXT("TraceMain"),SF_Pixel);

/** Binds vertex and pixel shaders for this element */
void FBatchedElementVolumeTexturePreviewParameters::BindShaders(
	FRHICommandList& RHICmdList,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FMatrix& InTransform,
	const float InGamma,
	const FMatrix& InColorWeights,
	const FTexture* Texture)
{
	TShaderMapRef<FSimpleElementVS> VertexShader(GetGlobalShaderMap(InFeatureLevel));

	FSimpleElementVolumeTexturePreviewPS* PixelShader = nullptr;
	
	if (bViewModeAsDepthSlices)
	{
		TShaderMapRef<FVolumeTextureTilePreviewPS> TileShader(GetGlobalShaderMap(InFeatureLevel));
		PixelShader = *TileShader;
	}
	else
	{
		TShaderMapRef<FVolumeTextureTracePreviewPS> TileShader(GetGlobalShaderMap(InFeatureLevel));
		PixelShader = *TileShader;
	}


	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSimpleElementVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	if (!bViewModeAsDepthSlices)
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
	}

	FMatrix ColorWeights = InColorWeights;
	if (!bViewModeAsDepthSlices && ColorWeights.M[3][3] == 0)
	{
		const float XWeight = ColorWeights.M[0][0] + ColorWeights.M[1][0] + ColorWeights.M[2][0];
		const float YWeight = ColorWeights.M[0][1] + ColorWeights.M[1][1] + ColorWeights.M[2][1];
		const float ZWeight = ColorWeights.M[0][2] + ColorWeights.M[1][2] + ColorWeights.M[2][2];
		const float OneOverWeightSum = 1.f / FMath::Max(SMALL_NUMBER, XWeight + YWeight + ZWeight);
		ColorWeights.M[3][0] = XWeight * OneOverWeightSum;
		ColorWeights.M[3][1] = YWeight * OneOverWeightSum;
		ColorWeights.M[3][2] = ZWeight * OneOverWeightSum;
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, EApplyRendertargetOption::ForceApply);

	VertexShader->SetParameters(RHICmdList, InTransform);
	PixelShader->SetParameters(RHICmdList, Texture, SizeZ, ColorWeights, InGamma, MipLevel, Opacity, TraceOrientation);
}
