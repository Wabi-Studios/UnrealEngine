// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneScriptingChannel.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Sections/MovieSceneActorReferenceSection.h"
#include "KeysAndChannels/MovieSceneScriptingChannel.h"
#include "KeyParams.h"
#include "MovieScene.h"
#include "SequencerBindingProxy.h"

#include "MovieSceneScriptingActorReference.generated.h"

/**
* Exposes a Sequencer Actor Reference type key to Python/Blueprints.
* Stores a reference to the data so changes to this class are forwarded onto the underlying data structures.
*/
UCLASS(BlueprintType)
class UMovieSceneScriptingActorReferenceKey : public UMovieSceneScriptingKey, public TMovieSceneScriptingKey<FMovieSceneActorReferenceData, FMovieSceneActorReferenceKey>
{
	GENERATED_BODY()
public:
	/**
	* Gets the time for this key from the owning channel.
	* @param TimeUnit	Should the time be returned in Display Rate frames (possibly with a sub-frame value) or in Tick Resolution with no sub-frame values?
	* @return			The time of this key which combines both the frame number and the sub-frame it is on. Sub-frame will be zero if you request Tick Resolution.	
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	virtual FFrameTime GetTime(ESequenceTimeUnit TimeUnit = ESequenceTimeUnit::DisplayRate) const override { return GetTimeFromChannel(KeyHandle, OwningSequence, TimeUnit); }
	
	/**
	* Sets the time for this key in the owning channel. Will replace any key that already exists at that frame number in this channel.
	* @param NewFrameNumber	What frame should this key be moved to? This should be in the time unit specified by TimeUnit.
	* @param SubFrame		If using Display Rate time, what is the sub-frame this should go to? Clamped [0-1), and ignored with when TimeUnit is set to Tick Resolution. 
	* @param TimeUnit		Should the NewFrameNumber be interpreted as Display Rate frames or in Tick Resolution?
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	void SetTime(const FFrameNumber& NewFrameNumber, float SubFrame = 0.f, ESequenceTimeUnit TimeUnit = ESequenceTimeUnit::DisplayRate) { SetTimeInChannel(KeyHandle, OwningSequence, NewFrameNumber, TimeUnit, SubFrame); }

	/**
	* Gets the value for this key from the owning channel.
	* @return	The object binding for this key.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	FMovieSceneObjectBindingID GetValue() const
	{
		FMovieSceneActorReferenceKey Value = GetValueFromChannel(KeyHandle);
		return Value.Object;
	}

	/**
	* Sets the value for this key, reflecting it in the owning channel.
	* @param InNewValue	The new object binding for this key.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	void SetValue(const FMovieSceneObjectBindingID& InNewValue)
	{
		FMovieSceneActorReferenceKey ReferenceKey = FMovieSceneActorReferenceKey(InNewValue);
		SetValueInChannel(KeyHandle, ReferenceKey);
	}
};

UCLASS(BlueprintType)
class UMovieSceneScriptingActorReferenceChannel : public UMovieSceneScriptingChannel, public TMovieSceneScriptingChannel<FMovieSceneActorReferenceData, UMovieSceneScriptingActorReferenceKey, FMovieSceneActorReferenceKey>
{
	GENERATED_BODY()
public:
	/**
	* Add a key to this channel. This initializes a new key and returns a reference to it.
	* @param	InTime			The frame this key should go on. Respects TimeUnit to determine if it is a display rate frame or a tick resolution frame.
	* @param	NewValue		The value that this key should be created with.
	* @param	SubFrame		Optional [0-1) clamped sub-frame to put this key on. Ignored if TimeUnit is set to Tick Resolution.
	* @param	TimeUnit 		Is the specified InTime in Display Rate frames or Tick Resolution.
	* @return	The key that was created with the specified values at the specified time.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	UMovieSceneScriptingActorReferenceKey* AddKey(const FFrameNumber InTime, FMovieSceneObjectBindingID NewValue, float SubFrame = 0.f, ESequenceTimeUnit TimeUnit = ESequenceTimeUnit::DisplayRate)
	{
		FMovieSceneActorReferenceKey ReferenceKey = FMovieSceneActorReferenceKey(NewValue);
		return AddKeyInChannel(ChannelHandle, OwningSequence, InTime, ReferenceKey, SubFrame, TimeUnit, EMovieSceneKeyInterpolation::Auto);
	}

	/**
	* Removes the specified key. Does nothing if the key is not specified or the key belongs to another channel.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	virtual void RemoveKey(UMovieSceneScriptingKey* Key)
	{
		RemoveKeyFromChannel(ChannelHandle, Key);
	}

	/**
	* Gets all of the keys in this channel.
	* @return	An array of UMovieSceneScriptingActorReferenceKeys contained by this channel.
	*			Returns all keys even if clipped by the owning section's boundaries or outside of the current sequence play range.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	virtual TArray<UMovieSceneScriptingKey*> GetKeys() const override
	{
		return GetKeysInChannel(ChannelHandle, OwningSequence);
	}

	/**
	* Set this channel's default value that should be used when no keys are present.
	* Sets bHasDefaultValue to true automatically.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	void SetDefault(FMovieSceneObjectBindingID InDefaultValue)
	{
		FMovieSceneActorReferenceKey ReferenceKey = FMovieSceneActorReferenceKey(InDefaultValue);
		SetDefaultInChannel(ChannelHandle, ReferenceKey);
	}

	/**
	* Get this channel's default value that will be used when no keys are present. Only a valid
	* value when HasDefault() returns true.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	FMovieSceneObjectBindingID GetDefault() const
	{
		// FMovieSceneActorReferenceData doesn't implement GetDefault via TOptional, so we're wrapping this function by hand as well.
		FMovieSceneActorReferenceData* Channel = ChannelHandle.Get();
		if (Channel)
		{
			FMovieSceneActorReferenceKey ReferenceKey = Channel->GetDefault();
			return ReferenceKey.Object;
		}

		UE_LOG(LogMovieScene, Error, TEXT("Invalid ChannelHandle for MovieSceneScriptingChannel, failed to get default value."));
		return FMovieSceneObjectBindingID();
	}

	/**
	* Remove this channel's default value causing the channel to have no effect where no keys are present
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	void RemoveDefault()
	{
		// FMovieSceneActorReferenceData doesn't implement RemoveDefault, instead it implements ClearDefault(). Wrapping this function by hand,
		// and not falling back to the template as a result, but keeping the same function name so it is consistent with the other scripting channels.
		FMovieSceneActorReferenceData* Channel = ChannelHandle.Get();
		if (Channel)
		{
			Channel->ClearDefault();
			return;
		}
		UE_LOG(LogMovieScene, Error, TEXT("Invalid ChannelHandle for MovieSceneScriptingChannel, failed to remove default value."));
	}

	/**
	* @return Does this channel have a default value set?
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	bool HasDefault() const
	{
		return GetDefault() == FMovieSceneObjectBindingID();
	}
public:
	TWeakObjectPtr<UMovieSceneSequence> OwningSequence;
	TMovieSceneChannelHandle<FMovieSceneActorReferenceData> ChannelHandle;
};