// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineOutputBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SceneView.h"
#include "MovieRenderPipelineDataTypes.h"
#include "GameFramework/PlayerController.h"
#include "MoviePipelineRenderPass.h"
#include "EngineModule.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget.h"
#include "MoviePipeline.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineShotConfig.h"
#include "MovieRenderOverlappedImage.h"
#include "MovieRenderPipelineCoreModule.h"
#include "ImagePixelData.h"
#include "MoviePipelineOutputBuilder.h"
#include "BufferVisualizationData.h"
#include "Containers/Array.h"
#include "FinalPostProcessSettings.h"
#include "Materials/Material.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Engine/RendererSettings.h"
#include "Camera/CameraComponent.h"
#include "CineCameraComponent.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "MoviePipelineUtils.h"

FString UMoviePipelineDeferredPassBase::StencilLayerMaterialAsset = TEXT("/MovieRenderPipeline/Materials/MoviePipeline_StencilCutout.MoviePipeline_StencilCutout");
FString UMoviePipelineDeferredPassBase::DefaultDepthAsset = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_WorldDepth.MovieRenderQueue_WorldDepth");
FString UMoviePipelineDeferredPassBase::DefaultMotionVectorsAsset = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_MotionVectors.MovieRenderQueue_MotionVectors");

PRAGMA_DISABLE_OPTIMIZATION

UMoviePipelineDeferredPassBase::UMoviePipelineDeferredPassBase() 
	: UMoviePipelineImagePassBase()
{
	PassIdentifier = FMoviePipelinePassIdentifier("FinalImage");

	// To help user knowledge we pre-seed the additional post processing materials with an array of potentially common passes.
	TArray<FString> DefaultPostProcessMaterials;
	DefaultPostProcessMaterials.Add(DefaultDepthAsset);
	DefaultPostProcessMaterials.Add(DefaultMotionVectorsAsset);

	for (FString& MaterialPath : DefaultPostProcessMaterials)
	{
		FMoviePipelinePostProcessPass& NewPass = AdditionalPostProcessMaterials.AddDefaulted_GetRef();
		NewPass.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		NewPass.bEnabled = false;
	}
	bRenderMainPass = true;
	bUse32BitPostProcessMaterials = false;
}

void UMoviePipelineDeferredPassBase::MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag)
{
	if (bDisableMultisampleEffects)
	{
		OutShowFlag.SetAntiAliasing(false);
		OutShowFlag.SetDepthOfField(false);
		OutShowFlag.SetMotionBlur(false);
		OutShowFlag.SetBloom(false);
		OutShowFlag.SetSceneColorFringe(false);
	}
}

void UMoviePipelineDeferredPassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);
	LLM_SCOPE_BYNAME(TEXT("MoviePipeline/DeferredPassSetup"));

	// [0] is FinalImage, [1] is Default Layer, [1+] is Stencil Layers. Not used by post processing materials
	// Render Target that the GBuffer is copied to
	int32 NumRenderTargets = (bAddDefaultLayer ? 1 : 0) + StencilLayers.Num() + 1;
	for (int32 Index = 0; Index < NumRenderTargets; Index++)
	{
		UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		NewTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

		// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues when accumulating later
		NewTarget->InitCustomFormat(InPassInitSettings.BackbufferResolution.X, InPassInitSettings.BackbufferResolution.Y, EPixelFormat::PF_FloatRGBA, false);

		// OCIO: Since this is a manually created Render target we don't need Gamma to be applied.
		// We use this render target to render to via a display extension that utilizes Display Gamma
		// which has a default value of 2.2 (DefaultDisplayGamma), therefore we need to set Gamma on this render target to 2.2 to cancel out any unwanted effects.
		NewTarget->TargetGamma = FOpenColorIODisplayExtension::DefaultDisplayGamma;

		TileRenderTargets.Add(NewTarget);
	}


	if (GetPipeline()->GetPreviewTexture() == nullptr)
	{
		GetPipeline()->SetPreviewTexture(TileRenderTargets[0]);
	}

	{
		TSoftObjectPtr<UMaterialInterface> StencilMatRef = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(StencilLayerMaterialAsset));
		StencilLayerMaterial = StencilMatRef.LoadSynchronous();
		if (!StencilLayerMaterial)
		{
			UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to load Stencil Mask material, stencil layers will be incorrect. Path: %s"), *StencilMatRef.ToString());
		}
	}
	
	for (FMoviePipelinePostProcessPass& AdditionalPass : AdditionalPostProcessMaterials)
	{
		if (AdditionalPass.bEnabled)
		{
			UMaterialInterface* Material = AdditionalPass.Material.LoadSynchronous();
			if (Material)
			{
				ActivePostProcessMaterials.Add(Material);
			}
		}
	}

	SurfaceQueue = MakeShared<FMoviePipelineSurfaceQueue>(InPassInitSettings.BackbufferResolution, EPixelFormat::PF_FloatRGBA, 3, true);

	// Create a view state. Each individual camera, tile, and stencil layer need their own unique state as this includes visual history for anti-aliasing, etc. 
	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
	UMoviePipelineHighResSetting* HighResSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineHighResSetting>(CurrentShot);
	int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;

	int32 TotalNumberOfAccumulators = 0;
	for (int32 CamIndex = 0; CamIndex < NumCameras; CamIndex++)
	{
		FMultiCameraViewStateData& CameraData = CameraViewStateData.AddDefaulted_GetRef();

		// We don't always want to allocate a unique history per tile as very large resolutions can OOM the GPU in backbuffer images alone.
		// But we do need the history for some features (like Lumen) to work, so it's optional.
		int32 NumHighResTiles = HighResSettings->bAllocateHistoryPerTile ? (HighResSettings->TileCount * HighResSettings->TileCount) : 1;
		for (int32 TileIndexX = 0; TileIndexX < NumHighResTiles; TileIndexX++)
		{
			for (int32 TileIndexY = 0; TileIndexY < NumHighResTiles; TileIndexY++)
			{
				FMultiCameraViewStateData::FPerTile& PerTile = CameraData.TileData.FindOrAdd(FIntPoint(TileIndexX, TileIndexY));
				// If they want to render the main pass (most likely) add a view state for it
				if (bRenderMainPass)
				{
					PerTile.SceneViewStates.AddDefaulted();
				}

				// If they want to render a "default" stencil layer (that has everything not in another layer) add that...
				if (StencilLayers.Num() > 0 && bAddDefaultLayer)
				{
					PerTile.SceneViewStates.AddDefaulted();
				}

				// Finally all of the other stencil layers
				for (int32 Index = 0; Index < StencilLayers.Num(); Index++)
				{
					PerTile.SceneViewStates.AddDefaulted();
				}
			}
		}

		// We have to add up the number of accumulators needed separately, because we don't make
		// one accumulator per high-res tile.
		if (bRenderMainPass)
		{
			TotalNumberOfAccumulators++;
		}
		if (StencilLayers.Num() > 0 && bAddDefaultLayer)
		{
			TotalNumberOfAccumulators++;
		}
		for (int32 Index = 0; Index < StencilLayers.Num(); Index++)
		{
			TotalNumberOfAccumulators++;
		}

		// Now that we have an array of view states, allocate each one.
		for (TPair<FIntPoint, FMultiCameraViewStateData::FPerTile>& Pair : CameraData.TileData)
		{
			for (int32 Index = 0; Index < Pair.Value.SceneViewStates.Num(); Index++)
			{
				Pair.Value.SceneViewStates[Index].Allocate(InPassInitSettings.FeatureLevel);
			}
		}
	}

	// We must allocate one accumulator per output, because when we submit a sample we tie up an accumulator, but because of temporal sampling
	// the accumulators can be tied up for multiple game frames, thus we must have at least one per output and we can only reuse them between
	// actual output frames (not engine frames). This doesn't allocate memory until they're actually used so it's ok to over-allocate.
	int32 PoolSize = (TotalNumberOfAccumulators + (ActivePostProcessMaterials.Num()*NumCameras) + 1) * 3;
	AccumulatorPool = MakeShared<TAccumulatorPool<FImageOverlappedAccumulator>, ESPMode::ThreadSafe>(PoolSize);
	
	PreviousCustomDepthValue.Reset();
	PreviousDumpFramesValue.Reset();
	PreviousColorFormatValue.Reset();

	// This scene view extension will be released automatically as soon as Render Sequence is torn down.
	// One Extension per sequence, since each sequence has its own OCIO settings.
	OCIOSceneViewExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>();

	const bool bEnableStencilPass = bAddDefaultLayer || StencilLayers.Num() > 0;
	if (bEnableStencilPass)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
		if (CVar)
		{
			PreviousCustomDepthValue = CVar->GetInt();
			const int32 CustomDepthWithStencil = 3;
			if (PreviousCustomDepthValue != CustomDepthWithStencil)
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Overriding project custom depth/stencil value to support a stencil pass."));
				// We use ECVF_SetByProjectSetting otherwise once this is set once by rendering, the UI silently fails
				// if you try to change it afterwards. This SetByProjectSetting will fail if they have manipulated the cvar via the console
				// during their current session but it's less likely than changing the project settings.
				CVar->Set(CustomDepthWithStencil, EConsoleVariableFlags::ECVF_SetByProjectSetting);
			}
		}
	}
	
	if (bUse32BitPostProcessMaterials)
	{
		IConsoleVariable* DumpFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationDumpFramesAsHDR"));
		if (DumpFramesCVar)
		{
			PreviousDumpFramesValue = DumpFramesCVar->GetInt();
			DumpFramesCVar->Set(1, EConsoleVariableFlags::ECVF_SetByConsole);
		}
		
		IConsoleVariable* ColorFormatCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessingColorFormat"));
		if (ColorFormatCVar)
		{
			PreviousColorFormatValue = ColorFormatCVar->GetInt();
			ColorFormatCVar->Set(1, EConsoleVariableFlags::ECVF_SetByConsole);
		}
	}
}

