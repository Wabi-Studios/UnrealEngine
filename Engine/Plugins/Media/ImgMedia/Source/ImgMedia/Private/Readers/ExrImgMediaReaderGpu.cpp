// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExrImgMediaReaderGpu.h"

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "Async/Async.h"
#include "Misc/Paths.h"
#include "OpenExrWrapper.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "ExrReaderGpu.h"
#include "RHICommandList.h"
#include "ExrSwizzlingShader.h"
#include "SceneUtils.h"


#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "CommonRenderResources.h"
#include "ImgMediaLoader.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "TextureResource.h"
#include "ScreenPass.h"

#include "ID3D12DynamicRHI.h"


DECLARE_GPU_STAT_NAMED(ExrImgMediaReaderGpu, TEXT("ExrImgMediaReaderGpu"));
DECLARE_GPU_STAT_NAMED(ExrImgMediaReaderGpu_MipRender, TEXT("ExrImgMediaReaderGpu_MipRender"));

namespace {

	/** This function is similar to DrawScreenPass in OpenColorIODisplayExtension.cpp except it is catered for Viewless texture rendering. */
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FIntPoint& OutputResolution,
		const FIntRect& Viewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			0, 0, OutputResolution.X, OutputResolution.Y,
			Viewport.Min.X, Viewport.Min.Y, Viewport.Width(), Viewport.Height(),
			OutputResolution,
			OutputResolution,
			PipelineState.VertexShader,
			INDEX_NONE,
			false,
			DrawRectangleFlags);
	}
}


/* FExrImgMediaReaderGpu structors
 *****************************************************************************/

FExrImgMediaReaderGpu::~FExrImgMediaReaderGpu()
{
	// A signal that tells all buffers that are stored in shared references not to return to the pool
	// but delete instead.
	bIsShuttingDown = true;

	// Making sure that all Used memory is processed first and returned into memory pool
	TransferFromStagingBuffer();

	// Unlock all buffers so that these will release.
	volatile bool bUnlocked = false;
	ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([this, &bUnlocked](FRHICommandListImmediate& RHICmdList)
	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_ReleaseMemoryPool);
		TArray<uint32> KeysForIteration;
		MemoryPool.GetKeys(KeysForIteration);
		for (uint32 Key : KeysForIteration)
		{
			TArray<FStructuredBufferPoolItem*> AllValues;
			MemoryPool.MultiFind(Key, AllValues);
			for (FStructuredBufferPoolItem* MemoryPoolItem : AllValues)
			{
				// Check if fence has signaled.
				check(!MemoryPoolItem->bWillBeSignaled || MemoryPoolItem->Fence->Poll());
				{
					RHIUnlockBuffer(MemoryPoolItem->BufferRef);
					delete MemoryPoolItem;
				}
			}
		}
		MemoryPool.Reset();
		bUnlocked = true;
	});

	// Wait until unlocking is complete.
	while (!bUnlocked)
	{
		FPlatformProcess::Sleep(0.01f);
	}
}


/* FExrImgMediaReaderGpu interface
 *****************************************************************************/

