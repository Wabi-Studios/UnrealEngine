// Copyright Epic Games, Inc. All Rights Reserved.
#include "Sound/SoundSubmixSend.h"

#include "Sound/SoundSubmix.h"

FSoundSubmixSendInfo::FSoundSubmixSendInfo()
	: SendLevelControlMethod(ESendLevelControlMethod::Manual)
	, SendStage(ESubmixSendStage::PostDistanceAttenuation)
	, SoundSubmix(nullptr)
	, SendLevel(0.0f)
	, DisableManualSendClamp(false)
	, MinSendLevel(0.0f)
	, MaxSendLevel(1.0f)
	, MinSendDistance(100.0f)
	, MaxSendDistance(1000.0f)
	{
	}