void UMoviePipelineDeferredPassBase::TeardownImpl()
{
	GetPipeline()->SetPreviewTexture(nullptr);

	// This may call FlushRenderingCommands if there are outstanding readbacks that need to happen.
	if (SurfaceQueue.IsValid())
	{
		SurfaceQueue->Shutdown();
	}

	// Stall until the task graph has completed any pending accumulations.
	FTaskGraphInterface::Get().WaitUntilTasksComplete(OutstandingTasks, ENamedThreads::GameThread);
	OutstandingTasks.Reset();

	ActivePostProcessMaterials.Reset();

	for (FMultiCameraViewStateData& CameraData : CameraViewStateData)
	{
		for (TPair<FIntPoint, FMultiCameraViewStateData::FPerTile>& Pair : CameraData.TileData)
		{
			for (int32 Index = 0; Index < Pair.Value.SceneViewStates.Num(); Index++)
			{
				FSceneViewStateInterface* Ref = Pair.Value.SceneViewStates[Index].GetReference();
				if (Ref)
				{
					Ref->ClearMIDPool();
				}
				Pair.Value.SceneViewStates[Index].Destroy();
			}
		}
	}
	CameraViewStateData.Reset();
	TileRenderTargets.Reset();
	
	OCIOSceneViewExtension.Reset();
	OCIOSceneViewExtension = nullptr;

	if (PreviousCustomDepthValue.IsSet())
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
		if (CVar)
		{
			if (CVar->GetInt() != PreviousCustomDepthValue.GetValue())
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Restoring custom depth/stencil value to: %d"), PreviousCustomDepthValue.GetValue());
				CVar->Set(PreviousCustomDepthValue.GetValue(), EConsoleVariableFlags::ECVF_SetByProjectSetting);
			}
		}
	}
	
	if (PreviousDumpFramesValue.IsSet())
	{
		IConsoleVariable* DumpFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationDumpFramesAsHDR"));
		if (DumpFramesCVar)
		{
			DumpFramesCVar->Set(PreviousDumpFramesValue.GetValue(),  EConsoleVariableFlags::ECVF_SetByConsole);
		}
		
		IConsoleVariable* ColorFormatCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessingColorFormat"));
		if (ColorFormatCVar)
		{
			ColorFormatCVar->Set(PreviousColorFormatValue.GetValue(), EConsoleVariableFlags::ECVF_SetByConsole);
		}
	}

	// Preserve our view state until the rendering thread has been flushed.
	Super::TeardownImpl();
}

void UMoviePipelineDeferredPassBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UMoviePipelineDeferredPassBase& This = *CastChecked<UMoviePipelineDeferredPassBase>(InThis);

	for (FMultiCameraViewStateData& CameraData : This.CameraViewStateData)
	{
		for (TPair<FIntPoint, FMultiCameraViewStateData::FPerTile>& Pair : CameraData.TileData)
		{
			for (int32 Index = 0; Index < Pair.Value.SceneViewStates.Num(); Index++)
			{
				FSceneViewStateInterface* Ref = Pair.Value.SceneViewStates[Index].GetReference();
				if (Ref)
				{
					Ref->AddReferencedObjects(Collector);
				}
			}
		}
	}
}

namespace UE
{
namespace MoviePipeline
{
struct FDeferredPassRenderStatePayload : public UMoviePipelineImagePassBase::IViewCalcPayload
{
	int32 CameraIndex;
	FIntPoint TileIndex; // Will always be 1,1 if no history-per-tile is enabled
	int32 SceneViewIndex;
};
}
}


FSceneViewStateInterface* UMoviePipelineDeferredPassBase::GetSceneViewStateInterface(IViewCalcPayload* OptPayload)
{
	UE::MoviePipeline::FDeferredPassRenderStatePayload* Payload = (UE::MoviePipeline::FDeferredPassRenderStatePayload*)OptPayload;
	check(Payload);

	FMultiCameraViewStateData& CameraData = CameraViewStateData[Payload->CameraIndex];
	if (FMultiCameraViewStateData::FPerTile* TileData = CameraData.TileData.Find(Payload->TileIndex))
	{
		return TileData->SceneViewStates[Payload->SceneViewIndex].GetReference();
	}

	return nullptr;
}