bool FExrImgMediaReaderGpu::ReadFrame(int32 FrameId, const TMap<int32, FImgMediaTileSelection>& InMipTiles, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame)
{
	// Fall back to cpu?
	if (bFallBackToCPU)
	{
		return FExrImgMediaReader::ReadFrame(FrameId, InMipTiles, OutFrame);
	}

	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Loader = LoaderPtr.Pin();
	if (Loader.IsValid() == false)
	{
		return false;
	}


	const FString& LargestImagePath = Loader->GetImagePath(FrameId, 0);

	if (!GetInfo(LargestImagePath, OutFrame->Info))
	{
		return false;
	}

	// Get tile info.
	bool bHasTiles = OutFrame->Info.bHasTiles;

	TSharedPtr<FSampleConverterParameters> ConverterParams = MakeShared<FSampleConverterParameters>();
	ConverterParams->FullResolution = OutFrame->Info.Dim;

	const FIntPoint TileDim = OutFrame->Info.TileDimensions;

	if (ConverterParams->FullResolution.GetMin() <= 0)
	{
		return false;
	}

	ConverterParams->FrameInfo = OutFrame->Info;
	ConverterParams->PixelSize = sizeof(uint16) * ConverterParams->FrameInfo.NumChannels;
	ConverterParams->TileDimWithBorders = TileDim + OutFrame->Info.TileBorder * 2;
	ConverterParams->NumMipLevels = Loader->GetNumMipLevels();
	ConverterParams->bCustomExr = OutFrame->Info.FormatName == TEXT("EXR CUSTOM");
	ConverterParams->bMipsInSeparateFiles = Loader->MipsInSeparateFiles();

	FExrMediaTextureSampleConverter* SampleConverter;
	if (!OutFrame->SampleConverter.IsValid())
	{
		OutFrame->SampleConverter = MakeShared<FExrMediaTextureSampleConverter>();
	}
	SampleConverter = static_cast<FExrMediaTextureSampleConverter*>(OutFrame->SampleConverter.Get());

	{
		// Loop over all mips.
		FIntPoint CurrentMipDim = ConverterParams->FullResolution;

		for (const TPair<int32, FImgMediaTileSelection>& TilesPerMip : InMipTiles)
		{

			const int32 CurrentMipLevel = TilesPerMip.Key;

			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("FExrImgMediaReaderGpu_ReadMip %d"), CurrentMipLevel));

			const FImgMediaTileSelection& CurrentTileSelection = TilesPerMip.Value;

			bool ReadThisMip = true;
			// Avoid reads if the cached frame already contains the current tiles for this mip level.
			if (const FImgMediaTileSelection* CachedSelection = OutFrame->MipTilesPresent.Find(CurrentMipLevel))
			{
				ReadThisMip = !CachedSelection->Contains(CurrentTileSelection);
			}
			
			if (!ReadThisMip)
			{
				continue;
			}

			// Next mip level.
			int MipLevelDiv = 1 << CurrentMipLevel;
			CurrentMipDim = ConverterParams->FullResolution / MipLevelDiv;


			const SIZE_T BufferSize = GetBufferSize(CurrentMipDim, ConverterParams->FrameInfo.NumChannels, bHasTiles, OutFrame->Info.NumTiles / MipLevelDiv, ConverterParams->bCustomExr);
			FStructuredBufferPoolItemSharedPtr BufferData = SampleConverter->GetMipLevelBuffer(CurrentMipLevel);

			if (!BufferData.IsValid())
			{
				BufferData = AllocateGpuBufferFromPool(BufferSize);
				SampleConverter->SetMipLevelBuffer(CurrentMipLevel, BufferData);
			}

			uint16* MipDataPtr = static_cast<uint16*>(BufferData->MappedBuffer);

			// Get highest resolution mip level path.
			FString ImagePath = Loader->GetImagePath(FrameId, ConverterParams->bMipsInSeparateFiles ? CurrentMipLevel : 0);
				
			EReadResult ReadResult = Fail;

			if (FPaths::FileExists(ImagePath))
			{
				// read frame data
				if (bHasTiles || ConverterParams->bCustomExr)
				{
					TArray<FIntRect> TileRegionsToRead;
					TArray<FIntRect> TileRegionsToRender;
					{
						TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("CalculateRegions %d"), CurrentMipLevel));

						if (const FImgMediaTileSelection* CachedSelection = OutFrame->MipTilesPresent.Find(CurrentMipLevel))
						{
							TileRegionsToRead = CachedSelection->GetVisibleRegions(&CurrentTileSelection);
							TileRegionsToRender = CurrentTileSelection.GetVisibleRegions();
						}
						else
						{
							TileRegionsToRead = TileRegionsToRender = CurrentTileSelection.GetVisibleRegions();
						}
					}

					TArray<FIntRect>& Viewports = ConverterParams->Viewports.Add(CurrentMipLevel);
					for (const FIntRect& TileRegion : TileRegionsToRender)
					{
						FIntRect Viewport;
						Viewport.Min = FIntPoint(ConverterParams->TileDimWithBorders.X * TileRegion.Min.X, ConverterParams->TileDimWithBorders.Y * TileRegion.Min.Y);
						Viewport.Max = FIntPoint(ConverterParams->TileDimWithBorders.X * TileRegion.Max.X, ConverterParams->TileDimWithBorders.Y * TileRegion.Max.Y);
						Viewport.Clip(FIntRect(FIntPoint::ZeroValue, CurrentMipDim));
						Viewports.Add(MoveTemp(Viewport));
					}

					ReadResult = ReadTilesCustom(MipDataPtr, BufferSize, ImagePath, FrameId, TileRegionsToRead, ConverterParams, CurrentMipLevel);
				}
				else
				{
					TArray<FIntRect>& Viewports = ConverterParams->Viewports.Add(CurrentMipLevel);
					FIntRect Viewport;
						
					Viewport.Min = FIntPoint(0,0);
					Viewport.Max = CurrentMipDim;
					Viewports.Add(MoveTemp(Viewport));

					ReadResult = ReadInChunks(MipDataPtr, ImagePath, FrameId, CurrentMipDim, BufferSize);
				}

				if (ReadResult != Fail)
				{
					OutFrame->MipTilesPresent.Emplace(CurrentMipLevel, CurrentTileSelection);
				}
			}
			else
			{
				UE_LOG(LogImgMedia, Error, TEXT("Could not load %s"), *ImagePath);
				return false;
			}
			if (ReadResult == Fail)
			{
				// Check if we have a compressed file.
				FImgMediaFrameInfo Info;
				if (GetInfo(ImagePath, Info))
				{
					if (Info.CompressionName != "Uncompressed")
					{
						UE_LOG(LogImgMedia, Error, TEXT("GPU Reader cannot read compressed file %s."), *ImagePath);
						UE_LOG(LogImgMedia, Error, TEXT("Compressed and uncompressed files should not be mixed in a single sequence."));
					}
				}

				// Fall back to CPU.
				bFallBackToCPU = true;

				// To make sure that Media Texture doesn't call the converter if this frame is invalid.
				OutFrame->SampleConverter.Reset();

				return FExrImgMediaReader::ReadFrame(FrameId, InMipTiles, OutFrame);
			}
		}
	}

	OutFrame->Format = ConverterParams->FrameInfo.NumChannels <= 3 ? EMediaTextureSampleFormat::FloatRGB : EMediaTextureSampleFormat::FloatRGBA;
	OutFrame->Stride = ConverterParams->FullResolution.X * ConverterParams->PixelSize;

	CreateSampleConverterCallback(SampleConverter, ConverterParams);

	UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Read Pixels Complete. %i"), this, FrameId);
	return true;
}

