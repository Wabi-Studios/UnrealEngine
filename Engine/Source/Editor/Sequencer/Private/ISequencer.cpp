// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ISequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"

FFrameRate ISequencer::GetRootTickResolution() const
{
	UMovieSceneSequence* RootSequence = GetRootMovieSceneSequence();
	if (RootSequence)
	{
		return RootSequence->GetMovieScene()->GetTickResolution();
	}

	ensureMsgf(false, TEXT("No valid sequence found."));
	return FFrameRate();
}

FFrameRate ISequencer::GetRootDisplayRate() const
{
	UMovieSceneSequence* RootSequence = GetRootMovieSceneSequence();
	if (RootSequence)
	{
		return RootSequence->GetMovieScene()->GetDisplayRate();
	}

	ensureMsgf(false, TEXT("No valid sequence found."));
	return FFrameRate();
}

FFrameRate ISequencer::GetFocusedTickResolution() const
{
	UMovieSceneSequence* FocusedSequence = GetFocusedMovieSceneSequence();
	if (FocusedSequence)
	{
		return FocusedSequence->GetMovieScene()->GetTickResolution();
	}

	ensureMsgf(false, TEXT("No valid sequence found."));
	return FFrameRate();
}

FFrameRate ISequencer::GetFocusedDisplayRate() const
{
	UMovieSceneSequence* FocusedSequence = GetFocusedMovieSceneSequence();
	if (FocusedSequence)
	{
		return FocusedSequence->GetMovieScene()->GetDisplayRate();
	}

	ensureMsgf(false, TEXT("No valid sequence found."));
	return FFrameRate();
}