// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditableTextBox.generated.h"

/** Editable text box widget */
UCLASS(ClassGroup=UserInterface)
class UMG_API UEditableTextBox : public UWidget
{
	GENERATED_UCLASS_BODY()

public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditableTextBoxChangedEvent, const FText&, Text);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEditableTextBoxCommittedEvent, const FText&, Text, ETextCommit::Type, CommitMethod);

public:

	/** The style */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style", meta=( DisplayName="Style" ))
	FEditableTextBoxStyle WidgetStyle;

	/** Style used for the text box */
	UPROPERTY()
	USlateWidgetStyleAsset* Style_DEPRECATED;

	/** The text content for this editable text box widget */
	UPROPERTY(EditDefaultsOnly, Category=Content)
	FText Text;

	/** A bindable delegate to allow logic to drive the text of the widget */
	UPROPERTY()
	FGetText TextDelegate;

	/** Hint text that appears when there is no text in the text box */
	UPROPERTY(EditDefaultsOnly, Category=Content)
	FText HintText;

	/** A bindable delegate to allow logic to drive the hint text of the widget */
	UPROPERTY()
	FGetText HintTextDelegate;

	/** Font color and opacity (overrides Style) */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FSlateFontInfo Font;

	/** Text color and opacity (overrides Style) */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FLinearColor ForegroundColor;

	/** The color of the background/border around the editable text (overrides Style) */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FLinearColor BackgroundColor;

	/** Text color and opacity when read-only (overrides Style) */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FLinearColor ReadOnlyForegroundColor;

	/** Sets whether this text box can actually be modified interactively by the user */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	bool IsReadOnly;

	/** Sets whether this text box is for storing a password */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	bool IsPassword;

	/** Minimum width that a text block should be */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	float MinimumDesiredWidth;

	/** Padding between the box/border and the text widget inside (overrides Style) */
	UPROPERTY(EditDefaultsOnly, Category=Appearance)
	FMargin Padding;

	/** Workaround as we loose focus when the auto completion closes. */
	UPROPERTY(EditDefaultsOnly, Category=Behavior, AdvancedDisplay)
	bool IsCaretMovedWhenGainFocus;

	/** Whether to select all text when the user clicks to give focus on the widget */
	UPROPERTY(EditDefaultsOnly, Category=Behavior, AdvancedDisplay)
	bool SelectAllTextWhenFocused;

	/** Whether to allow the user to back out of changes when they press the escape key */
	UPROPERTY(EditDefaultsOnly, Category=Behavior, AdvancedDisplay)
	bool RevertTextOnEscape;

	/** Whether to clear keyboard focus when pressing enter to commit changes */
	UPROPERTY(EditDefaultsOnly, Category=Behavior, AdvancedDisplay)
	bool ClearKeyboardFocusOnCommit;

	/** Whether to select all text when pressing enter to commit changes */
	UPROPERTY(EditDefaultsOnly, Category=Behavior, AdvancedDisplay)
	bool SelectAllTextOnCommit;

	/** Called whenever the text is changed interactively by the user */
	UPROPERTY(BlueprintAssignable, Category="Widget Event")
	FOnEditableTextBoxChangedEvent OnTextChanged;

	/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
	UPROPERTY(BlueprintAssignable, Category="Widget Event")
	FOnEditableTextBoxCommittedEvent OnTextCommitted;

	/** Provide a alternative mechanism for error reporting. */
	//SLATE_ARGUMENT(TSharedPtr<class IErrorReportingWidget>, ErrorReporting)

public:

	/**  */
	UFUNCTION(BlueprintCallable, Category="Widget")
	FText GetText() const;

	/**  */
	UFUNCTION(BlueprintCallable, Category="Widget")
	void SetText(FText InText);

	UFUNCTION(BlueprintCallable, Category="Widget")
	void SetError(FText InError);

	UFUNCTION(BlueprintCallable, Category="Widget")
	void ClearError();

	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

	// UVisual interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual interface

	// Begin UObject interface
	virtual void PostLoad() override;
	// End of UObject interface

#if WITH_EDITOR
	virtual const FSlateBrush* GetEditorIcon() override;
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget

	void HandleOnTextChanged(const FText& Text);
	void HandleOnTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

protected:
	TSharedPtr<SEditableTextBox> MyEditableTextBlock;
};