void FExrImgMediaReaderGpu::PreAllocateMemoryPool(int32 NumFrames, const FImgMediaFrameInfo& FrameInfo, const bool bCustomExr)
{
	SIZE_T AllocSize = GetBufferSize(FrameInfo.Dim, FrameInfo.NumChannels, FrameInfo.bHasTiles, FrameInfo.NumTiles, bCustomExr);
	for (int32 FrameCacheNum = 0; FrameCacheNum < NumFrames; FrameCacheNum++)
	{
		AllocateGpuBufferFromPool(AllocSize, FrameCacheNum == NumFrames - 1);
	}
}


void FExrImgMediaReaderGpu::OnTick()
{
	// Only tick once per frame.
	if (LastTickedFrameCounter != GFrameCounter)
	{
		LastTickedFrameCounter = GFrameCounter;

		TransferFromStagingBuffer();
	}
}

/* FExrImgMediaReaderGpu implementation
 *****************************************************************************/

FExrImgMediaReaderGpu::EReadResult FExrImgMediaReaderGpu::ReadInChunks(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntPoint& Dim, int32 BufferSize)
{
	EReadResult bResult = Success;

	// Chunks are of 16 MB
	const int32 ChunkSize = 0xF42400;
	const int32 Remainder = BufferSize % ChunkSize;
	const int32 NumChunks = (BufferSize - Remainder) / ChunkSize;
	int32 CurrentBufferPos = 0;
	FExrReader ChunkReader;

	// Since ReadInChunks is only utilized for exr files without tiles and mips, Num Mip levels is always 1.
	const int32 NumLevels = 1;
	TArray<int32> NumTOffsetsPerLevel;
	NumTOffsetsPerLevel.Add(Dim.Y);
	if (!ChunkReader.OpenExrAndPrepareForPixelReading(ImagePath, NumTOffsetsPerLevel, TArray<TArray<int64>>()))
	{
		return Fail;
	}

	for (int32 Row = 0; Row <= NumChunks; Row++)
	{
		int32 Step = Row == NumChunks ? Remainder : ChunkSize;
		if (Step == 0)
		{
			break;
		}

		// Check to see if the frame was canceled.
		{
			FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
			if (CanceledFrames.Remove(FrameId) > 0)
			{
				UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Canceling Frame %i At chunk # %i"), this, FrameId, Row);
				bResult = Cancelled;
				break;
			}
		}

		if (!ChunkReader.ReadExrImageChunk(reinterpret_cast<char*>(Buffer) + CurrentBufferPos, Step))
		{
			bResult = Fail;
			break;
		}
		CurrentBufferPos += Step;
	}

	if (!ChunkReader.CloseExrFile())
	{
		return Fail;
	}

	return bResult;
}

