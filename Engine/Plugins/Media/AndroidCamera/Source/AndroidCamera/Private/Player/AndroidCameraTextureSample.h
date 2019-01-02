// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreGlobals.h"
#include "IMediaTextureSample.h"
#include "Math/IntPoint.h"
#include "Math/Range.h"
#include "MediaObjectPool.h"
#include "Misc/Timespan.h"
#include "RHI.h"
#include "RHIUtilities.h"
#include "Templates/SharedPointer.h"

#if !WITH_ENGINE
	#include "MediaObjectPool.h"
#endif


/**
 * Texture sample generated by AndroidCamera player.
 */
class FAndroidCameraTextureSample
	: public IMediaTextureSample
	, public IMediaPoolable
{
public:

	/** Default constructor. */
	FAndroidCameraTextureSample()
		: Buffer(nullptr)
		, BufferSize(0)
		, Dim(FIntPoint::ZeroValue)
		, Duration(FTimespan::Zero())
		, Time(FTimespan::Zero())
		, ScaleRotation(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f))
		, Offset(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
	{ }

	/** Virtual destructor. */
	virtual ~FAndroidCameraTextureSample()
	{
		if (BufferSize > 0)
		{
			FMemory::Free(Buffer);
		}
	}

public:

	/**
	 * Get a writable pointer to the sample buffer.
	 *
	 * @return Sample buffer.
	 */
	void* GetMutableBuffer()
	{
		return Buffer;
	}

	/**
	 * Initialize the sample.
	 *
	 * @param InDim The sample buffer's width and height (in pixels).
	 * @param InDuration The duration for which the sample is valid.
	 * @return true on success, false otherwise.
	 */
	bool Initialize(const FIntPoint& InDim, FTimespan InDuration)
	{
		if (InDim.GetMin() <= 0)
		{
			return false;
		}

		Dim = InDim;
		Duration = InDuration;

		return true;
	}

	/**
	 * Initialize the sample with a memory buffer.
	 *
	 * @param InBuffer The buffer containing the sample data.
	 * @param InTime The sample time (in the player's local clock).
	 * @param Copy Whether the buffer should be copied (true) or referenced (false).
	 * @see InitializeTexture
	 */
	void InitializeBuffer(void* InBuffer, FTimespan InTime, bool Copy)
	{
		Time = InTime;

		if (Copy)
		{
			const SIZE_T RequiredBufferSize = Dim.X * Dim.Y * sizeof(int32);

			if (BufferSize < RequiredBufferSize)
			{
				if (BufferSize == 0)
				{
					Buffer = FMemory::Malloc(RequiredBufferSize);
				}
				else
				{
					Buffer = FMemory::Realloc(Buffer, RequiredBufferSize);
				}

				BufferSize = RequiredBufferSize;
			}

			FMemory::Memcpy(Buffer, InBuffer, RequiredBufferSize);
		}
		else
		{
			if (BufferSize > 0)
			{
				FMemory::Free(Buffer);
				BufferSize = 0;
			}

			Buffer = InBuffer;
		}
	}

	/**
	 * Initialize the sample with a texture resource.
	 *
	 * @param InTime The sample time (in the player's local clock).
	 * @return The texture resource object that will hold the sample data.
	 * @note This method must be called on the render thread.
	 * @see InitializeBuffer
	 */
	FRHITexture2D* InitializeTexture(FTimespan InTime)
	{
		check(IsInRenderingThread() || IsInRHIThread());

		Time = InTime;

		if (Texture.IsValid() && (Texture->GetSizeXY() == Dim))
		{
			return Texture;
		}

		const uint32 CreateFlags = TexCreate_Dynamic | TexCreate_SRGB;

		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
		FRHIResourceCreateInfo CreateInfo;

		RHICreateTargetableShaderResource2D(
			Dim.X,
			Dim.Y,
			PF_B8G8R8A8,
			1,
			CreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			Texture,
			DummyTexture2DRHI
		);

		return Texture;
	}

	/**
	 * Set the sample Scale, Rotation, Offset.
	 *
	 * @param InScaleRotation The sample scale and rotation transform (2x2).
	 * @param InOffset The sample offset.
	 */
	void SetScaleRotationOffset(FVector4& InScaleRotation, FVector4& InOffset)
	{
		ScaleRotation = FLinearColor(InScaleRotation.X, InScaleRotation.Y, InScaleRotation.Z, InScaleRotation.W);
		Offset = FLinearColor(InOffset.X, InOffset.Y, InOffset.Z, InOffset.W);
	}

public:

	//~ IMediaTextureSample interface

	virtual const void* GetBuffer() override
	{
		return Buffer;
	}

	virtual FIntPoint GetDim() const override
	{
		return Dim;
	}

	virtual FTimespan GetDuration() const override
	{
		return Duration;
	}

	virtual EMediaTextureSampleFormat GetFormat() const override
	{
		return EMediaTextureSampleFormat::CharBGRA;
	}

	virtual FIntPoint GetOutputDim() const override
	{
		return Dim;
	}

	virtual uint32 GetStride() const override
	{
		return Dim.X * sizeof(int32);
	}

#if WITH_ENGINE

	virtual FRHITexture* GetTexture() const override
	{
		return Texture.GetReference();
	}

#endif //WITH_ENGINE

	virtual FTimespan GetTime() const override
	{
		return Time;
	}

	virtual bool IsCacheable() const override
	{
#if WITH_ENGINE
		return true;
#else
		return (BufferSize > 0);
#endif
	}

	virtual bool IsOutputSrgb() const override
	{
		return true;
	}

	virtual FLinearColor GetScaleRotation() const override
	{
		return ScaleRotation;
	}

	virtual FLinearColor GetOffset() const override
	{
		return Offset;
	}

private:

	/** The sample's data buffer. */
	void* Buffer;

	/** Current allocation size of Buffer. */
	SIZE_T BufferSize;

	/** Width and height of the texture sample. */
	FIntPoint Dim;

	/** Duration for which the sample is valid. */
	FTimespan Duration;

	/** Sample time. */
	FTimespan Time;

	/** ScaleRotation for the sample. */
	FLinearColor ScaleRotation;

	/** Offset for the sample. */
	FLinearColor Offset;

#if WITH_ENGINE

	/** Texture resource. */
	TRefCountPtr<FRHITexture2D> Texture;

#endif //WITH_ENGINE
};


/** Implements a pool for Android texture sample objects. */
class FAndroidCameraTextureSamplePool : public TMediaObjectPool<FAndroidCameraTextureSample> { };
