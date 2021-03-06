// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderH264Wrapper.h"
#include "Stats.h"
#include "VideoEncoderFactory.h"
#include "VideoEncoderFactorySimple.h"
#include "FrameBufferH264.h"
#include "FrameAdapterH264.h"

namespace UE::PixelStreaming
{
	FVideoEncoderH264Wrapper::FVideoEncoderH264Wrapper(TUniquePtr<FEncoderFrameFactory> InFrameFactory, TUniquePtr<AVEncoder::FVideoEncoder> InEncoder)
		: FrameFactory(MoveTemp(InFrameFactory))
		, Encoder(MoveTemp(InEncoder)){
			checkf(Encoder, TEXT("Encoder is nullptr."))
		}

		FVideoEncoderH264Wrapper::~FVideoEncoderH264Wrapper()
	{
		Encoder->ClearOnEncodedPacket();
		Encoder->Shutdown();
	}

	void FVideoEncoderH264Wrapper::Encode(const webrtc::VideoFrame& WebRTCFrame, bool bKeyframe)
	{
		FFrameBufferH264* FrameBuffer = static_cast<FFrameBufferH264*>(WebRTCFrame.video_frame_buffer().get());
		checkf(FrameBuffer->GetFrameBufferType() == EPixelStreamingFrameBufferType::Layer, TEXT("FVideoEncoderH264Wrapper::Encode(): Supplied frame buffer is of incorrect type."));

		AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig = GetCurrentConfig();
		TSharedPtr<AVEncoder::FVideoEncoderInputFrame> EncoderInputFrame = FrameFactory->GetFrameAndSetTexture(FrameBuffer->GetAdaptedLayer()->GetFrameTexture());
		if (EncoderInputFrame)
		{
			EncoderInputFrame->SetTimestampUs(WebRTCFrame.timestamp_us());
			EncoderInputFrame->SetTimestampRTP(WebRTCFrame.timestamp());
			EncoderInputFrame->SetFrameID(WebRTCFrame.id());

			AVEncoder::FVideoEncoder::FEncodeOptions Options;
			Options.bForceKeyFrame = bKeyframe || bForceNextKeyframe;
			bForceNextKeyframe = false;

			Encoder->Encode(EncoderInputFrame, Options);
		}
	}

	AVEncoder::FVideoEncoder::FLayerConfig FVideoEncoderH264Wrapper::GetCurrentConfig()
	{
		checkf(Encoder, TEXT("Cannot request layer config when encoder is nullptr."));

		// Asume user wants config for layer zero.
		return Encoder->GetLayerConfig(0);
	}

	void FVideoEncoderH264Wrapper::SetConfig(const AVEncoder::FVideoEncoder::FLayerConfig& NewConfig)
	{
		checkf(Encoder, TEXT("Cannot set layer config when encoder is nullptr."));
		AVEncoder::FVideoEncoder::FLayerConfig CurrentConfig = GetCurrentConfig();
		// Assumer user wants to update layer zero.
		if (NewConfig != CurrentConfig)
		{
			if (Encoder)
			{
				Encoder->UpdateLayerConfig(0, NewConfig);
			}
		}
	}

	/* ------------------ Static functions below --------------------- */

	void FVideoEncoderH264Wrapper::OnEncodedPacket(FVideoEncoderFactorySimple* Factory, uint32 InLayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InFrame, const AVEncoder::FCodecPacket& InPacket)
	{
		webrtc::EncodedImage Image;

#if WEBRTC_VERSION == 84
		webrtc::RTPFragmentationHeader FragHeader;
		//CreateH264FragmentHeader(InPacket.Data.Get(), InPacket.DataSize, FragHeader);

		std::vector<webrtc::H264::NaluIndex> NALUIndices = webrtc::H264::FindNaluIndices(InPacket.Data.Get(), InPacket.DataSize);
		FragHeader.VerifyAndAllocateFragmentationHeader(NALUIndices.size());
		FragHeader.fragmentationVectorSize = static_cast<uint16_t>(NALUIndices.size());
		for (int i = 0; i != NALUIndices.size(); ++i)
		{
			webrtc::H264::NaluIndex const& NALUIndex = NALUIndices[i];
			FragHeader.fragmentationOffset[i] = NALUIndex.payload_start_offset;
			FragHeader.fragmentationLength[i] = NALUIndex.payload_size;
		}
#endif
		Image.timing_.packetization_finish_ms = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
		Image.timing_.encode_start_ms = InPacket.Timings.StartTs.GetTotalMilliseconds();
		Image.timing_.encode_finish_ms = InPacket.Timings.FinishTs.GetTotalMilliseconds();
		Image.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

		Image.SetEncodedData(webrtc::EncodedImageBuffer::Create(const_cast<uint8_t*>(InPacket.Data.Get()), InPacket.DataSize));
		Image._encodedWidth = InFrame->GetWidth();
		Image._encodedHeight = InFrame->GetHeight();
		Image._frameType = InPacket.IsKeyFrame ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
		Image.content_type_ = webrtc::VideoContentType::UNSPECIFIED;
		Image.qp_ = InPacket.VideoQP;
		Image.SetSpatialIndex(InLayerIndex);
#if WEBRTC_VERSION == 84
		Image._completeFrame = true;
#endif
		Image.rotation_ = webrtc::VideoRotation::kVideoRotation_0;
		Image.SetTimestamp(InFrame->GetTimestampRTP());
		Image.capture_time_ms_ = InFrame->GetTimestampUs() / 1000.0;

		webrtc::CodecSpecificInfo CodecInfo;
		CodecInfo.codecType = webrtc::VideoCodecType::kVideoCodecH264;
		CodecInfo.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
		CodecInfo.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
		CodecInfo.codecSpecific.H264.idr_frame = InPacket.IsKeyFrame;
		CodecInfo.codecSpecific.H264.base_layer_sync = false;

#if WEBRTC_VERSION == 84
		Factory->OnEncodedImage(Image, &CodecInfo, &FragHeader);
#elif WEBRTC_VERSION == 96
		Factory->OnEncodedImage(Image, &CodecInfo);
#endif
	}
} // namespace UE::PixelStreaming