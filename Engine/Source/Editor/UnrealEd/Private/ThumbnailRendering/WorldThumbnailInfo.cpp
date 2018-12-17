// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ThumbnailRendering/WorldThumbnailInfo.h"

UWorldThumbnailInfo::UWorldThumbnailInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CameraMode = ECameraProjectionMode::Perspective;
	OrthoDirection = EOrthoThumbnailDirection::Top;
}
