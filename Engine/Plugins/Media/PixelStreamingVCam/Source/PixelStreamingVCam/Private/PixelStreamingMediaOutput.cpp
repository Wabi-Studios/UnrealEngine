// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingMediaOutput.h"
#include "PixelStreamingMediaCapture.h"
#include "IPixelStreamingModule.h"

void UPixelStreamingMediaOutput::BeginDestroy()
{
	StopStreaming();
	Streamer = nullptr;
	Super::BeginDestroy();
}

UMediaCapture* UPixelStreamingMediaOutput::CreateMediaCaptureImpl()
{
	if (!Streamer.IsValid())
	{
		IPixelStreamingModule& Module = FModuleManager::LoadModuleChecked<IPixelStreamingModule>("PixelStreaming");
		Streamer = Module.CreateStreamer(StreamID);
	}

	Capture = nullptr;
	if (Streamer.IsValid())
	{
		Capture = NewObject<UPixelStreamingMediaCapture>();
		Capture->SetMediaOutput(this);
		Capture->OnStateChangedNative.AddLambda([this]() { OnCaptureStateChanged(); });
	}
	return Capture;
}

void UPixelStreamingMediaOutput::OnCaptureStateChanged()
{
	switch (Capture->GetState())
	{
		case EMediaCaptureState::Capturing:
			StartStreaming();
			break;
		case EMediaCaptureState::Stopped:
		case EMediaCaptureState::Error:
			StopStreaming();
			break;
		default:
			break;
	}
}

void UPixelStreamingMediaOutput::StartStreaming()
{
	if (Streamer)
	{
		Streamer->SetVideoInput(Capture->GetVideoInput());
		Streamer->SetTargetViewport(Capture->GetViewport());
		Streamer->SetSignallingServerURL(SignallingServerURL);
		Streamer->StartStreaming();
	}
}

void UPixelStreamingMediaOutput::StopStreaming()
{
	if (Streamer)
	{
		Streamer->StopStreaming();
		Streamer->SetTargetViewport(nullptr);
	}
}

void UPixelStreamingMediaOutput::SetSignallingServerURL(FString InURL)
{
	SignallingServerURL = InURL;
}

void UPixelStreamingMediaOutput::SetSignallingStreamID(FString InStreamID)
{
	StreamID = InStreamID;
}