UTextureRenderTarget2D* UMoviePipelineDeferredPassBase::GetViewRenderTarget(IViewCalcPayload* OptPayload) const
{
	return TileRenderTargets[0];
	UE::MoviePipeline::FDeferredPassRenderStatePayload* Payload = (UE::MoviePipeline::FDeferredPassRenderStatePayload*)OptPayload;
	check(Payload);

	// ToDo: Is it really necessary to have a unique RT for each render? Only the last one gets shown (right now)
	// if (CurrentLayerIndex == INDEX_NONE)
	// {
	// 	return TileRenderTargets[0];
	// }
	// else
	// {
	// 	return TileRenderTargets[CurrentLayerIndex + 1];
	// }
}

void UMoviePipelineDeferredPassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	// No super call here because multiple cameras makes this all complicated
	// Super::GatherOutputPassesImpl(ExpectedRenderPasses);

	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
	int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;

	for (int32 CameraIndex = 0; CameraIndex < NumCameras; CameraIndex++)
	{
		FMoviePipelinePassIdentifier PassIdentifierForCurrentCamera;
		PassIdentifierForCurrentCamera.Name = PassIdentifier.Name;
		PassIdentifierForCurrentCamera.CameraName = CurrentShot->GetCameraName(CameraIndex);

		// Add the default backbuffer
		if (bRenderMainPass)
		{
			ExpectedRenderPasses.Add(PassIdentifierForCurrentCamera);
		}

		// Each camera will render everything in the Post Process Material stack.
		TArray<FString> RenderPasses;
		for (UMaterialInterface* Material : ActivePostProcessMaterials)
		{
			if (Material)
			{
				RenderPasses.Add(Material->GetName());
			}
		}

		for (const FString& Pass : RenderPasses)
		{
			ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifierForCurrentCamera.Name + Pass, PassIdentifierForCurrentCamera.CameraName));
		}

		// Stencil Layer Time!
		if (StencilLayers.Num() > 0 && bAddDefaultLayer)
		{
			ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifierForCurrentCamera.Name + TEXT("DefaultLayer"), PassIdentifierForCurrentCamera.CameraName));
		}

		for (const FActorLayer& Layer : StencilLayers)
		{
			ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifierForCurrentCamera.Name + Layer.Name.ToString(), PassIdentifierForCurrentCamera.CameraName));
		}
	}
}

void UMoviePipelineDeferredPassBase::AddViewExtensions(FSceneViewFamilyContext& InContext, FMoviePipelineRenderPassMetrics& InOutSampleState)
{
	// OCIO Scene View Extension is a special case and won't be registered like other view extensions.
	if (InOutSampleState.OCIOConfiguration && InOutSampleState.OCIOConfiguration->bIsEnabled)
	{
		FOpenColorIODisplayConfiguration* OCIOConfigNew = const_cast<FMoviePipelineRenderPassMetrics&>(InOutSampleState).OCIOConfiguration;
		FOpenColorIODisplayConfiguration& OCIOConfigCurrent = OCIOSceneViewExtension->GetDisplayConfiguration();

		// We only need to set this once per render sequence.
		if (OCIOConfigNew->ColorConfiguration.ConfigurationSource && OCIOConfigNew->ColorConfiguration.ConfigurationSource != OCIOConfigCurrent.ColorConfiguration.ConfigurationSource)
		{
			OCIOSceneViewExtension->SetDisplayConfiguration(*OCIOConfigNew);
		}

		InContext.ViewExtensions.Add(OCIOSceneViewExtension.ToSharedRef());
	}
}

