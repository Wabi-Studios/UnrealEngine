// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct APPLICATIONCORE_API FAndroidApplicationMisc : public FGenericPlatformApplicationMisc
{
	static void LoadPreInitModules();
	static class FFeedbackContext* GetFeedbackContext();
	static class FOutputDeviceError* GetErrorOutputDevice();
	static class GenericApplication* CreateApplication();
	static void RequestMinimize();
	static bool IsScreensaverEnabled();
	static bool ControlScreensaver(EScreenSaverAction Action);
	static void ResetGamepadAssignments();
	static void ResetGamepadAssignmentToController(int32 ControllerId);
	static bool IsControllerAssignedToGamepad(int32 ControllerId);
	static void ClipboardCopy(const TCHAR* Str);
	static void ClipboardPaste(class FString& Dest);
	static EScreenPhysicalAccuracy ComputePhysicalScreenDensity(int32& OutScreenDensity);
};

#if !PLATFORM_LUMIN
typedef FAndroidApplicationMisc FPlatformApplicationMisc;
#endif