SIZE_T FExrImgMediaReaderGpu::GetBufferSize(const FIntPoint& Dim, int32 NumChannels, bool bHasTiles, const FIntPoint& TileNum, const bool bCustomExr)
{
	if (!bHasTiles && !bCustomExr)
	{
		/** 
		* Reading scanlines.
		* 
		* At the beginning of each row of B G R channel planes there is 2x4 byte data that has information
		* about number of pixels in the current row and row's number.
		*/
		const uint16 Padding = FExrReader::PLANAR_RGB_SCANLINE_PADDING;
		SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + Dim.Y * Padding;
		return BufferSize;
	}
	else
	{
		/** 
		* Reading tiles.
		* 
		* At the beginning of each tile there is 20 byte data that has information
		* about number contents of tiles.
		*/
		const uint16 Padding = bCustomExr ? 0 : FExrReader::TILE_PADDING;
		SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + (TileNum.X * TileNum.Y) * Padding;
		return BufferSize;
	}

}

void FExrImgMediaReaderGpu::CreateSampleConverterCallback
	( FExrMediaTextureSampleConverter* SampleConverter
	, TSharedPtr<FSampleConverterParameters> ConverterParams)
{
	auto RenderThreadSwizzler = [ConverterParams] (FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef RenderTargetTextureRHI, TMap<int32, FStructuredBufferPoolItemSharedPtr>& MipBuffers)->bool
	{
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_Convert);
		SCOPED_GPU_STAT(RHICmdList, ExrImgMediaReaderGpu);

		FIntPoint Dim = ConverterParams->FullResolution;
		for (TPair<int32, TArray<FIntRect>> MipLevelViewports : ConverterParams->Viewports)
		{
			int32 MipLevel = MipLevelViewports.Key;
			SCOPED_GPU_STAT(RHICmdList, ExrImgMediaReaderGpu_MipRender);
			int MipLevelDiv = 1 << MipLevelViewports.Key;
			Dim = ConverterParams->FullResolution / MipLevelDiv;

			FStructuredBufferPoolItemSharedPtr BufferData = MipBuffers[MipLevel];
			if (BufferData.IsValid())
			{
				if (!BufferData->BufferRef->IsValid())
				{
					continue;
				}

				// This flag will indicate that we should wait for poll to complete.
				BufferData->bWillBeSignaled = true;

				FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store, nullptr, MipLevel);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("ExrTextureSwizzle"));

				FExrSwizzlePS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FExrSwizzlePS::FRgbaSwizzle>(ConverterParams->FrameInfo.NumChannels - 1);
				PermutationVector.Set<FExrSwizzlePS::FRenderTiles>(ConverterParams->FrameInfo.bHasTiles || ConverterParams->bCustomExr);
				PermutationVector.Set<FExrSwizzlePS::FCustomExr>(ConverterParams->bCustomExr);
				PermutationVector.Set<FExrSwizzlePS::FPartialTiles>(false);

				FExrSwizzlePS::FParameters Parameters = FExrSwizzlePS::FParameters();
				Parameters.TextureSize = Dim;
				Parameters.TileSize = ConverterParams->TileDimWithBorders;
				Parameters.NumChannels = ConverterParams->FrameInfo.NumChannels;
				if (ConverterParams->FrameInfo.bHasTiles)
				{
					Parameters.NumTiles = FIntPoint(FMath::CeilToInt(float(Dim.X) / ConverterParams->TileDimWithBorders.X), FMath::CeilToInt(float(Dim.Y) / ConverterParams->TileDimWithBorders.Y));
				}

				if (ConverterParams->FrameInfo.bHasTiles && 
					(ConverterParams->TileInfoPerMipLevel.Num() > MipLevel && ConverterParams->TileInfoPerMipLevel[MipLevel].Num() > 0))
				{
					TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("FExrImgMediaReaderGpu_TileDesc")));

					FBufferRHIRef BufferRef;
					FRHIResourceCreateInfo CreateInfo(TEXT("FExrImgMediaReaderGpu_TileDesc"));
					const uint32 BytesPerElement = sizeof(FExrReader::FTileDesc);
					const uint32 NumElements = ConverterParams->TileInfoPerMipLevel[MipLevel].Num();

					BufferRef = RHICreateStructuredBuffer(BytesPerElement, BytesPerElement*NumElements, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM, CreateInfo);
					void* MappedBuffer = RHILockBuffer(BufferRef, 0, NumElements, RLM_WriteOnly);
					FMemory::Memcpy(MappedBuffer, &ConverterParams->TileInfoPerMipLevel[MipLevel][0], sizeof(FExrReader::FTileDesc) * ConverterParams->TileInfoPerMipLevel[MipLevel].Num());
					RHIUnlockBuffer(BufferRef);
					Parameters.TileDescBuffer = RHICreateShaderResourceView(BufferRef);
					PermutationVector.Set<FExrSwizzlePS::FPartialTiles>(true);
				}

				Parameters.UnswizzledBuffer = RHICreateShaderResourceView(BufferData->BufferRef);

				FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

				TShaderMapRef<FExrSwizzleVS> SwizzleShaderVS(ShaderMap);
				TShaderMapRef<FExrSwizzlePS> SwizzleShaderPS(ShaderMap, PermutationVector);

				FScreenPassPipelineState PipelineState(SwizzleShaderVS, SwizzleShaderPS, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());
				
				// If there are tiles determines if we should deliver tiles one by one or in a bulk.
				for (const FIntRect& Viewport : MipLevelViewports.Value)
				{
					DrawScreenPass(RHICmdList, Dim, Viewport, PipelineState, [&](FRHICommandListImmediate& RHICmdList)
						{
							SetShaderParameters(RHICmdList, SwizzleShaderPS, SwizzleShaderPS.GetPixelShader(), Parameters);
						});
				}

				// Resolve render target.
				RHICmdList.EndRenderPass();

				// Mark this render command for this buffer as complete, so we can poll it and transfer later.
				RHICmdList.WriteGPUFence(BufferData->Fence);
			}

			
		}

		//Doesn't need further conversion so returning false.
		return false;
	};

	// Stacks up converters for each tile region.
	SampleConverter->AddCallback(FExrConvertBufferCallback::CreateLambda(RenderThreadSwizzler));
}

