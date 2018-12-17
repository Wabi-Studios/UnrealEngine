// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCProxyCommon.h"

class FNetworkAudioCapturer : public webrtc::AudioDeviceModule
{
public:
	void ProcessPacket(PixelStreamingProtocol::EToProxyMsg PkType, const void* Data, uint32_t Size);

private:
	//
	// webrtc::AudioDeviceModule interface
	//
	int32_t ActiveAudioLayer(AudioLayer* audioLayer) const override;
	int32_t RegisterAudioCallback(webrtc::AudioTransport* audioCallback) override;

	// Main initialization and termination
	int32_t Init() override;
	int32_t Terminate() override;
	bool Initialized() const override;

	// Device enumeration
	int16_t PlayoutDevices() override;
	int16_t RecordingDevices() override;
	int32_t PlayoutDeviceName(
	    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize]) override;
	int32_t RecordingDeviceName(
	    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize], char guid[webrtc::kAdmMaxGuidSize]) override;

	// Device selection
	int32_t SetPlayoutDevice(uint16_t index) override;
	int32_t SetPlayoutDevice(WindowsDeviceType device) override;
	int32_t SetRecordingDevice(uint16_t index) override;
	int32_t SetRecordingDevice(WindowsDeviceType device) override;

	// Audio transport initialization
	int32_t PlayoutIsAvailable(bool* available) override;
	int32_t InitPlayout() override;
	bool PlayoutIsInitialized() const override;
	int32_t RecordingIsAvailable(bool* available) override;
	int32_t InitRecording() override;
	bool RecordingIsInitialized() const override;

	// Audio transport control
	virtual int32_t StartPlayout() override;
	virtual int32_t StopPlayout() override;
	virtual bool Playing() const override;
	virtual int32_t StartRecording() override;
	virtual int32_t StopRecording() override;
	virtual bool Recording() const override;

	// Audio mixer initialization
	virtual int32_t InitSpeaker() override;
	virtual bool SpeakerIsInitialized() const override;
	virtual int32_t InitMicrophone() override;
	virtual bool MicrophoneIsInitialized() const override;

	// Speaker volume controls
	virtual int32_t SpeakerVolumeIsAvailable(bool* available) override
	{
		return -1;
	}
	virtual int32_t SetSpeakerVolume(uint32_t volume) override
	{
		return -1;
	}
	virtual int32_t SpeakerVolume(uint32_t* volume) const override
	{
		return -1;
	}
	virtual int32_t MaxSpeakerVolume(uint32_t* maxVolume) const override
	{
		return -1;
	}
	virtual int32_t MinSpeakerVolume(uint32_t* minVolume) const override
	{
		return -1;
	}

	// Microphone volume controls
	virtual int32_t MicrophoneVolumeIsAvailable(bool* available) override
	{
		return -1;
	}
	virtual int32_t SetMicrophoneVolume(uint32_t volume) override
	{
		return -1;
	}
	virtual int32_t MicrophoneVolume(uint32_t* volume) const override
	{
		return -1;
	}
	virtual int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override
	{
		return -1;
	}
	virtual int32_t MinMicrophoneVolume(uint32_t* minVolume) const override
	{
		return -1;
	}

	// Speaker mute control
	virtual int32_t SpeakerMuteIsAvailable(bool* available) override
	{
		return -1;
	}
	virtual int32_t SetSpeakerMute(bool enable) override
	{
		return -1;
	}
	virtual int32_t SpeakerMute(bool* enabled) const override
	{
		return -1;
	}

	// Microphone mute control
	virtual int32_t MicrophoneMuteIsAvailable(bool* available) override
	{
		return -1;
	}
	virtual int32_t SetMicrophoneMute(bool enable) override
	{
		return -1;
	}
	virtual int32_t MicrophoneMute(bool* enabled) const override
	{
		return -1;
	}

	// Stereo support
	virtual int32_t StereoPlayoutIsAvailable(bool* available) const override;
	virtual int32_t SetStereoPlayout(bool enable) override;
	virtual int32_t StereoPlayout(bool* enabled) const override;
	virtual int32_t StereoRecordingIsAvailable(bool* available) const override;
	virtual int32_t SetStereoRecording(bool enable) override;
	virtual int32_t StereoRecording(bool* enabled) const override;

	// Playout delay
	virtual int32_t PlayoutDelay(uint16_t* delayMS) const override
	{
		return -1;
	}

	// Only supported on Android.
	virtual bool BuiltInAECIsAvailable() const override
	{
		return false;
	}
	virtual bool BuiltInAGCIsAvailable() const override
	{
		return false;
	}
	virtual bool BuiltInNSIsAvailable() const override
	{
		return false;
	}

	// Enables the built-in audio effects. Only supported on Android.
	virtual int32_t EnableBuiltInAEC(bool enable) override
	{
		return -1;
	}
	virtual int32_t EnableBuiltInAGC(bool enable) override
	{
		return -1;
	}
	virtual int32_t EnableBuiltInNS(bool enable) override
	{
		return -1;
	}

	std::atomic<bool> bInitialized = false;
	std::unique_ptr<webrtc::AudioDeviceBuffer> DeviceBuffer;

	std::vector<uint8_t> Tempbuf;
	std::vector<uint8_t> RecordingBuffer;
	int RecordingBufferSize = 0;

	std::atomic<bool> bRecordingInitialized = false;
	int SampleRate = 48000;
	int Channels = 2;
};
