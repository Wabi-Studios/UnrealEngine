// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Editor/PropertyEditor/Public/PropertyEditing.h"
#include "SGameplayTagWidget.h"

//////////////////////////////////////////////////////////////////////////
// FGameplayTagsSettingsCustomization

class FGameplayTagsSettingsCustomization : public IDetailCustomization
{
public:
	FGameplayTagsSettingsCustomization();
	virtual ~FGameplayTagsSettingsCustomization();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:

	/** Callback for when a tag changes */
	void OnTagChanged();

	/** Module callback for when the tag tree changes */
	void OnTagTreeChanged();

	TSharedPtr<SGameplayTagWidget> TagWidget;
};