// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixerBuffer.h"
#include "AudioMixerSourceManager.h"

namespace Audio
{
	struct FMixerSourceVoiceBuffer;
	struct FMixerSourceVoiceFilterParams;
	struct FMixerSourceVoiceInitParams;
	class FMixerDevice;
	class FMixerSubmix;
	class FMixerSource;
	class FMixerSourceManager;
	class ISourceBufferQueueListener;


	class FMixerSourceVoice
	{
	public:
		FMixerSourceVoice();
		~FMixerSourceVoice();

		// Resets the source voice state
		void Reset(FMixerDevice* InMixerDevice);

		// Initializes the mixer source voice
		bool Init(const FMixerSourceVoiceInitParams& InFormat);

		// Releases the source voice back to the source buffer pool
		void Release();

		// Sets the source voice pitch value.
		void SetPitch(const float InPitch);

		// Sets the source voice volume value.
		void SetVolume(const float InVolume);

		// Sets the source voice distance attenuation.
		void SetDistanceAttenuation(const float InDistanceAttenuation);
		
		// Sets the source voice's LPF filter frequency.
		void SetLPFFrequency(const float InFrequency);

		// Sets the source voice's HPF filter frequency.
		void SetHPFFrequency(const float InFrequency);

		// Sets the source voice's channel map (2d or 3d).
		void SetChannelMap(ESubmixChannelFormat InChannelType, const Audio::AlignedFloatBuffer& InChannelMap, const bool bInIs3D, const bool bInIsCenterChannelOnly);

		// Sets params used by HRTF spatializer
		void SetSpatializationParams(const FSpatializationParams& InParams);

		// Starts the source voice generating audio output into it's submix.
		void Play();

		// Pauses the source voice (i.e. stops generating output but keeps its state as "active and playing". Can be restarted.)
		void Pause();

		// Immediately stops the source voice (no longer playing or active, can't be restarted.)
		void Stop();

		// Does a faded stop (to avoid discontinuity)
		void StopFade(int32 NumFrames);

		// Queries if the voice is playing
		bool IsPlaying() const;

		// Queries if the voice is paused
		bool IsPaused() const;

		// Queries if the source voice is active.
		bool IsActive() const;

		// Queries if the source has finished its fade out.
		bool IsStopFadedOut() const { return bStopFadedOut; }

		// Whether or not the device changed and needs another speaker map sent
		bool NeedsSpeakerMap() const;

		// Retrieves the total number of samples played.
		int64 GetNumFramesPlayed() const;

		// Retrieves the envelope value of the source.
		float GetEnvelopeValue() const;

		// Mixes the dry and wet buffer audio into the given buffers.
		void MixOutputBuffers(const ESubmixChannelFormat InSubmixChannelType, const float SendLevel, AlignedFloatBuffer& OutWetBuffer) const;

		// Sets the submix send levels
		void SetSubmixSendInfo(FMixerSubmixWeakPtr Submix, const float SendLevel);

		// Called when the source is a bus and needs to mix other sources together to generate output
		void OnMixBus(FMixerSourceVoiceBuffer* OutMixerSourceBuffer);

	private:

		friend class FMixerSourceManager;

		FMixerSourceManager* SourceManager;
		TMap<uint32, FMixerSourceSubmixSend> SubmixSends;
		FMixerDevice* MixerDevice;
		TMap<ESubmixChannelFormat, TArray<float>> ChannelMaps;
		FThreadSafeBool bStopFadedOut;
		float Pitch;
		float Volume;
		float DistanceAttenuation;
		float Distance;
		float LPFFrequency;
		float HPFFrequency;
		int32 SourceId;
		uint16 bIsPlaying : 1;
		uint16 bIsPaused : 1;
		uint16 bIsActive : 1;
		uint16 bOutputToBusOnly : 1;
		uint16 bIsBus : 1;
	};

}
