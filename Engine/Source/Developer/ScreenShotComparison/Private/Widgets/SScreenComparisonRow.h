// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISourceControlState.h"

class FScreenComparisonModel;

/**
 * Widget to display a particular view.
 */
class SScreenComparisonRow : public SMultiColumnTableRow< TSharedPtr<FScreenComparisonModel> >
{
public:

	SLATE_BEGIN_ARGS( SScreenComparisonRow )
		{}

		SLATE_ARGUMENT( IScreenShotManagerPtr, ScreenshotManager )
		SLATE_ARGUMENT( FString, ComparisonDirectory )
		SLATE_ARGUMENT( TSharedPtr<FComparisonResults>, Comparisons )
		SLATE_ARGUMENT( TSharedPtr<FScreenComparisonModel>, ComparisonResult )

	SLATE_END_ARGS()
	
	/**
	 * Construct the widget.
	 *
	 * @param InArgs   A declaration from which to construct the widget.
 	 * @param InOwnerTableView   The owning table data.
	 */
	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView );
	
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	bool CanUseSourceControl() const;

	TSharedRef<SWidget> BuildMissingView();
	TSharedRef<SWidget> BuildAddedView();
	TSharedRef<SWidget> BuildComparisonPreview();

	FReply AddNew();
	FReply RemoveOld();
	FReply ReplaceOld();

	void GetStatus();

	TSharedPtr<FSlateDynamicImageBrush> LoadScreenshot(FString ImagePath);

private:

	//Holds the screen shot info.
	TSharedPtr<FScreenComparisonModel> Model;

	// The manager containing the screen shots
	IScreenShotManagerPtr ScreenshotManager;

	FString ComparisonDirectory;

	TSharedPtr<FComparisonResults> Comparisons;

	//The cached actual size of the screenshot
	FIntPoint CachedActualImageSize;

	//Holds the dynamic brush.
	TSharedPtr<FSlateDynamicImageBrush> ApprovedBrush;

	//Holds the dynamic brush.
	TSharedPtr<FSlateDynamicImageBrush> UnapprovedBrush;

	//Holds the dynamic brush.
	TSharedPtr<FSlateDynamicImageBrush> ComparisonBrush;

	// 
	TArray<FString> ExternalFiles;

	// 
	TArray<FSourceControlStateRef> SourceControlStates;
};
