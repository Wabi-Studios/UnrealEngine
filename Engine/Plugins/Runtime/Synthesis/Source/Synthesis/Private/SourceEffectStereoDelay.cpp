// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SourceEffects/SourceEffectStereoDelay.h"
#include "Templates/Casts.h"

void FSourceEffectStereoDelay::Init(const FSoundEffectSourceInitData& InitData)
{
	bIsActive = true;
	DelayStereo.Init(InitData.SampleRate, InitData.NumSourceChannels);
}

void FSourceEffectStereoDelay::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SourceEffectStereoDelay);

	DelayStereo.SetDelayTimeMsec(Settings.DelayTimeMsec);
	DelayStereo.SetFeedback(Settings.Feedback);
	DelayStereo.SetWetLevel(Settings.WetLevel);
	DelayStereo.SetDelayRatio(Settings.DelayRatio);
	DelayStereo.SetMode((Audio::EStereoDelayMode::Type)Settings.DelayMode);
}

void FSourceEffectStereoDelay::ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData)
{
	DelayStereo.ProcessAudio(InData.InputSourceEffectBufferPtr, InData.NumSamples, OutAudioBufferData);
}

void USourceEffectStereoDelayPreset::SetSettings(const FSourceEffectStereoDelaySettings& InSettings)
{
	UpdateSettings(InSettings);
}