// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXViewport.h: AGX RHI viewport definitions.
=============================================================================*/

#pragma once

#if PLATFORM_MAC
#include "Mac/CocoaTextView.h"
@interface FAGXView : FCocoaTextView
@end
#endif
#include "HAL/PlatformFramePacer.h"
THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

enum EAGXViewportAccessFlag
{
	EAGXViewportAccessRHI,
	EAGXViewportAccessRenderer,
	EAGXViewportAccessGame,
	EAGXViewportAccessDisplayLink
};

class FAGXCommandQueue;

typedef void (^FAGXViewportPresentHandler)(uint32 CGDirectDisplayID, double OutputSeconds, double OutputDuration);

class FAGXViewport : public FRHIViewport
{
public:
	FAGXViewport(void* WindowHandle, uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen,EPixelFormat Format);
	~FAGXViewport();

	void Resize(uint32 InSizeX, uint32 InSizeY, bool bInIsFullscreen,EPixelFormat Format);
	
	TRefCountPtr<FAGXSurface> GetBackBuffer(EAGXViewportAccessFlag Accessor) const;
	id<CAMetalDrawable> GetDrawable(EAGXViewportAccessFlag Accessor);
	id<MTLTexture> GetDrawableTexture(EAGXViewportAccessFlag Accessor);
	id<MTLTexture> GetCurrentTexture(EAGXViewportAccessFlag Accessor) const;
	void ReleaseDrawable(void);

	// supports pulling the raw MTLTexture
	virtual void* GetNativeBackBufferTexture() const override { return GetBackBuffer(EAGXViewportAccessRenderer).GetReference(); }
	virtual void* GetNativeBackBufferRT() const override { return (const_cast<FAGXViewport *>(this))->GetDrawableTexture(EAGXViewportAccessRenderer); }
	
#if PLATFORM_MAC
	virtual void SetCustomPresent(FRHICustomPresent* InCustomPresent) override
	{
		CustomPresent = InCustomPresent;
	}

	virtual FRHICustomPresent* GetCustomPresent() const override { return CustomPresent; }
#endif
	
	void Present(FAGXCommandQueue& CommandQueue, bool bLockToVsync);
	void Swap();
	
private:
	uint32 GetViewportIndex(EAGXViewportAccessFlag Accessor) const;

private:
	id<CAMetalDrawable> Drawable;
	TRefCountPtr<FAGXSurface> BackBuffer[2];
	mutable FCriticalSection Mutex;
	
	id<MTLTexture> DrawableTextures[2];
	
	uint32 DisplayID;
	FAGXViewportPresentHandler Block;
	volatile int32 FrameAvailable;
	TRefCountPtr<FAGXSurface> LastCompleteFrame;
	bool bIsFullScreen;

#if PLATFORM_MAC
	FAGXView* View;
	FRHICustomPresent* CustomPresent;
#endif
};

template<>
struct TAGXResourceTraits<FRHIViewport>
{
	typedef FAGXViewport TConcreteType;
};
