// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformCrashContext.h"

#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include "DbgHelp.h"
#include "Microsoft/HideMicrosoftPlatformTypes.h"

#if WER_CUSTOM_REPORTS
struct CORE_API FHoloLensPlatformCrashContext : public FGenericCrashContext
{
	/** Platform specific constants. */
	enum EConstants
	{
		UE4_MINIDUMP_CRASHCONTEXT = LastReservedStream + 1,
	};

	virtual void AddPlatformSpecificProperties() override
	{
		AddCrashProperty( TEXT( "Platform.IsRunningHoloLens" ), 1 );
	}
};

typedef FHoloLensPlatformCrashContext FPlatformCrashContext;

#endif