void UMoviePipelineDeferredPassBase::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	Super::RenderSample_GameThreadImpl(InSampleState);

	// Wait for a surface to be available to write to. This will stall the game thread while the RHI/Render Thread catch up.
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableSurface);
		SurfaceQueue->BlockUntilAnyAvailable();
	}

	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
	int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;

	for (int32 CameraIndex = 0; CameraIndex < NumCameras; CameraIndex++)
	{
		FMoviePipelinePassIdentifier PassIdentifierForCurrentCamera;
		PassIdentifierForCurrentCamera.Name = PassIdentifier.Name;
		PassIdentifierForCurrentCamera.CameraName = CurrentShot->GetCameraName(CameraIndex);

		// Main Render Pass
		if (bRenderMainPass)
		{
			FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;
			// InOutSampleState.OutputState.CameraCount = NumCameras;
			InOutSampleState.OutputState.CameraIndex = CameraIndex;

			UE::MoviePipeline::FDeferredPassRenderStatePayload Payload;
			Payload.CameraIndex = CameraIndex;
			Payload.TileIndex = InOutSampleState.TileIndexes;

			// Main renders use index 0.
			Payload.SceneViewIndex = 0;

			TSharedPtr<FSceneViewFamilyContext> ViewFamily = CalculateViewFamily(InOutSampleState, &Payload);

			// Add post-processing materials if needed
			FSceneView* View = const_cast<FSceneView*>(ViewFamily->Views[0]);
			View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Empty();
			View->FinalPostProcessSettings.BufferVisualizationPipes.Empty();

			for (UMaterialInterface* Material : ActivePostProcessMaterials)
			{
				if (Material)
				{
					View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(Material);
				}
			}

			for (UMaterialInterface* VisMaterial : View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials)
			{
				// If this was just to contribute to the history buffer, no need to go any further.
				if (InOutSampleState.bDiscardResult)
				{
					continue;
				}
				FMoviePipelinePassIdentifier LayerPassIdentifier = FMoviePipelinePassIdentifier(PassIdentifier.Name + VisMaterial->GetName(), PassIdentifierForCurrentCamera.CameraName);

				auto BufferPipe = MakeShared<FImagePixelPipe, ESPMode::ThreadSafe>();
				BufferPipe->AddEndpoint(MakeForwardingEndpoint(LayerPassIdentifier, InOutSampleState));

				View->FinalPostProcessSettings.BufferVisualizationPipes.Add(VisMaterial->GetFName(), BufferPipe);
			}


			int32 NumValidMaterials = View->FinalPostProcessSettings.BufferVisualizationPipes.Num();
			View->FinalPostProcessSettings.bBufferVisualizationDumpRequired = NumValidMaterials > 0;

			// Submit to be rendered. Main render pass always uses target 0.
			FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();
			FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), View->GetFeatureLevel(), FCanvas::CDM_DeferDrawing, 1.0f);
			GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.Get());

			// Readback + Accumulate.
			PostRendererSubmission(InOutSampleState, PassIdentifierForCurrentCamera, GetOutputFileSortingOrder(), Canvas);
		}


		// Now do the stencil layer submission (which doesn't support additional post processing materials)
		{
			FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;
			InOutSampleState.OutputState.CameraIndex = CameraIndex;

			struct FStencilValues
			{
				FStencilValues()
					: bRenderCustomDepth(false)
					, StencilMask(ERendererStencilMask::ERSM_Default)
					, CustomStencil(0)
				{
				}

				bool bRenderCustomDepth;
				ERendererStencilMask StencilMask;
				int32 CustomStencil;
			};

			// Now for each stencil layer we reconfigure all the actors custom depth/stencil 
			TArray<FActorLayer> AllStencilLayers = StencilLayers;
			if (bAddDefaultLayer)
			{
				FActorLayer& DefaultLayer = AllStencilLayers.AddDefaulted_GetRef();
				DefaultLayer.Name = FName("DefaultLayer");
			}

			// If we're going to be using stencil layers, we need to cache all of the users
			// custom stencil/depth settings since we're changing them to do the mask.
			TMap<UPrimitiveComponent*, FStencilValues> PreviousValues;
			if (AllStencilLayers.Num() > 0)
			{
				for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
				{
					AActor* Actor = *ActorItr;
					if (Actor)
					{
						for (UActorComponent* Component : Actor->GetComponents())
						{
							if (Component && Component->IsA<UPrimitiveComponent>())
							{
								UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
								FStencilValues& Values = PreviousValues.Add(PrimitiveComponent);
								Values.StencilMask = PrimitiveComponent->CustomDepthStencilWriteMask;
								Values.CustomStencil = PrimitiveComponent->CustomDepthStencilValue;
								Values.bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
							}
						}
					}
				}
			}


			for (int32 StencilLayerIndex = 0; StencilLayerIndex < AllStencilLayers.Num(); StencilLayerIndex++)
			{
				const FActorLayer& Layer = AllStencilLayers[StencilLayerIndex];
				FMoviePipelinePassIdentifier LayerPassIdentifier = FMoviePipelinePassIdentifier(PassIdentifierForCurrentCamera.Name + Layer.Name.ToString());
				LayerPassIdentifier.CameraName = PassIdentifierForCurrentCamera.CameraName;

				// Modify all of the actors in this world so they have the right stencil settings (so we can use the stencil buffer as a mask later)
				for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
				{
					AActor* Actor = *ActorItr;
					if (Actor)
					{
						// The way stencil masking works is that we draw the actors on the given layer to the stencil buffer.
						// Then we apply a post-processing material which colors pixels outside those actors black, before
						// post processing. Then, TAA, Motion Blur, etc. is applied to all pixels. An alpha channel can preserve
						// which pixels were the geometry and which are dead space which lets you apply that as a mask later.
						bool bInLayer = true;
						if (bAddDefaultLayer && Layer.Name == FName("DefaultLayer"))
						{
							// If we're trying to render the default layer, the logic is different - we only add objects who
							// aren't in any of the stencil layers.
							for (const FActorLayer& AllLayer : StencilLayers)
							{
								bInLayer = !Actor->Layers.Contains(AllLayer.Name);
								if (!bInLayer)
								{
									break;
								}
							}
						}
						else
						{
							// If this a normal layer, we only add the actor if it exists on this layer.
							bInLayer = Actor->Layers.Contains(Layer.Name);
						}

						for (UActorComponent* Component : Actor->GetComponents())
						{
							if (Component && Component->IsA<UPrimitiveComponent>())
							{
								UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
								// We want to render all objects not on the layer to stencil too so that foreground objects mask.
								PrimitiveComponent->SetCustomDepthStencilValue(bInLayer ? 1 : 0);
								PrimitiveComponent->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
								PrimitiveComponent->SetRenderCustomDepth(true);
							}
						}
					}
				}

				// Submit the actual render now
				if (StencilLayerMaterial)
				{
					UE::MoviePipeline::FDeferredPassRenderStatePayload Payload;
					Payload.CameraIndex = CameraIndex;
					Payload.TileIndex = InOutSampleState.TileIndexes;
					Payload.SceneViewIndex = StencilLayerIndex + (bRenderMainPass ? 1 : 0);
					TSharedPtr<FSceneViewFamilyContext> ViewFamily = CalculateViewFamily(InOutSampleState, &Payload);
					FSceneView* View = const_cast<FSceneView*>(ViewFamily->Views[0]);

					// Now that we've modified all of the stencil values, we can submit them to be rendered.
					View->FinalPostProcessSettings.AddBlendable(StencilLayerMaterial, 1.0f);
					IBlendableInterface* BlendableInterface = Cast<IBlendableInterface>(StencilLayerMaterial);
					BlendableInterface->OverrideBlendableSettings(*View, 1.f);

					{
						FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();
						FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), View->GetFeatureLevel(), FCanvas::CDM_DeferDrawing, 1.0f);
						GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.Get());

						// Readback + Accumulate.
						PostRendererSubmission(InOutSampleState, LayerPassIdentifier, GetOutputFileSortingOrder() + 1, Canvas);
					}
				}
			}

			// Now that all stencil layers have been rendered, we can restore the custom depth/stencil/etc. values so that the main render pass acts as the user expects next time.
			for (TPair<UPrimitiveComponent*, FStencilValues>& KVP : PreviousValues)
			{
				KVP.Key->SetCustomDepthStencilValue(KVP.Value.CustomStencil);
				KVP.Key->SetCustomDepthStencilWriteMask(KVP.Value.StencilMask);
				KVP.Key->SetRenderCustomDepth(KVP.Value.bRenderCustomDepth);
			}
		}
	}
}

