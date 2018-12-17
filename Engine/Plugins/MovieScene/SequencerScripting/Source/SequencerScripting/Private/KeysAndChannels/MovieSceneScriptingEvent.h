// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneScriptingChannel.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Sections/MovieSceneEventSection.h"
#include "KeysAndChannels/MovieSceneScriptingChannel.h"
#include "KeyParams.h"
#include "MovieScene.h"

#include "MovieSceneScriptingEvent.generated.h"

/**
* Exposes a Sequencer Event type key to Python/Blueprints.
* Stores a reference to the data so changes to this class are forwarded onto the underlying data structures.
*/
UCLASS(BlueprintType)
class UMovieSceneScriptingEventKey : public UMovieSceneScriptingKey, public TMovieSceneScriptingKey<FMovieSceneEventSectionData, FEventPayload>
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
	* @return	The event payload for this key which contains an event name and data.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	FEventPayload GetValue() const
	{
		return GetValueFromChannel(KeyHandle);
	}

	/**
	* Sets the value for this key, reflecting it in the owning channel.
	* @param InNewValue	The new event payload for this key.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	void SetValue(const FEventPayload& InNewValue)
	{
		SetValueInChannel(KeyHandle, InNewValue);
	}
};

UCLASS(BlueprintType)
class UMovieSceneScriptingEventChannel : public UMovieSceneScriptingChannel, public TMovieSceneScriptingChannel<FMovieSceneEventSectionData, UMovieSceneScriptingEventKey, FEventPayload>
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
	UMovieSceneScriptingEventKey* AddKey(const FFrameNumber& InTime, FEventPayload NewValue, float SubFrame = 0.f, ESequenceTimeUnit TimeUnit = ESequenceTimeUnit::DisplayRate)
	{
		return AddKeyInChannel(ChannelHandle, OwningSequence, InTime, NewValue, SubFrame, TimeUnit, EMovieSceneKeyInterpolation::Auto);
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
	* @return	An array of UMovieSceneScriptingEventKey's contained by this channel.
	*			Returns all keys even if clipped by the owning section's boundaries or outside of the current sequence play range.
	*/
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Sequencer Tools | Keys")
	virtual TArray<UMovieSceneScriptingKey*> GetKeys() const override
	{
		return GetKeysInChannel(ChannelHandle, OwningSequence);
	}

public:
	TWeakObjectPtr<UMovieSceneSequence> OwningSequence;
	TMovieSceneChannelHandle<FMovieSceneEventSectionData> ChannelHandle;
};