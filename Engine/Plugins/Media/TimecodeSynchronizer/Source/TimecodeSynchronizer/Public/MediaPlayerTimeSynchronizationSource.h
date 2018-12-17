// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TimeSynchronizationSource.h"

#include "MediaPlayerTimeSynchronizationSource.generated.h"


class UTimeSynchronizableMediaSource;
class UMediaTexture;

/**
 * Synchronization Source using the Media Player framework
 */
UCLASS(EditInlineNew)
class TIMECODESYNCHRONIZER_API UMediaPlayerTimeSynchronizationSource : public UTimeSynchronizationSource
{
	GENERATED_BODY()

public:
	/* Media source asset of this input*/
	UPROPERTY(EditAnywhere, Category=Player)
	UTimeSynchronizableMediaSource* MediaSource;

	/* Texture linked to the media player*/
	UPROPERTY(EditAnywhere, Category = Player)
	UMediaTexture* MediaTexture;

public:

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	//~ Begin UTimeSynchronizationSource Interface
	virtual FFrameTime GetNewestSampleTime() const override;
	virtual FFrameTime GetOldestSampleTime() const override;
	virtual FFrameRate GetFrameRate() const override;
	virtual bool IsReady() const override;
	virtual bool Open(const FTimeSynchronizationOpenData& InOpenData) override;
	virtual void Start(const FTimeSynchronizationStartData& InStartData) override;
	virtual void Close() override;
	virtual FString GetDisplayName() const override;
	//~ End UTimeSynchronizationSource Interface

private:

	TOptional<FTimeSynchronizationOpenData> OpenData;
	TOptional<FTimeSynchronizationStartData> StartData;
};
