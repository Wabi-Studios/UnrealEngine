// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneVectorSection.h"
#include "UObject/StructOnScope.h"
#include "UObject/SequencerObjectVersion.h"
#include "Channels/MovieSceneChannelProxy.h"

/* FMovieSceneVectorKeyStruct interface
 *****************************************************************************/

#if WITH_EDITOR
struct FVectorSectionEditorData
{
	FVectorSectionEditorData(int32 NumChannels)
	{
		MetaData[0].SetIdentifiers("Vector.X", FCommonChannelData::ChannelX);
		MetaData[0].SortOrder = 0;
		MetaData[0].Color = FCommonChannelData::RedChannelColor;

		MetaData[1].SetIdentifiers("Vector.Y", FCommonChannelData::ChannelY);
		MetaData[1].SortOrder = 1;
		MetaData[1].Color = FCommonChannelData::GreenChannelColor;

		MetaData[2].SetIdentifiers("Vector.Z", FCommonChannelData::ChannelZ);
		MetaData[2].SortOrder = 2;
		MetaData[2].Color = FCommonChannelData::BlueChannelColor;

		MetaData[3].SetIdentifiers("Vector.W", FCommonChannelData::ChannelW);
		MetaData[3].SortOrder = 3;

		ExternalValues[0].OnGetExternalValue = [NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelX(InObject, Bindings, NumChannels); };
		ExternalValues[1].OnGetExternalValue = [NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelY(InObject, Bindings, NumChannels); };
		ExternalValues[2].OnGetExternalValue = [NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelZ(InObject, Bindings, NumChannels); };
		ExternalValues[3].OnGetExternalValue = [NumChannels](UObject& InObject, FTrackInstancePropertyBindings* Bindings) { return ExtractChannelW(InObject, Bindings, NumChannels); };
	}

	static FVector4 GetPropertyValue(UObject& InObject, FTrackInstancePropertyBindings& Bindings, int32 NumChannels)
	{
		if (NumChannels == 2)
		{
			FVector2D Vector = Bindings.GetCurrentValue<FVector2D>(InObject);
			return FVector4(Vector.X, Vector.Y, 0.f, 0.f);
		}
		else if (NumChannels == 3)
		{
			FVector Vector = Bindings.GetCurrentValue<FVector>(InObject);
			return FVector4(Vector.X, Vector.Y, Vector.Z, 0.f);
		}
		else
		{
			return Bindings.GetCurrentValue<FVector4>(InObject);
		}
	}

	static TOptional<float> ExtractChannelX(UObject& InObject, FTrackInstancePropertyBindings* Bindings, int32 NumChannels)
	{
		return Bindings ? GetPropertyValue(InObject, *Bindings, NumChannels).X : TOptional<float>();
	}
	static TOptional<float> ExtractChannelY(UObject& InObject, FTrackInstancePropertyBindings* Bindings, int32 NumChannels)
	{
		return Bindings ? GetPropertyValue(InObject, *Bindings, NumChannels).Y : TOptional<float>();
	}
	static TOptional<float> ExtractChannelZ(UObject& InObject, FTrackInstancePropertyBindings* Bindings, int32 NumChannels)
	{
		return Bindings ? GetPropertyValue(InObject, *Bindings, NumChannels).Z : TOptional<float>();
	}
	static TOptional<float> ExtractChannelW(UObject& InObject, FTrackInstancePropertyBindings* Bindings, int32 NumChannels)
	{
		return Bindings ? GetPropertyValue(InObject, *Bindings, NumChannels).W : TOptional<float>();
	}
	FMovieSceneChannelMetaData      MetaData[4];
	TMovieSceneExternalValue<float> ExternalValues[4];
};
#endif

void FMovieSceneVectorKeyStructBase::PropagateChanges(const FPropertyChangedEvent& ChangeEvent)
{
	KeyStructInterop.Apply(Time);
}


/* UMovieSceneVectorSection structors
 *****************************************************************************/

UMovieSceneVectorSection::UMovieSceneVectorSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ChannelsUsed = 0;
	bSupportsInfiniteRange = true;

	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToRestoreState ? 
			EMovieSceneCompletionMode::KeepState : 
			GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);
	BlendType = EMovieSceneBlendType::Absolute;
}

void UMovieSceneVectorSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		RecreateChannelProxy();
	}
}

void UMovieSceneVectorSection::RecreateChannelProxy()
{
	FMovieSceneChannelProxyData Channels;

	check(ChannelsUsed <= ARRAY_COUNT(Curves));

#if WITH_EDITOR

	const FVectorSectionEditorData EditorData(ChannelsUsed);
	for (int32 Index = 0; Index < ChannelsUsed; ++Index)
	{
		Channels.Add(Curves[Index], EditorData.MetaData[Index], EditorData.ExternalValues[Index]);
	}

#else

	for (int32 Index = 0; Index < ChannelsUsed; ++Index)
	{
		Channels.Add(Curves[Index]);
	}

#endif

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
}

TSharedPtr<FStructOnScope> UMovieSceneVectorSection::GetKeyStruct(TArrayView<const FKeyHandle> KeyHandles)
{
	TSharedPtr<FStructOnScope> KeyStruct;
	if (ChannelsUsed == 2)
	{
		KeyStruct = MakeShareable(new FStructOnScope(FMovieSceneVector2DKeyStruct::StaticStruct()));
	}
	else if (ChannelsUsed == 3)
	{
		KeyStruct = MakeShareable(new FStructOnScope(FMovieSceneVectorKeyStruct::StaticStruct()));
	}
	else if (ChannelsUsed == 4)
	{
		KeyStruct = MakeShareable(new FStructOnScope(FMovieSceneVector4KeyStruct::StaticStruct()));
	}

	if (KeyStruct.IsValid())
	{
		FMovieSceneVectorKeyStructBase* Struct = (FMovieSceneVectorKeyStructBase*)KeyStruct->GetStructMemory();
		for (int32 Index = 0; Index < ChannelsUsed; ++Index)
		{
			Struct->KeyStructInterop.Add(FMovieSceneChannelValueHelper(ChannelProxy->MakeHandle<FMovieSceneFloatChannel>(Index), Struct->GetPropertyChannelByIndex(Index), KeyHandles));
		}

		Struct->Time = Struct->KeyStructInterop.GetUnifiedKeyTime().Get(0);
	}

	return KeyStruct;
}