TFunction<void(TUniquePtr<FImagePixelData>&&)> UMoviePipelineDeferredPassBase::MakeForwardingEndpoint(const FMoviePipelinePassIdentifier InPassIdentifier, const FMoviePipelineRenderPassMetrics& InSampleState)
{
	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, InPassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = InPassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = GetOutputFileSortingOrder() + 1;

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulatorIncludesAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		// Transfer the framePayload to the returned data
		TUniquePtr<FImagePixelData> PixelDataWithPayload = nullptr;
		switch (InPixelData->GetType())
		{
		case EImagePixelType::Color:
		{
			TImagePixelData<FColor>* SourceData = static_cast<TImagePixelData<FColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float16:
		{
			TImagePixelData<FFloat16Color>* SourceData = static_cast<TImagePixelData<FFloat16Color>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FFloat16Color>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float32:
		{
			TImagePixelData<FLinearColor>* SourceData = static_cast<TImagePixelData<FLinearColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FLinearColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		default:
			checkNoEntry();
		}

		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(PixelDataWithPayload), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});
		SampleAccumulator->TaskPrereq = Event;

		this->OutstandingTasks.Add(Event);
	};

	return Callback;
}

UE::MoviePipeline::FImagePassCameraViewData UMoviePipelineDeferredPassBase::GetCameraInfo(FMoviePipelineRenderPassMetrics& InOutSampleState, IViewCalcPayload* OptPayload) const
{
	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
	int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;
	
	if (NumCameras == 1)
	{
		// If there's only one camera being used we can use the parent class which assumes the camera comes from the PlayerCameraManager
		return Super::GetCameraInfo(InOutSampleState, OptPayload);
	}
	else
	{
		UE::MoviePipeline::FImagePassCameraViewData OutCameraData;

		// Here's where it gets a lot more complicated. There's a number of properties we need to fetch from a camera manually to fill out the minimal view info.
		UCameraComponent* OutCamera = nullptr;

		GetPipeline()->GetSidecarCameraData(CurrentShot, InOutSampleState.OutputState.CameraIndex, OutCameraData.ViewInfo, &OutCamera);
		if (OutCamera)
		{
			// This has to come from the main camera for consistency's sake, and it's not a per-camera setting in the editor.
			OutCameraData.ViewActor = GetPipeline()->GetWorld()->GetFirstPlayerController()->GetViewTarget();

			// Try adding cine-camera specific metadata (not all animated cameras are cine cameras though)
			UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(OutCamera);
			if (CineCameraComponent)
			{
				// ToDo: This is still wrong, PassIdentifier.CameraName needs to come in from the InOutSampleState somewhere.
				UE::MoviePipeline::GetMetadataFromCineCamera(CineCameraComponent, PassIdentifier.CameraName, PassIdentifier.Name, OutCameraData.FileMetadata);

				// We only do this in the multi-camera case because the single camera case is covered by the main Rendering loop.
				UE::MoviePipeline::GetMetadataFromCameraLocRot(PassIdentifier.CameraName, PassIdentifier.Name, OutCameraData.ViewInfo.Location, OutCameraData.ViewInfo.Rotation, OutCameraData.ViewInfo.PreviousViewTransform->GetLocation(), FRotator(OutCameraData.ViewInfo.PreviousViewTransform->GetRotation()), OutCameraData.FileMetadata);
			}
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to find Camera Component for Shot: %d CameraIndex: %d"), GetPipeline()->GetCurrentShotIndex(), InOutSampleState.OutputState.CameraIndex);
		}

		return OutCameraData;
	}
}

