// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEvalTemplate.h"
#include "MovieSceneSkeletalAnimationSection.h"
#include "MovieSceneSkeletalAnimationTemplate.generated.h"

class UMovieSceneSkeletalAnimationSection;

USTRUCT()
struct FMovieSceneSkeletalAnimationSectionTemplateParameters : public FMovieSceneSkeletalAnimationParams
{
	GENERATED_BODY()

	FMovieSceneSkeletalAnimationSectionTemplateParameters() {}
	FMovieSceneSkeletalAnimationSectionTemplateParameters(const FMovieSceneSkeletalAnimationParams& BaseParams, float InSectionStartTime)
		: FMovieSceneSkeletalAnimationParams(BaseParams)
		, SectionStartTime(InSectionStartTime)
	{}

	float MapTimeToAnimation(float InPosition) const;

	UPROPERTY()
	float SectionStartTime;
};

USTRUCT()
struct FMovieSceneSkeletalAnimationSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
	
	FMovieSceneSkeletalAnimationSectionTemplate() {}
	FMovieSceneSkeletalAnimationSectionTemplate(const UMovieSceneSkeletalAnimationSection& Section);

	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	UPROPERTY()
	FMovieSceneSkeletalAnimationSectionTemplateParameters Params;
};