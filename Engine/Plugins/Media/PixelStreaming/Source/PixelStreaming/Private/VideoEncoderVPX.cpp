// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderVPX.h"
#include "FrameBufferI420.h"
#include "FrameAdapterI420.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	FVideoEncoderVPX::FVideoEncoderVPX(int VPXVersion)
	{
		if (VPXVersion == 8)
		{
			WebRTCVPXEncoder = webrtc::VP8Encoder::Create();
		}
		else if (VPXVersion == 9)
		{
			WebRTCVPXEncoder = webrtc::VP9Encoder::Create();
		}
	}

	int FVideoEncoderVPX::InitEncode(webrtc::VideoCodec const* InCodecSettings, VideoEncoder::Settings const& settings)
	{
		return WebRTCVPXEncoder->InitEncode(InCodecSettings, settings);
	}

	int32 FVideoEncoderVPX::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
	{
		return WebRTCVPXEncoder->RegisterEncodeCompleteCallback(callback);
	}

	int32 FVideoEncoderVPX::Release()
	{
		return WebRTCVPXEncoder->Release();
	}

	int32 FVideoEncoderVPX::Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types)
	{
		FFrameBufferI420* FrameBuffer = static_cast<FFrameBufferI420*>(frame.video_frame_buffer().get());

		FPixelStreamingFrameMetadata& FrameMetadata = FrameBuffer->GetAdaptedLayer()->Metadata;
		FrameMetadata.UseCount++;
		FrameMetadata.LastEncodeStartTime = FPlatformTime::Cycles64();
		if (FrameMetadata.UseCount == 1)
		{
			FrameMetadata.FirstEncodeStartTime = FrameMetadata.LastEncodeStartTime;
		}

		const int32 EncodeResult = WebRTCVPXEncoder->Encode(frame, frame_types);

		FrameBuffer->GetAdaptedLayer()->Metadata.LastEncodeEndTime = FPlatformTime::Cycles64();

		FStats::Get()->AddFrameTimingStats(FrameBuffer->GetAdaptedLayer()->Metadata);

		return EncodeResult;
	}

	// Pass rate control parameters from WebRTC to our encoder
	// This is how WebRTC can control the bitrate/framerate of the encoder.
	void FVideoEncoderVPX::SetRates(RateControlParameters const& parameters)
	{
		WebRTCVPXEncoder->SetRates(parameters);
	}

	webrtc::VideoEncoder::EncoderInfo FVideoEncoderVPX::GetEncoderInfo() const
	{
		VideoEncoder::EncoderInfo info = WebRTCVPXEncoder->GetEncoderInfo();
		info.supports_native_handle = true;
		return info;
	}
} // namespace UE::PixelStreaming