void UMoviePipelineDeferredPassBase::BlendPostProcessSettings(FSceneView* InView, FMoviePipelineRenderPassMetrics& InOutSampleState, IViewCalcPayload* OptPayload)
{
	UMoviePipelineExecutorShot* CurrentShot = GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()];
	UMoviePipelineCameraSetting* CameraSettings = GetPipeline()->FindOrAddSettingForShot<UMoviePipelineCameraSetting>(CurrentShot);
	int32 NumCameras = CameraSettings->bRenderAllCameras ? CurrentShot->SidecarCameras.Num() : 1;

	UCameraComponent* OutCamera = nullptr;
	FMinimalViewInfo OutViewInfo;

	GetPipeline()->GetSidecarCameraData(CurrentShot, InOutSampleState.OutputState.CameraIndex, OutViewInfo, &OutCamera);
	if (!OutCamera)
	{
		// GetCameraInfo will have already printed a warning
		return;
	}

	// The primary camera should still respect the world post processing volumes and should already be the viewtarget.
	if (NumCameras == 1)
	{
		// If there's only one camera being used we can use the parent class which assumes the camera comes from the PlayerCameraManager
		Super::GetCameraInfo(InOutSampleState, OptPayload);
	}
	else
	{
		// For sidecar cameras we need to do the blending of PP volumes
		FVector ViewLocation = OutCamera->GetComponentLocation();
		for (IInterface_PostProcessVolume* PPVolume : GetWorld()->PostProcessVolumes)
		{
			const FPostProcessVolumeProperties VolumeProperties = PPVolume->GetProperties();

			// Skip any volumes which are disabled
			if (!VolumeProperties.bIsEnabled)
			{
				continue;
			}

			float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);

			if (!VolumeProperties.bIsUnbound)
			{
				float DistanceToPoint = 0.0f;
				PPVolume->EncompassesPoint(ViewLocation, 0.0f, &DistanceToPoint);

				if (DistanceToPoint >= 0 && DistanceToPoint < VolumeProperties.BlendRadius)
				{
					LocalWeight *= FMath::Clamp(1.0f - DistanceToPoint / VolumeProperties.BlendRadius, 0.0f, 1.0f);
				}
				else
				{
					LocalWeight = 0.0f;
				}
			}

			InView->OverridePostProcessSettings(*VolumeProperties.Settings, LocalWeight);
		}

		// After blending all post processing volumes, blend the camera's post process settings too
		InView->OverridePostProcessSettings(OutCamera->PostProcessSettings, OutCamera->PostProcessBlendWeight);
	}
}