FStructuredBufferPoolItemSharedPtr FExrImgMediaReaderGpu::AllocateGpuBufferFromPool(uint32 AllocSize, bool bWait)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("FExrImgMediaReaderGpu_AllocBuffer %d"), AllocSize));

	// This function is attached to the shared pointer and is used to return any allocated memory to staging pool.
	auto BufferDeleter = [AllocSize](FStructuredBufferPoolItem* ObjectToDelete) {
		ReturnGpuBufferToStagingPool(AllocSize, ObjectToDelete);
	};

	// Buffer that ends up being returned out of this function.
	FStructuredBufferPoolItemSharedPtr AllocatedBuffer;

	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		FStructuredBufferPoolItem** FoundBuffer = MemoryPool.Find(AllocSize);
		if (FoundBuffer)
		{
			AllocatedBuffer = MakeShareable(*FoundBuffer, MoveTemp(BufferDeleter));
			MemoryPool.Remove(AllocSize, *FoundBuffer);
		}
	}

	if (!AllocatedBuffer)
	{
		// This boolean value is used to wait until render thread finishes buffer initialization.
		volatile bool bInitDone = false;
		{
			AllocatedBuffer = MakeShareable(new FStructuredBufferPoolItem(), MoveTemp(BufferDeleter));
			AllocatedBuffer->Reader = AsShared();

			// Allocate and unlock the structured buffer on render thread.
			ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([AllocatedBuffer, AllocSize, &bInitDone, this, bWait](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock ScopeLock(&AllocatorCriticalSecion);
				SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_AllocateBuffer);
				FRHIResourceCreateInfo CreateInfo(TEXT("FExrImgMediaReaderGpu"));
				AllocatedBuffer->BufferRef = RHICreateStructuredBuffer(sizeof(uint16) * 2., AllocSize, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM, CreateInfo);
				AllocatedBuffer->MappedBuffer = RHILockBuffer(AllocatedBuffer->BufferRef, 0, AllocSize, RLM_WriteOnly);
				AllocatedBuffer->Fence = RHICreateGPUFence(TEXT("BufferNoLongerInUseFence"));
				if (bWait)
				{
					bInitDone = true;
				}
			});
		}

		/** Wait until buffer is initialized. */
		while (!bInitDone && bWait)
		{
			FPlatformProcess::Sleep(0.01f);
		}

	}

	// This buffer will be automatically processed and returned to StagingMemoryPool once nothing keeps reference to it.
	return AllocatedBuffer;
}

