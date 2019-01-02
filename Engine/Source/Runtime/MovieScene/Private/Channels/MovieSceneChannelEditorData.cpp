// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneChannelEditorData.h"

#if WITH_EDITOR

const FText FCommonChannelData::ChannelX = NSLOCTEXT("MovieSceneChannels", "ChannelX", "X");
const FText FCommonChannelData::ChannelY = NSLOCTEXT("MovieSceneChannels", "ChannelY", "Y");
const FText FCommonChannelData::ChannelZ = NSLOCTEXT("MovieSceneChannels", "ChannelZ", "Z");
const FText FCommonChannelData::ChannelW = NSLOCTEXT("MovieSceneChannels", "ChannelW", "W");

const FText FCommonChannelData::ChannelR = NSLOCTEXT("MovieSceneChannels", "ChannelR", "R");
const FText FCommonChannelData::ChannelG = NSLOCTEXT("MovieSceneChannels", "ChannelG", "G");
const FText FCommonChannelData::ChannelB = NSLOCTEXT("MovieSceneChannels", "ChannelB", "B");
const FText FCommonChannelData::ChannelA = NSLOCTEXT("MovieSceneChannels", "ChannelA", "A");

const FLinearColor FCommonChannelData::RedChannelColor(1.0f, 0.0f, 0.0f, 0.8f);
const FLinearColor FCommonChannelData::GreenChannelColor(0.0f, 1.0f, 0.0f, 0.8f);
const FLinearColor FCommonChannelData::BlueChannelColor(0.05f, 0.05f, 1.0f, 0.8f);


FMovieSceneChannelMetaData::FMovieSceneChannelMetaData()
	: bEnabled(true)
	, bCanCollapseToTrack(true)
	, SortOrder(0)
	, Name(NAME_None)
{}

FMovieSceneChannelMetaData::FMovieSceneChannelMetaData(FName InName, FText InDisplayText, FText InGroup)
	: bEnabled(true)
	, bCanCollapseToTrack(true)
	, SortOrder(0)
	, Name(InName)
	, DisplayText(InDisplayText)
	, Group(InGroup)
{}

void FMovieSceneChannelMetaData::SetIdentifiers(FName InName, FText InDisplayText, FText InGroup)
{
	Group = InGroup;
	Name = InName;
	DisplayText = InDisplayText;
}

#endif	// WITH_EDITOR