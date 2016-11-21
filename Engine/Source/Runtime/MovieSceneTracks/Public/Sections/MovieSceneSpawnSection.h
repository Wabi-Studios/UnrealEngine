// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneBoolSection.h"
#include "MovieSceneSpawnSection.generated.h"

/**
 * A spawn section.
 */
UCLASS(MinimalAPI)
class UMovieSceneSpawnSection 
	: public UMovieSceneBoolSection
{
	GENERATED_BODY()

	UMovieSceneSpawnSection(const FObjectInitializer& Init);
};
