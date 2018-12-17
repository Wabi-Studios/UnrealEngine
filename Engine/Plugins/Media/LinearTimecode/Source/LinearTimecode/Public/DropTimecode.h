// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Timecode.h"
#include "DropTimecode.generated.h"

/** Hold a frame of a Linear Timecode Frame */

USTRUCT(BlueprintType)
struct LINEARTIMECODE_API FDropTimecode
{
	GENERATED_USTRUCT_BODY()

	/** Decoded Timecode */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	FTimecode Timecode;
	/** Guess at incoming frame rate */
	UPROPERTY(BlueprintReadWrite, Category = "Media")
	int32 FrameRate;
	/** Sync is in phase with color burst */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	bool bColorFraming;
	/** When timecode is reading forward */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	bool bRunningForward;
	/** Is a new timecode frame */
	UPROPERTY(BlueprintReadWrite, Category = "Time")
	bool bNewFrame;
};