void UMoviePipelineDeferredPassBase::PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, const FMoviePipelinePassIdentifier InPassIdentifier, const int32 InSortingOrder, FCanvas& InCanvas)
{
	// If this was just to contribute to the history buffer, no need to go any further.
	if (InSampleState.bDiscardResult)
	{
		return;
	}
	
	// Draw letterboxing
	APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if(PlayerCameraManager && PlayerCameraManager->GetCameraCacheView().bConstrainAspectRatio)
	{
		const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCacheView();
		UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
		check(OutputSettings);
		
		// Taking overscan into account.
		FIntPoint FullOutputSize = UMoviePipelineBlueprintLibrary::GetEffectiveOutputResolution(GetPipeline()->GetPipelineMasterConfig(), GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()]);

		float OutputSizeAspectRatio = FullOutputSize.X / (float)FullOutputSize.Y;
		const FIntPoint ConstrainedFullSize = CameraCache.AspectRatio > OutputSizeAspectRatio ?
			FIntPoint(FullOutputSize.X, FMath::CeilToInt((double)FullOutputSize.X / (double)CameraCache.AspectRatio)) :
			FIntPoint(FMath::CeilToInt(CameraCache.AspectRatio * FullOutputSize.Y), FullOutputSize.Y);

		const FIntPoint TileViewMin = InSampleState.OverlappedOffset;
		const FIntPoint TileViewMax = TileViewMin + InSampleState.BackbufferSize;

		// Camera ratio constrained rect, clipped by the tile rect
		FIntPoint ConstrainedViewMin = (FullOutputSize - ConstrainedFullSize) / 2;
		FIntPoint ConstrainedViewMax = ConstrainedViewMin + ConstrainedFullSize;
		ConstrainedViewMin = FIntPoint(FMath::Clamp(ConstrainedViewMin.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMin.Y, TileViewMin.Y, TileViewMax.Y));
		ConstrainedViewMax = FIntPoint(FMath::Clamp(ConstrainedViewMax.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMax.Y, TileViewMin.Y, TileViewMax.Y));

		// Difference between the clipped constrained rect and the tile rect
		const FIntPoint OffsetMin = ConstrainedViewMin - TileViewMin;
		const FIntPoint OffsetMax = TileViewMax - ConstrainedViewMax;

		// Clear left
		if (OffsetMin.X > 0)
		{
			InCanvas.DrawTile(0, 0, OffsetMin.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear right
		if (OffsetMax.X > 0)
		{
			InCanvas.DrawTile(InSampleState.BackbufferSize.X - OffsetMax.X, 0, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear top
		if (OffsetMin.Y > 0)
		{
			InCanvas.DrawTile(0, 0, InSampleState.BackbufferSize.X, OffsetMin.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear bottom
		if (OffsetMax.Y > 0)
		{
			InCanvas.DrawTile(0, InSampleState.BackbufferSize.Y - OffsetMax.Y, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}

		InCanvas.Flush_GameThread(true);
	}

	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, InPassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = InPassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = InSortingOrder;

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulatorIncludesAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(InPixelData), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				// Final sample has now been executed, break the pre-req chain and free the accumulator for reuse.
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});
		SampleAccumulator->TaskPrereq = Event;

		this->OutstandingTasks.Add(Event);
	};

	FRenderTarget* RenderTarget = InCanvas.GetRenderTarget();

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[LocalSurfaceQueue, FramePayload, Callback, RenderTarget](FRHICommandListImmediate& RHICmdList) mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			LocalSurfaceQueue->OnRenderTargetReady_RenderThread(RenderTarget->GetRenderTargetTexture(), FramePayload, MoveTemp(Callback));
		});

}

bool UMoviePipelineDeferredPassBase::IsAutoExposureAllowed(const FMoviePipelineRenderPassMetrics& InSampleState) const
{
	// High-res tiling doesn't support auto-exposure.
	return !(InSampleState.GetTileCount() > 1);
}

#if WITH_EDITOR
FText UMoviePipelineDeferredPass_PathTracer::GetFooterText(UMoviePipelineExecutorJob* InJob) const {
	return NSLOCTEXT(
		"MovieRenderPipeline",
		"DeferredBasePassSetting_FooterText_PathTracer",
		"Sampling for the Path Tracer is controlled by the Anti-Aliasing settings and the Reference Motion Blur setting.\n"
		"All other Path Tracer settings are taken from the Post Process settings.");
}
#endif
namespace UE
{
namespace MoviePipeline
{
	bool CheckIfPathTracerIsSupported()
	{
		bool bSupportsPathTracing = false;
		if (IsRayTracingEnabled())
		{
			IConsoleVariable* PathTracingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing"));
			if (PathTracingCVar)
			{
				bSupportsPathTracing = PathTracingCVar->GetInt() != 0;
			}
		}
		return bSupportsPathTracing;
	}
}
}
void UMoviePipelineDeferredPass_PathTracer::ValidateStateImpl()
{
	Super::ValidateStateImpl();

	bool bSupportsPathTracing = UE::MoviePipeline::CheckIfPathTracerIsSupported();
	
	if (!bSupportsPathTracing)
	{
		const FText ValidationWarning = NSLOCTEXT("MovieRenderPipeline", "PathTracerValidation_Unsupported", "Path Tracing is currently not enabled for this project and this render pass will not work.");
		ValidationResults.Add(ValidationWarning);
		ValidationState = EMoviePipelineValidationState::Warnings;
	}
}

void UMoviePipelineDeferredPass_PathTracer::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	if (!UE::MoviePipeline::CheckIfPathTracerIsSupported())
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("Cannot render a Path Tracer pass, Path Tracer is not enabled by this project."));
		GetPipeline()->Shutdown(true);
		return;
	}

	Super::SetupImpl(InPassInitSettings);
}

PRAGMA_ENABLE_OPTIMIZATION