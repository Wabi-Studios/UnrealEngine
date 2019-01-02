// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

#define WMFMEDIA_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && (WINVER >= 0x0600 /*Vista*/) && !UE_SERVER)

#if WMFMEDIA_SUPPORTED_PLATFORM
	#include "../../WmfMediaFactory/Public/WmfMediaSettings.h"

	#include "Windows/WindowsHWrapper.h"
	#include "Windows/AllowWindowsPlatformTypes.h"

	THIRD_PARTY_INCLUDES_START

	#include <windows.h>
	
	#include <D3D9.h>
	#include <mfapi.h>
	#include <mferror.h>
	#include <mfidl.h>
	#include <mmdeviceapi.h>
	#include <mmeapi.h>
	#include <nserror.h>
	#include <shlwapi.h>

	THIRD_PARTY_INCLUDES_END

	const GUID FORMAT_525WSS = { 0xc7ecf04d, 0x4582, 0x4869, { 0x9a, 0xbb, 0xbf, 0xb5, 0x23, 0xb6, 0x2e, 0xdf } };
	const GUID FORMAT_DvInfo = { 0x05589f84, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
	const GUID FORMAT_MPEG2Video = { 0xe06d80e3, 0xdb46, 0x11cf, { 0xb4, 0xd1, 0x00, 0x80, 0x05f, 0x6c, 0xbb, 0xea } };
	const GUID FORMAT_MPEGStreams = { 0x05589f83, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
	const GUID FORMAT_MPEGVideo = { 0x05589f82, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
	const GUID FORMAT_None = { 0x0F6417D6, 0xc318, 0x11d0, { 0xa4, 0x3f, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
	const GUID FORMAT_VideoInfo = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
	const GUID FORMAT_VideoInfo2 = { 0xf72a76A0, 0xeb0a, 0x11d0, { 0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
	const GUID FORMAT_WaveFormatEx = { 0x05589f81, 0xc356, 0x11ce,{ 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
	
	const GUID MR_VIDEO_ACCELERATION_SERVICE = { 0xefef5175, 0x5c7d, 0x4ce2, { 0xbb, 0xbd, 0x34, 0xff, 0x8b, 0xca, 0x65, 0x54 } };

	#if (WINVER < _WIN32_WINNT_WIN8)
		const GUID MF_LOW_LATENCY = { 0x9c27891a, 0xed7a, 0x40e1, { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };
	#endif

	#include "Windows/COMPointer.h"
	#include "Windows/HideWindowsPlatformTypes.h"

#elif PLATFORM_WINDOWS && !UE_SERVER
	#pragma message("Skipping WmfMedia (requires WINVER >= 0x0600, but WINVER is " PREPROCESSOR_TO_STRING(WINVER) ")")

#endif //WMFMEDIA_SUPPORTED_PLATFORM


/** Log category for the WmfMedia module. */
DECLARE_LOG_CATEGORY_EXTERN(LogWmfMedia, Log, All);
