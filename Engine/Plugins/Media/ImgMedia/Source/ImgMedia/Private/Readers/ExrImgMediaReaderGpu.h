// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ExrImgMediaReader.h"

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "IImgMediaReader.h"
#include "IMediaTextureSampleConverter.h"

class FExrImgMediaReaderGpu;
class FExrMediaTextureSampleConverter;

struct FStructuredBufferPoolItem
{
	/**
	* This is the actual buffer reference that we need to keep after it is locked and until it is unlocked.
	*/
	FBufferRHIRef BufferRef;

	/** 
	* A pointer to mapped GPU memory.
	*/
	void* MappedBuffer;

	/** 
	* A Gpu fence that identifies if this pool item is available for use again.
	*/
	FGPUFenceRHIRef Fence;

	/**
	* This boolean is used as a flag in combination with fences to indicate if rendering thread 
	* is currently using it.
	*/
	bool bWillBeSignaled = false;

	/**
	* Keep track of our reader in case it gets destroyed.
	*/
	TWeakPtr<FExrImgMediaReaderGpu, ESPMode::ThreadSafe> Reader;
};

/**
* A shared pointer that will be released automatically and returned to UsedPool
*/
typedef TSharedPtr<FStructuredBufferPoolItem, ESPMode::ThreadSafe> FStructuredBufferPoolItemSharedPtr;

/**
 * Implements a reader for EXR image sequences.
 */
class FExrImgMediaReaderGpu
	: public FExrImgMediaReader
	, public TSharedFromThis<FExrImgMediaReaderGpu, ESPMode::ThreadSafe>
{
public:

	/** Default constructor. */
	FExrImgMediaReaderGpu(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader):FExrImgMediaReader(InLoader),
		LastTickedFrameCounter((uint64)-1), bIsShuttingDown(false), bFallBackToCPU(false) {};
	virtual ~FExrImgMediaReaderGpu();

public:

	//~ FExrImgMediaReader interface
	virtual bool ReadFrame(int32 FrameId, const TMap<int32, FImgMediaTileSelection>& InMipTiles, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame) override;
	
	/**
	* For performance reasons we want to pre-allocate structured buffers to at least the number of concurrent frames.
	*/
	virtual void PreAllocateMemoryPool(int32 NumFrames, const FImgMediaFrameInfo& FrameInfo, const bool bCustomExr) override;
	virtual void OnTick() override;

protected:

	/** 
	 * This function reads file in 16 MB chunks and if it detects that
	 * Frame is pending for cancellation stops reading the file and returns false.
	*/
	EReadResult ReadInChunks(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntPoint& Dim, int32 BufferSize);

	/**
	 * Get the size of the buffer needed to load in an image.
	 * 
	 * @param Dim Dimensions of the image.
	 * @param NumChannels Number of channels in the image.
	 */
	static SIZE_T GetBufferSize(const FIntPoint& Dim, int32 NumChannels, bool bHasTiles, const FIntPoint& TileNum, const bool bCustomExr);

	/**
	* Creates Sample converter to be used by Media Texture Resource.
	*/
	static void CreateSampleConverterCallback
		(FExrMediaTextureSampleConverter* SampleConverter, TSharedPtr<FSampleConverterParameters> ConverterParams);

public:

	/** Typically we would need the (ImageResolution.x*y)*NumChannels*ChannelSize */
	FStructuredBufferPoolItemSharedPtr AllocateGpuBufferFromPool(uint32 AllocSize, bool bWait = true);

	/** Either return or Add new chunk of memory to the pool based on its size. */
	static void ReturnGpuBufferToStagingPool(uint32 AllocSize, FStructuredBufferPoolItem* Buffer);

	/** Transfer from Staging buffer to Memory pool. */
	void TransferFromStagingBuffer();

private:

	/** A critical section used for memory allocation and pool management. */
	FCriticalSection AllocatorCriticalSecion;

	/** Main memory pool from where we are allowed to take buffers. */
	TMultiMap<uint32, FStructuredBufferPoolItem*> MemoryPool;

	/** 
	* This pool could contain potentially in use buffers and every tick it is processed
	* and those buffers that are ready to be used returned back to Main memory pool
	*/
	TMultiMap<uint32, FStructuredBufferPoolItem*> StagingMemoryPool;

	/** Frame that was last ticked so we don't tick more than once. */
	uint64 LastTickedFrameCounter;

	/** A flag indicating this reader is being destroyed, therefore memory should not be returned. */
	bool bIsShuttingDown;

	/** If true, then just use the CPU to read the file. */
	bool bFallBackToCPU;
};

FUNC_DECLARE_DELEGATE(FExrConvertBufferCallback, bool, FRHICommandListImmediate& /*RHICmdList*/, FTexture2DRHIRef /*RenderTargetTextureRHI*/, TMap<int32, FStructuredBufferPoolItemSharedPtr>& /*MipBuffers*/ )

class FExrMediaTextureSampleConverter: public IMediaTextureSampleConverter
{

public:
	virtual bool Convert(FTexture2DRHIRef& InDstTexture, const FConversionHints& Hints) override;
	virtual ~FExrMediaTextureSampleConverter() {};
	
	void AddCallback(FExrConvertBufferCallback&& Callback) 
	{
		FScopeLock ScopeLock(&ConverterCallbacksCriticalSection);
		ConvertExrBufferCallback = Callback;
	};

	FStructuredBufferPoolItemSharedPtr GetMipLevelBuffer(int32 RequestedMipLevel)
	{
		FScopeLock ScopeLock(&ConverterCallbacksCriticalSection);
		if (MipBuffers.Contains(RequestedMipLevel))
		{
			return *MipBuffers.Find(RequestedMipLevel);
		}
		return nullptr;
	}

	void SetMipLevelBuffer(int32 RequestedMipLevel, FStructuredBufferPoolItemSharedPtr Buffer)
	{
		FScopeLock ScopeLock(&ConverterCallbacksCriticalSection);
		check(!MipBuffers.Contains(RequestedMipLevel));
		MipBuffers.Add(RequestedMipLevel, Buffer);
	}

private:
	FCriticalSection ConverterCallbacksCriticalSection;
	FExrConvertBufferCallback ConvertExrBufferCallback;

	/** An array of structured buffers that are big enough to fully contain corresponding mip levels. */
	TMap<int32,FStructuredBufferPoolItemSharedPtr> MipBuffers;
};

#endif //defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

