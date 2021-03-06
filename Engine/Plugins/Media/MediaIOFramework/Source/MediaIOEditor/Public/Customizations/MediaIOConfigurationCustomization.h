// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Customizations/MediaIOCustomizationBase.h"
#include "Input/Reply.h"
#include "MediaIOCoreDefinitions.h"

/**
 * Implements a details view customization for the FMediaIOConfiguration
 */
class MEDIAIOEDITOR_API FMediaIOConfigurationCustomization : public FMediaIOCustomizationBase
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

private:
	virtual TAttribute<FText> GetContentText() override;
	virtual TSharedRef<SWidget> HandleSourceComboButtonMenuContent() override;

	ECheckBoxState GetEnforceCheckboxState() const;
	void SetEnforceCheckboxState(ECheckBoxState CheckboxState);
	bool ShowAdvancedColumns(FName ColumnName, const TArray<FMediaIOConfiguration>& UniquePermutationsForThisColumn) const;

	void OnSelectionChanged(FMediaIOConfiguration SelectedItem);
	FReply OnButtonClicked() const;

	TWeakPtr<SWidget> PermutationSelector;
	FMediaIOConfiguration SelectedConfiguration;
	TArray<TWeakObjectPtr<class UTimeSynchronizableMediaSource>> CustomizedSources;
};