void FExrImgMediaReaderGpu::ReturnGpuBufferToStagingPool(uint32 AllocSize, FStructuredBufferPoolItem* Buffer)
{
	TSharedPtr< FExrImgMediaReaderGpu, ESPMode::ThreadSafe> Reader = Buffer->Reader.Pin();

	// If reader is being deleted, we don't need to return the memory into staging buffer and instead should delete it.
	if ((Reader.IsValid() == false) || (Reader->bIsShuttingDown))
	{
		ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([Buffer](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_ReleaseBuffer);

			// By this point we don't need a lock because the destructor was already called and it 
			// is guaranteed that this buffer is no longer used anywhere else.
			RHIUnlockBuffer(Buffer->BufferRef);
			delete Buffer;
		});
	}
	else
	{
		FScopeLock ScopeLock(&Reader->AllocatorCriticalSecion);

		// We don't need to process this pooled buffer if the Reader is being destroyed.
		Reader->StagingMemoryPool.Add(AllocSize, Buffer);
	}

}

void FExrImgMediaReaderGpu::TransferFromStagingBuffer()
{
	ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([&, this](FRHICommandListImmediate& RHICmdList)
	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_TransferFromStagingBuffer);

		TArray<uint32> KeysForIteration;
		StagingMemoryPool.GetKeys(KeysForIteration);
		for (uint32 Key : KeysForIteration)
		{
			TArray<FStructuredBufferPoolItem*> AllValues;
			StagingMemoryPool.MultiFind(Key, AllValues);
			for (FStructuredBufferPoolItem* MemoryPoolItem : AllValues)
			{
				// Check if fence has signaled. Or otherwise if we are waiting for signal to come through.
				if (MemoryPoolItem->Fence->Poll() || !MemoryPoolItem->bWillBeSignaled)
				{
					// If buffer was in use but the fence signaled we need to reset bWillBeSignaled flag.
					MemoryPoolItem->bWillBeSignaled = false;

					StagingMemoryPool.Remove(Key, MemoryPoolItem);
					MemoryPool.Add(Key, MemoryPoolItem);
				}
			}
		}
	});
}


/* FExrMediaTextureSampleConverter implementation
 *****************************************************************************/

bool FExrMediaTextureSampleConverter::Convert(FTexture2DRHIRef& InDstTexture, const FConversionHints& Hints)
{
	FScopeLock ScopeLock(&ConverterCallbacksCriticalSection);
	bool bExecutionSuccessful = false;
	if (ConvertExrBufferCallback.IsBound())
	{
		bExecutionSuccessful = ConvertExrBufferCallback.Execute(FRHICommandListExecutor::GetImmediateCommandList(), InDstTexture, MipBuffers);
	}
	return bExecutionSuccessful;
}

#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM && PLATFORM_WINDOWS

