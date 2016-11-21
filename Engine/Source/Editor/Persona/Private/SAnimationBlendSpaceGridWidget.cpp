// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "PersonaPrivatePCH.h"
#include "SAnimationBlendSpaceGridWidget.h"

#include "Animation/BlendSpaceBase.h"
#include "Animation/BlendSpace1D.h"

#include "STooltip.h"
#include "SNumericEntryBox.h"

#include "PropertyEditing.h"
#include "IDetailsView.h"
#include "ISinglePropertyView.h"
#include "IStructureDetailsView.h"

#include "Customization/BlendSampleDetails.h"
#include "AssetDragDropOp.h"

#define LOCTEXT_NAMESPACE "SAnimationBlendSpaceGridWidget"

void SBlendSpaceGridWidget::Construct(const FArguments& InArgs)
{
	BlendSpace = InArgs._BlendSpaceBase;
	NotifyHook = InArgs._NotifyHook;
	OnSampleAdded = InArgs._OnSampleAdded;
	OnSampleMoved = InArgs._OnSampleMoved;
	OnSampleRemoved = InArgs._OnSampleRemoved;

	GridType = BlendSpace->IsA<UBlendSpace1D>() ? EGridType::SingleAxis : EGridType::TwoAxis;
	BlendParametersToDraw = (GridType == EGridType::SingleAxis) ? 1 : 2;
	
	HighlightedSampleIndex = SelectedSampleIndex = DraggedSampleIndex = INDEX_NONE;
	DragState = EDragState::None;
	// Initialize flags 
	bPreviewPositionSet = false;
	bShowTriangulation = false;
	bMouseIsOverGeometry = false;
	bRefreshCachedData = true;
	bStretchToFit = true;

	InvalidSamplePositionDragDropText = FText::FromString(TEXT("Invalid Sample Position"));

	// Retrieve UI color values
	KeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Regular");
	HighlightKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Highlight");
	SelectKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Pressed");
	PreDragKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Pressed");
	DragKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Drag");
	InvalidColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Invalid");
	DropKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Drop");
	PreviewKeyColor = FEditorStyle::GetSlateColor("BlendSpaceKey.Preview");
	GridLinesColor = GetDefault<UEditorStyleSettings>()->RegularColor;
	GridOutlineColor = GetDefault<UEditorStyleSettings>()->RuleColor;

	// Retrieve background and sample key brushes 
	BackgroundImage = FEditorStyle::GetBrush(TEXT("Graph.Panel.SolidBackground"));
	KeyBrush = FEditorStyle::GetBrush("CurveEd.CurveKey");

	// Retrieve font data 
	FontInfo = FEditorStyle::GetFontStyle("CurveEd.InfoFont");

	// Initialize UI layout values
	KeySize = FVector2D(12.0f, 12.0f);
	DragThresshold = 9.0f;
	ClickThresshold = 12.0f;
	TextMargin = 16.0f;
	GridMargin = FMargin(MaxVerticalAxisTextWidth + (TextMargin * 2.0f), TextMargin, (HorizontalAxisMaxTextWidth * 0.5f) + TextMargin, MaxHorizontalAxisTextHeight + (TextMargin * 2.0f));

	const bool bShowInputBoxLabel = true;
	// Widget construction
	this->ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()				
				[
					SNew(SBorder)
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Left)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.DesiredSizeScale(FVector2D(1.0f, 1.0f))
					.Padding_Lambda([&]() { return FMargin(GridMargin.Left + 6, GridMargin.Top + 6, 0, 0) + GridRatioMargin; })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NoBorder"))
							.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SBlendSpaceGridWidget::GetTriangulationButtonVisibility)))		
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ToolTipText(LOCTEXT("ShowTriangulation", "Show Triangulation"))
								.OnClicked(this, &SBlendSpaceGridWidget::ToggleTriangulationVisibility)
								.ContentPadding(1)
								[
									SNew(SImage)
									.Image(FEditorStyle::GetBrush("BlendSpaceEditor.ToggleTriangulation"))
									.ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NoBorder"))
							.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SBlendSpaceGridWidget::GetFittingButtonVisibility)))
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ToolTipText(this, &SBlendSpaceGridWidget::GetFittingTypeButtonToolTipText)
								.OnClicked(this, &SBlendSpaceGridWidget::ToggleFittingType)
								.ContentPadding(1)
								[
									SNew(SImage)
									.Image(FEditorStyle::GetBrush("WidgetDesigner.ZoomToFit"))
									.ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
						]


						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NoBorder"))
							.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SBlendSpaceGridWidget::GetInputBoxVisibility, 0)))
							.VAlign(VAlign_Center)
							[
								CreateGridEntryBox(0, bShowInputBoxLabel).ToSharedRef()
							]
						]
	
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("NoBorder"))
							.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SBlendSpaceGridWidget::GetInputBoxVisibility, 1)))
							.VAlign(VAlign_Center)
							[
								CreateGridEntryBox(1, bShowInputBoxLabel).ToSharedRef()
							]
						]
					]
				]
			]
		]
	];
		
	SAssignNew(ToolTip, SToolTip)
	.BorderImage(FCoreStyle::Get().GetBrush("ToolTip.BrightBackground"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		[
			SNew(STextBlock)
			.Text(this, &SBlendSpaceGridWidget::GetToolTipAnimationName)
			.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
			.ColorAndOpacity(FLinearColor::Black)
		]

		+ SVerticalBox::Slot()
		[
			SNew(STextBlock)
			.Text(this, &SBlendSpaceGridWidget::GetToolTipSampleValue)
			.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
			.ColorAndOpacity(FLinearColor::Black)
		]
	];
}

TSharedPtr<SWidget> SBlendSpaceGridWidget::CreateGridEntryBox(const int32 BoxIndex, const bool bShowLabel)
{
	return SNew(SNumericEntryBox<float>)
		.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
		.Value(this, &SBlendSpaceGridWidget::GetInputBoxValue, BoxIndex)
		.UndeterminedString(LOCTEXT("MultipleValues", "Multiple Values"))
		.OnValueCommitted(this, &SBlendSpaceGridWidget::OnInputBoxValueCommited, BoxIndex)
		.OnValueChanged(this, &SBlendSpaceGridWidget::OnInputBoxValueChanged, BoxIndex)
		.LabelVAlign(VAlign_Center)
		.AllowSpin(true)
		.MinValue(this, &SBlendSpaceGridWidget::GetInputBoxMinValue, BoxIndex)
		.MaxValue(this, &SBlendSpaceGridWidget::GetInputBoxMaxValue, BoxIndex)
		.MinSliderValue(this, &SBlendSpaceGridWidget::GetInputBoxMinValue, BoxIndex)
		.MaxSliderValue(this, &SBlendSpaceGridWidget::GetInputBoxMaxValue, BoxIndex)
		.MinDesiredValueWidth(60.0f)
		.Label()
		[
			SNew(STextBlock)
			.Visibility(bShowLabel ? EVisibility::Visible : EVisibility::Collapsed)
			.Text_Lambda([&]() { return (BoxIndex == 0) ? ParameterXName : ParameterYName; })
		];
}

int32 SBlendSpaceGridWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCompoundWidget::OnPaint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
	
	PaintBackgroundAndGrid(AllottedGeometry, MyClippingRect, OutDrawElements, LayerId);
	if (bShowTriangulation)
	{
		PaintTriangulation(AllottedGeometry, MyClippingRect, OutDrawElements, LayerId);
	}	
	PaintSampleKeys(AllottedGeometry, MyClippingRect, OutDrawElements, LayerId);
	PaintAxisText(AllottedGeometry, MyClippingRect, OutDrawElements, LayerId);

	return LayerId;
}

void SBlendSpaceGridWidget::PaintBackgroundAndGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32& DrawLayerId) const
{
	// Fill the background
	FSlateDrawElement::MakeBox( OutDrawElements, DrawLayerId, AllottedGeometry.ToPaintGeometry(), BackgroundImage, MyClippingRect );

	// Create the grid
	const FVector2D GridSize = CachedGridRectangle.GetSize();
	const FVector2D GridOffset = CachedGridRectangle.GetTopLeft();
	TArray<FVector2D> LinePoints;

	// Draw outer grid lines separately (this will avoid missing lines with 1D blend spaces)
	LinePoints.SetNumZeroed(5);

	// Top line
	LinePoints[0] = GridOffset;

	LinePoints[1] = GridOffset;
	LinePoints[1].X += GridSize.X;

	LinePoints[2] = GridOffset;
	LinePoints[2].X += GridSize.X;
	LinePoints[2].Y += GridSize.Y;

	LinePoints[3] = GridOffset;
	LinePoints[3].Y += GridSize.Y;

	LinePoints[4] = GridOffset;

	FSlateDrawElement::MakeLines( OutDrawElements, DrawLayerId + 1, AllottedGeometry.ToPaintGeometry(),	LinePoints, MyClippingRect, ESlateDrawEffect::None,	GridOutlineColor, true);
	
	// Draw grid lines
	LinePoints.SetNumZeroed(2);
	const FVector2D StartVectors[2] = { FVector2D(1.0f, 0.0f), FVector2D(0.0f, 1.0f) };
	const FVector2D OffsetVectors[2] = { FVector2D(0.0f, GridSize.Y), FVector2D(GridSize.X, 0.0f) };
	for (uint32 ParameterIndex = 0; ParameterIndex < BlendParametersToDraw; ++ParameterIndex)
	{
		const FBlendParameter& BlendParameter = BlendSpace->GetBlendParameter(ParameterIndex);
		const float Steps = GridSize[ParameterIndex] / ( BlendParameter.GridNum);

		for (int32 Index = 1; Index < BlendParameter.GridNum; ++Index)
		{			
			// Calculate line points
			LinePoints[0] = ((Index * Steps) * StartVectors[ParameterIndex]) + GridOffset;
			LinePoints[1] = LinePoints[0] + OffsetVectors[ParameterIndex];

			FSlateDrawElement::MakeLines( OutDrawElements, DrawLayerId, AllottedGeometry.ToPaintGeometry(), LinePoints,	MyClippingRect,	ESlateDrawEffect::None,	GridLinesColor,	true);
		}
	}	
	
	DrawLayerId += 2;
}

void SBlendSpaceGridWidget::PaintSampleKeys(const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32& DrawLayerId) const
{
	// Draw keys
	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
	{
		const FBlendSample& Sample = Samples[SampleIndex];
		
		FLinearColor DrawColor = KeyColor.GetSpecifiedColor();
		if (DraggedSampleIndex == SampleIndex)
		{
			DrawColor = (DragState == EDragState::PreDrag) ? PreDragKeyColor.GetSpecifiedColor() : DragKeyColor.GetSpecifiedColor();
		}
		else if (SelectedSampleIndex == SampleIndex)
		{
			DrawColor = SelectKeyColor.GetSpecifiedColor();
		}
		else if (HighlightedSampleIndex == SampleIndex)
		{
			DrawColor = HighlightKeyColor.GetSpecifiedColor();
		}

		DrawColor = Sample.bIsValid ? DrawColor : InvalidColor.GetSpecifiedColor();

		const FVector2D GridPosition = SampleValueToGridPosition(Sample.SampleValue) - (KeySize * 0.5f);
		FSlateDrawElement::MakeBox( OutDrawElements, DrawLayerId + 1, AllottedGeometry.ToPaintGeometry(GridPosition, KeySize), KeyBrush, MyClippingRect, ESlateDrawEffect::None, DrawColor );
	}

	if (bPreviewPositionSet)
	{
		const FVector2D MouseGridPosition = SampleValueToGridPosition(LastPreviewingSampleValue) - (KeySize * .5f);
		FSlateDrawElement::MakeBox( OutDrawElements, DrawLayerId + 1, AllottedGeometry.ToPaintGeometry(MouseGridPosition, KeySize), KeyBrush, MyClippingRect, ESlateDrawEffect::None, PreviewKeyColor.GetSpecifiedColor() );
	}

	if (DragState == EDragState::DragDrop || DragState == EDragState::InvalidDragDrop)
	{
		const FVector2D GridPoint = SnapToClosestGridPoint(LocalMousePosition) - (KeySize * .5f);
		FSlateDrawElement::MakeBox( OutDrawElements, DrawLayerId + 1, AllottedGeometry.ToPaintGeometry(GridPoint, KeySize), KeyBrush, MyClippingRect, ESlateDrawEffect::None,
			(DragState == EDragState::DragDrop) ? DropKeyColor.GetSpecifiedColor() : InvalidColor.GetSpecifiedColor() );
	}
}

void SBlendSpaceGridWidget::PaintAxisText(const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32& DrawLayerId) const
{
	const TSharedRef< FSlateFontMeasure > FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D GridCenter = CachedGridRectangle.GetCenter();

	// X axis
	FString Text = ParameterXName.ToString();
	FVector2D TextSize = FontMeasure->Measure(Text, FontInfo);

	// Label
	FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(GridCenter.X - (TextSize.X * .5f), CachedGridRectangle.Bottom + TextMargin + (KeySize.Y * .25f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text, FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);

	Text = FString::SanitizeFloat(SampleValueMin.X);
	TextSize = FontMeasure->Measure(Text, FontInfo);

	// Minimum value
	FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(CachedGridRectangle.Left - (TextSize.X * .5f), CachedGridRectangle.Bottom + TextMargin + (KeySize.Y * .25f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text, FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);

	Text = FString::SanitizeFloat(SampleValueMax.X);
	TextSize = FontMeasure->Measure(Text, FontInfo);

	// Maximum value
	FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(CachedGridRectangle.Right - (TextSize.X * .5f), CachedGridRectangle.Bottom + TextMargin + (KeySize.Y * .25f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text, FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);

	// Only draw Y axis labels if this is a 2D grid
	if (GridType == EGridType::TwoAxis)
	{
		// Y axis
		Text = ParameterYName.ToString();
		TextSize = FontMeasure->Measure(Text, FontInfo);

		// Label
		FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(((GridMargin.Left - TextSize.X) * 0.5f - (KeySize.X * .25f)) + GridRatioMargin.Left, GridCenter.Y - (TextSize.Y * .5f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text,	FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);

		Text = FString::SanitizeFloat(SampleValueMin.Y);
		TextSize = FontMeasure->Measure(Text, FontInfo);

		// Minimum value
		FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(((GridMargin.Left - TextSize.X) * 0.5f - (KeySize.X * .25f)) + GridRatioMargin.Left, CachedGridRectangle.Bottom - (TextSize.Y * .5f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text, FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);

		Text = FString::SanitizeFloat(SampleValueMax.Y);
		TextSize = FontMeasure->Measure(Text, FontInfo);

		// Maximum value
		FSlateDrawElement::MakeText(OutDrawElements, DrawLayerId + 1, AllottedGeometry.MakeChild(FVector2D(((GridMargin.Left - TextSize.X) * 0.5f - (KeySize.X * .25f) ) + GridRatioMargin.Left, ( GridMargin.Top + GridRatioMargin.Top) - (TextSize.Y * .5f)), FVector2D(1.0f, 1.0f)).ToPaintGeometry(), Text, FontInfo, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White);
	}
}

void SBlendSpaceGridWidget::PaintTriangulation(const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32& DrawLayerId) const
{
	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	const TArray<FEditorElement>& EditorElements = BlendSpace->GetGridSamples();
		
	for (const FEditorElement& Element : EditorElements)
	{
		for (int32 SourceIndex = 0; SourceIndex < 3; ++SourceIndex)
		{
			if (Element.Indices[SourceIndex] != INDEX_NONE)
			{
				const FBlendSample& SourceSample = Samples[Element.Indices[SourceIndex]];
				for (int32 TargetIndex = 0; TargetIndex < 3; ++TargetIndex)
				{
					if (Element.Indices[TargetIndex] != INDEX_NONE)
					{
						if (TargetIndex != SourceIndex)
						{
							const FBlendSample& TargetSample = Samples[Element.Indices[TargetIndex]];
							TArray<FVector2D> Points;

							Points.Add(SampleValueToGridPosition(SourceSample.SampleValue));
							Points.Add(SampleValueToGridPosition(TargetSample.SampleValue));

							// Draw line from and to element
							FSlateDrawElement::MakeLines(OutDrawElements, DrawLayerId, AllottedGeometry.ToPaintGeometry(), Points, MyClippingRect, ESlateDrawEffect::None, FLinearColor::White, true, 0.1f);
						}
					}
				}
			}
		}
	}
}

FReply SBlendSpaceGridWidget::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Check if we are in dropping state and if so snap to the grid and try to add the sample
	if (DragState == EDragState::DragDrop || DragState == EDragState::InvalidDragDrop )
	{
		if (DragState == EDragState::DragDrop)
		{
			const FVector2D GridPosition = SnapToClosestGridPoint(LocalMousePosition);
			const FVector SampleValue = GridPositionToSampleValue(GridPosition);
			
			TSharedPtr<FAssetDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
			if (DragDropOperation.IsValid())
			{
				UAnimSequence* Animation = FAssetData::GetFirstAsset<UAnimSequence>(DragDropOperation->AssetData);
				OnSampleAdded.ExecuteIfBound(Animation, SampleValue);
			}	
		}

		DragState = EDragState::None;
	}

	DragDropAnimationSequence = nullptr;

	return FReply::Unhandled();
}

void SBlendSpaceGridWidget::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid())
	{
		DragState = IsValidDragDropOperation(DragDropEvent, InvalidDragDropText) ? EDragState::DragDrop : EDragState::InvalidDragDrop;
	}
}

FReply SBlendSpaceGridWidget::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragState == EDragState::DragDrop || DragState == EDragState::InvalidDragDrop)
	{		
		LocalMousePosition = MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());				
		
		// Always update the tool tip, in case it became invalid
		TSharedPtr<FAssetDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
		if (DragDropOperation.IsValid())
		{
			DragDropOperation->SetToolTip(GetToolTipSampleValue(), DragDropOperation->GetIcon());
		}		

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SBlendSpaceGridWidget::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (DragState == EDragState::DragDrop || DragState == EDragState::InvalidDragDrop)
	{
		DragState = EDragState::None;
		DragDropAnimationSequence = nullptr;
	}
}

FReply SBlendSpaceGridWidget::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (this->HasMouseCapture())
	{
		if (DragState == EDragState::None || DragState == EDragState::PreDrag)
		{
			ProcessClick(MyGeometry, MouseEvent);
		}
		else if (DragState == EDragState::DragSample)
		{
			// Process drag ending			
			ResetToolTip();
		}

		// Reset drag state and index
		DragState = EDragState::None;
		DraggedSampleIndex = INDEX_NONE;

		return FReply::Handled().ReleaseMouseCapture();
	}
	else
	{
		return ProcessClick(MyGeometry, MouseEvent);
	}

	return FReply::Unhandled();
}

FReply SBlendSpaceGridWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// If we are over a sample, make it our currently (dragged) sample
		if (HighlightedSampleIndex != INDEX_NONE)
		{
			DraggedSampleIndex = SelectedSampleIndex = HighlightedSampleIndex;
			HighlightedSampleIndex = INDEX_NONE;
			ResetToolTip();
			DragState = EDragState::PreDrag;
			MouseDownPosition = LocalMousePosition;

			// Start mouse capture
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}		
	}

	return FReply::Handled();
}

FReply SBlendSpaceGridWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Cache the mouse position in local and screen space
	LocalMousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	LastMousePosition = MouseEvent.GetScreenSpacePosition();

	if (this->HasMouseCapture())
	{
		if (DragState == EDragState::None)
		{
			if (HighlightedSampleIndex != INDEX_NONE)
			{
				DragState = EDragState::DragSample;
				DraggedSampleIndex = HighlightedSampleIndex;
				HighlightedSampleIndex = INDEX_NONE;
			}
		}
		else if (DragState == EDragState::PreDrag)
		{
			// Actually start dragging
			if ((LocalMousePosition - MouseDownPosition).SizeSquared() > DragThresshold)
			{
				DragState = EDragState::DragSample;
				HighlightedSampleIndex = INDEX_NONE;
				ShowToolTip();
			}
		}
	}

	return FReply::Handled();
}

FReply SBlendSpaceGridWidget::ProcessClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		SelectedSampleIndex = INDEX_NONE;

		if (HighlightedSampleIndex == INDEX_NONE)
		{
			// If there isn't any sample currently being highlighted, retrieve all of them and see if we are over one
			const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
			for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
			{
				const FBlendSample& Sample = Samples[SampleIndex];
				const FVector2D GridPosition = SampleValueToGridPosition(Sample.SampleValue);

				const float MouseDistance = FMath::Abs(FVector2D::Distance(LocalMousePosition, GridPosition));
				if (MouseDistance < ClickThresshold)
				{
					SelectedSampleIndex = SampleIndex;					
					break;
				}
			}
		}
		else
		{
			// If we are over a sample, make it the selected sample index
			SelectedSampleIndex = HighlightedSampleIndex;
			HighlightedSampleIndex = INDEX_NONE;			
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// If we are over a sample open a context menu for editing its data
		if (HighlightedSampleIndex != INDEX_NONE)
		{	
			SelectedSampleIndex = HighlightedSampleIndex;

			// Create context menu
			TSharedPtr<SWidget> MenuContent = CreateBlendSampleContextMenu();
			
			// Reset highlight sample index
			HighlightedSampleIndex = INDEX_NONE;

			if (MenuContent.IsValid())
			{
				const FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
				const FVector2D MousePosition = MouseEvent.GetScreenSpacePosition();
				// This is of a fixed size atm since MenuContent->GetDesiredSize() will not take the detail customization into account and return an incorrect (small) size
				const FVector2D ExpectedSize(300, 100);				
				const FVector2D MenuPosition = FSlateApplication::Get().CalculatePopupWindowPosition(FSlateRect(MousePosition.X, MousePosition.Y, MousePosition.X, MousePosition.Y), ExpectedSize);

				FSlateApplication::Get().PushMenu(
					AsShared(),
					WidgetPath,
					MenuContent.ToSharedRef(),
					MenuPosition,
					FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
					);

				return FReply::Handled().SetUserFocus(MenuContent.ToSharedRef(), EFocusCause::SetDirectly).ReleaseMouseCapture();
			}
		}		
	}

	return FReply::Unhandled();
}

FReply SBlendSpaceGridWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Start previewing when either one of the shift keys is pressed
	if (bMouseIsOverGeometry && ((InKeyEvent.GetKey() == EKeys::LeftShift ) || (InKeyEvent.GetKey() == EKeys::RightShift)))
	{
		StartPreviewing();
		DragState = EDragState::Preview;
		// Make tool tip visible (this will display the current preview sample value)
		ShowToolTip();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SBlendSpaceGridWidget::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{	
	// Stop previewing when shift keys are released 
	if ((InKeyEvent.GetKey() == EKeys::LeftShift) || (InKeyEvent.GetKey() == EKeys::RightShift))
	{
		StopPreviewing();
		DragState = EDragState::None;
		ResetToolTip();
		return FReply::Handled();
	}

	// If delete is pressed and we currently have a sample selected remove it from the blendspace
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (SelectedSampleIndex != INDEX_NONE)
		{
			OnSampleRemoved.ExecuteIfBound(SelectedSampleIndex);
			
			if (SelectedSampleIndex == HighlightedSampleIndex)
			{
				HighlightedSampleIndex = INDEX_NONE;
				ResetToolTip();
			}

			SelectedSampleIndex = INDEX_NONE;
		}
	}

	// Pressing esc will remove the current key selection
	if( InKeyEvent.GetKey() == EKeys::Escape)
	{
		SelectedSampleIndex = INDEX_NONE;
	}

	return FReply::Unhandled();
}

TSharedPtr<SWidget> SBlendSpaceGridWidget::CreateBlendSampleContextMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);
	
	TSharedPtr<IStructureDetailsView> StructureDetailsView;
	// Initialize details view
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NotifyHook = NotifyHook;
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;		
	}
	
	FStructureDetailsViewArgs StructureViewArgs;
	{
		StructureViewArgs.bShowObjects = true;
		StructureViewArgs.bShowAssets = true;
		StructureViewArgs.bShowClasses = true;
		StructureViewArgs.bShowInterfaces = true;
	}
	
	StructureDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor")
		.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, nullptr, LOCTEXT("SampleData", "Blend Sample"));
	{
		const FBlendSample& Sample = BlendSpace->GetBlendSample(HighlightedSampleIndex);		
		StructureDetailsView->GetDetailsView().SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FBlendSampleDetails::MakeInstance, BlendSpace, this));

		FStructOnScope* Struct = new FStructOnScope(FBlendSample::StaticStruct(), (uint8*)&Sample);
		Struct->SetPackage(BlendSpace->GetOutermost());
		StructureDetailsView->SetStructureData(MakeShareable(Struct));
	}
	
	MenuBuilder.AddWidget(StructureDetailsView->GetWidget().ToSharedRef(), FText::GetEmpty(), true);

	return MenuBuilder.MakeWidget();
}

FReply SBlendSpaceGridWidget::ToggleTriangulationVisibility()
{
	bShowTriangulation = !bShowTriangulation;
	return FReply::Handled();
}

void SBlendSpaceGridWidget::CalculateGridPoints()
{
	CachedGridPoints.Empty(SampleGridDivisions.X * SampleGridDivisions.Y);
	for (int32 GridY = 0; GridY < ((GridType== EGridType::TwoAxis) ? SampleGridDivisions.Y + 1: 1); ++GridY)
	{
		for (int32 GridX = 0; GridX < SampleGridDivisions.X + 1; ++GridX)
		{
			// Calculate grid point in 0-1 form
			FVector2D GridPoint = FVector2D(GridX * (1.0f / SampleGridDivisions.X), (GridType == EGridType::TwoAxis) ? GridY * (1.0f / 
				SampleGridDivisions.Y) : 0.5f);

			// Multiply with size and offset according to the grid layout
			GridPoint *= CachedGridRectangle.GetSize();
			GridPoint += CachedGridRectangle.GetTopLeft();
			CachedGridPoints.Add(GridPoint);
		}
	}
}

const FVector2D SBlendSpaceGridWidget::SnapToClosestGridPoint(const FVector2D& InPosition) const
{
	// Clamp the screen position to the grid
	const FVector2D GridPosition( FMath::Clamp(InPosition.X, CachedGridRectangle.Left, CachedGridRectangle.Right),
								  FMath::Clamp(InPosition.Y, CachedGridRectangle.Top, CachedGridRectangle.Bottom));
	// Find the closest grid point
	float Distance = FLT_MAX;
	int32 GridPointIndex = INDEX_NONE;
	for (int32 Index = 0; Index < CachedGridPoints.Num(); ++Index)
	{
		const FVector2D& GridPoint = CachedGridPoints[Index];
		const float DistanceToGrid = FVector2D::DistSquared(GridPosition, GridPoint);
		if (DistanceToGrid < Distance)
		{
			Distance = DistanceToGrid;
			GridPointIndex = Index;
		}
	}

	checkf(GridPointIndex != INDEX_NONE, TEXT("Unable to find gridpoint"));

	return CachedGridPoints[GridPointIndex];
}

const FVector2D SBlendSpaceGridWidget::SampleValueToGridPosition(const FVector& SampleValue) const
{
	const FVector2D GridSize = CachedGridRectangle.GetSize();	
	const FVector2D GridCenter = GridSize * 0.5f;
		
	FVector2D SamplePosition2D;
	// Convert the sample value to -1 to 1 form
	SamplePosition2D.X = (((SampleValue.X - SampleValueMin.X) / SampleValueRange.X) * 2.0f) - 1.0f;
	SamplePosition2D.Y = (GridType == EGridType::TwoAxis) ? (((SampleValueMax.Y - SampleValue.Y) / SampleValueRange.Y) * 2.0f) - 1.0f : 0.0f;

	// Multiply by half of the grid size and offset is using the grid center position
	SamplePosition2D *= CachedGridRectangle.GetSize()* 0.5f;
	SamplePosition2D += CachedGridRectangle.GetCenter();

	return SamplePosition2D;	
}

const FVector SBlendSpaceGridWidget::GridPositionToSampleValue(const FVector2D& GridPosition) const
{
	FVector2D Position = GridPosition;
	// Move to center of grid and convert to 0 - 1 form
	Position -= CachedGridRectangle.GetCenter();
	Position /= (CachedGridRectangle.GetSize() * 0.5f);
	Position += FVector2D::UnitVector;
	Position *= 0.5f;

	// Calculate the sample value by mapping it to the blend parameter range
	const FVector SampleValue((Position.X * SampleValueRange.X) + SampleValueMin.X,
							  (GridType == EGridType::TwoAxis) ? SampleValueMax.Y - (Position.Y * SampleValueRange.Y) : 0.0f,
							  0.0f );
	return SampleValue;
}

const FSlateRect SBlendSpaceGridWidget::GetGridRectangleFromGeometry(const FGeometry& MyGeometry)
{
	FSlateRect WindowRect = FSlateRect(0, 0, MyGeometry.Size.X, MyGeometry.Size.Y);
	if (!bStretchToFit)
	{
		UpdateGridRationMargin(WindowRect.GetSize());
	}

	return WindowRect.InsetBy(GridMargin + GridRatioMargin);
}

void SBlendSpaceGridWidget::StartPreviewing()
{
	bSamplePreviewing = true;
	LastPreviewingMousePosition = LocalMousePosition;
	bPreviewPositionSet = true;	
}

void SBlendSpaceGridWidget::StopPreviewing()
{
	bSamplePreviewing = false;
	LastPreviewingMousePosition = LocalMousePosition;
}

FText SBlendSpaceGridWidget::GetToolTipAnimationName() const
{
	FText ToolTipText = FText::GetEmpty();
	const FText EmptyAnimationText = LOCTEXT("NoAnimationSetTooltipText","No Animation Set");
	switch (DragState)
	{
		// If we are not dragging, but over a valid blend sample return its animation asset name
		case EDragState::None:
		{		
			if (HighlightedSampleIndex != INDEX_NONE && BlendSpace->IsValidBlendSampleIndex(HighlightedSampleIndex))
			{
				const FBlendSample& BlendSample = BlendSpace->GetBlendSample(HighlightedSampleIndex);					
				ToolTipText = (BlendSample.Animation != nullptr) ? FText::FromString(BlendSample.Animation->GetName()) : EmptyAnimationText;
			}
			break;
		}

		case EDragState::PreDrag:
		{
			break;
		}

		// If we are dragging a sample return the dragged sample's animation asset name
		case EDragState::DragSample:
		{
			if (BlendSpace->IsValidBlendSampleIndex(DraggedSampleIndex))
			{
				const FBlendSample& BlendSample = BlendSpace->GetBlendSample(DraggedSampleIndex);
				ToolTipText = (BlendSample.Animation != nullptr) ? FText::FromString(BlendSample.Animation->GetName()) : EmptyAnimationText;
			}
			
			break;
		}

		// If we are performing a drag/drop operation return the cached operation animation name
		case EDragState::DragDrop:
		{
			ToolTipText = DragDropAnimationName;
			break;
		}

		case EDragState::InvalidDragDrop:
		{
			break;
		}
		
		// If we are previewing return a descriptive label
		case EDragState::Preview:
		{
			ToolTipText = FText::FromString("Preview value");
			break;
		}
		default:
			check(false);
	}

	return ToolTipText;
}

FText SBlendSpaceGridWidget::GetToolTipSampleValue() const
{
	FText ToolTipText = FText::GetEmpty();

	const FTextFormat ValueFormattingText = (GridType == EGridType::TwoAxis) ? FTextFormat::FromString("{0}: {1} - {2}: {3}") : FTextFormat::FromString("{0}: {1}");

	switch (DragState)
	{
		// If we are over a sample return its sample value if valid and otherwise show an error message as to why the sample is invalid
		case EDragState::None:
		{		
			if (HighlightedSampleIndex != INDEX_NONE)
			{
				const FBlendSample& BlendSample = BlendSpace->GetBlendSample(HighlightedSampleIndex);

				// Check if the sample is valid
				if (BlendSample.bIsValid)
				{
					ToolTipText = FText::Format(ValueFormattingText, ParameterXName, FText::FromString(FString::SanitizeFloat(BlendSample.SampleValue.X)), ParameterYName, FText::FromString(FString::SanitizeFloat(BlendSample.SampleValue.Y)));
				}
				else
				{
					ToolTipText = GetSampleErrorMessage(BlendSample);
				}
			}

			break;
		}

		case EDragState::PreDrag:
		{
			break;
		}

		// If we are dragging a sample return the current sample value it is hovered at
		case EDragState::DragSample:
		{
			if (DraggedSampleIndex != INDEX_NONE)
			{
				const FBlendSample& BlendSample = BlendSpace->GetBlendSample(DraggedSampleIndex);
				ToolTipText = FText::Format(ValueFormattingText, ParameterXName, FText::FromString(FString::SanitizeFloat(BlendSample.SampleValue.X)), ParameterYName, FText::FromString(FString::SanitizeFloat(BlendSample.SampleValue.Y)));
			}
			break;
		}

		// If we are performing a drag and drop operation return the current sample value it is hovered at
		case EDragState::DragDrop:
		{
			const FVector2D GridPoint = SnapToClosestGridPoint(LocalMousePosition);
			const FVector SampleValue = GridPositionToSampleValue(GridPoint);

			ToolTipText = FText::Format(ValueFormattingText, ParameterXName, FText::FromString(FString::SanitizeFloat(SampleValue.X)), ParameterYName, FText::FromString(FString::SanitizeFloat(SampleValue.Y)));

			break;
		}

		// If the drag and drop operation is invalid return the cached error message as to why it is invalid
		case EDragState::InvalidDragDrop:
		{
			ToolTipText = InvalidDragDropText;
			break;
		}

		// If we are setting the preview value return the current preview sample value
		case EDragState::Preview:
		{
			ToolTipText = FText::Format(ValueFormattingText, ParameterXName, FText::FromString(FString::SanitizeFloat(LastPreviewingSampleValue.X)), ParameterYName, FText::FromString(FString::SanitizeFloat(LastPreviewingSampleValue.Y)));

			break;
		}
		default:
			check(false);
	}

	return ToolTipText;
}

FText SBlendSpaceGridWidget::GetSampleErrorMessage(const FBlendSample &BlendSample) const
{
	const FVector2D GridPosition = SampleValueToGridPosition(BlendSample.SampleValue);
	// Either an invalid animation asset set
	if (BlendSample.Animation == nullptr)
	{
		static const FText NoAnimationErrorText = LOCTEXT("NoAnimationErrorText", "Invalid Animation for Sample");
		return NoAnimationErrorText;
	}
	// Or not aligned on the grid (which means that it does not match one of the cached grid points, == for FVector2D fails to compare though :/)
	else if (!CachedGridPoints.FindByPredicate([&](const FVector2D& Other) { return FMath::IsNearlyEqual(GridPosition.X, Other.X) && FMath::IsNearlyEqual(GridPosition.Y, Other.Y);}))
	{
		static const FText SampleNotAtGridPoint = LOCTEXT("SampleNotAtGridPointErrorText", "Sample is not on a valid Grid Point");
		return SampleNotAtGridPoint;
	}

	static const FText UnknownError = LOCTEXT("UnknownErrorText", "Sample is invalid for an Unknown Reason");
	return UnknownError;
}

void SBlendSpaceGridWidget::ShowToolTip()
{
	SetToolTip(ToolTip);
}

void SBlendSpaceGridWidget::ResetToolTip()
{
	SetToolTip(nullptr);
}

EVisibility SBlendSpaceGridWidget::GetInputBoxVisibility(const int32 ParameterIndex) const
{
	bool bVisible = true;
	// Only show input boxes when a sample is selected (hide it when one is being dragged since we have the tooltip information as well)
	bVisible &= (SelectedSampleIndex != INDEX_NONE && DraggedSampleIndex == INDEX_NONE);
	if ( ParameterIndex == 1 )
	{ 
		bVisible &= (GridType == EGridType::TwoAxis);
	}

	return bVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

TOptional<float> SBlendSpaceGridWidget::GetInputBoxValue(const int32 ParameterIndex) const
{
	checkf(ParameterIndex < 3, TEXT("Invalid parameter index, suppose to be within FVector array range"));
	float ReturnValue = 0.0f;
	if (SelectedSampleIndex != INDEX_NONE && SelectedSampleIndex < BlendSpace->GetNumberOfBlendSamples())
	{
		const FBlendSample& BlendSample = BlendSpace->GetBlendSample(SelectedSampleIndex);
		ReturnValue = BlendSample.SampleValue[ParameterIndex];
	}
	return ReturnValue;
}

TOptional<float> SBlendSpaceGridWidget::GetInputBoxMinValue(const int32 ParameterIndex) const
{
	checkf(ParameterIndex < 3, TEXT("Invalid parameter index, suppose to be within FVector array range"));
	return SampleValueMin[ParameterIndex];
}

TOptional<float> SBlendSpaceGridWidget::GetInputBoxMaxValue(const int32 ParameterIndex) const
{
	checkf(ParameterIndex < 3, TEXT("Invalid parameter index, suppose to be within FVector array range"));
	return SampleValueMax[ParameterIndex];
}

float SBlendSpaceGridWidget::GetInputBoxDelta(const int32 ParameterIndex) const
{
	checkf(ParameterIndex < 3, TEXT("Invalid parameter index, suppose to be within FVector array range"));
	return SampleGridDelta[ParameterIndex];
}

void SBlendSpaceGridWidget::OnInputBoxValueCommited(const float NewValue, ETextCommit::Type CommitType, const int32 ParameterIndex)
{
	OnInputBoxValueChanged(NewValue, ParameterIndex);
}

void SBlendSpaceGridWidget::OnInputBoxValueChanged(const float NewValue, const int32 ParameterIndex)
{
	checkf(ParameterIndex < 3, TEXT("Invalid parameter index, suppose to be within FVector array range"));

	if (SelectedSampleIndex != INDEX_NONE)
	{
		// Retrieve current sample value
		const FBlendSample& Sample = BlendSpace->GetBlendSample(SelectedSampleIndex);
		FVector SampleValue = Sample.SampleValue;

		// Calculate snapped value
		const float MinOffset = NewValue - SampleValueMin[ParameterIndex];
		float GridSteps = MinOffset / SampleGridDelta[ParameterIndex];
		int32 FlooredSteps = FMath::FloorToInt(GridSteps);
		GridSteps -= FlooredSteps;
		FlooredSteps = (GridSteps > .5f) ? FlooredSteps + 1 : FlooredSteps;

		// Temporary snap this value to closest point on grid (since the spin box delta does not provide the desired functionality)
		SampleValue[ParameterIndex] = SampleValueMin[ParameterIndex] + (FlooredSteps * SampleGridDelta[ParameterIndex]);
		OnSampleMoved.ExecuteIfBound(SelectedSampleIndex, SampleValue);
	}
}

EVisibility SBlendSpaceGridWidget::GetTriangulationButtonVisibility() const
{
	return (GridType == EGridType::TwoAxis) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SBlendSpaceGridWidget::ToggleFittingType()
{
	bStretchToFit = !bStretchToFit;

	// If toggle to stretching, reset the margin immediately
	if (bStretchToFit)
	{
		GridRatioMargin.Top = GridRatioMargin.Bottom = GridRatioMargin.Left = GridRatioMargin.Right = 0.0f;
	}

	return FReply::Handled();
}

void SBlendSpaceGridWidget::UpdateGridRationMargin(const FVector2D& GeometrySize)
{
	if (GridType == EGridType::TwoAxis)
	{
		// Reset values first
		GridRatioMargin.Top = GridRatioMargin.Bottom = GridRatioMargin.Left = GridRatioMargin.Right = 0.0f;

		if (SampleValueRange.X > SampleValueRange.Y)
		{
			if (GeometrySize.Y > GeometrySize.X)
			{
				const float Difference = GeometrySize.Y - GeometrySize.X;
				GridRatioMargin.Top = GridRatioMargin.Bottom = Difference * 0.5f;
			}
		}
		else if (SampleValueRange.X < SampleValueRange.Y)
		{
			if (GeometrySize.X > GeometrySize.Y)
			{
				const float Difference = GeometrySize.X - GeometrySize.Y;
				GridRatioMargin.Left = GridRatioMargin.Right = Difference * 0.5f;
			}
		}
	}
}

FText SBlendSpaceGridWidget::GetFittingTypeButtonToolTipText() const
{
	static const FText StretchText = LOCTEXT("StretchFittingText", "Stretch Grid to Fit");
	static const FText GridRatioText = LOCTEXT("GridRatioFittingText", "Fit Grid to Largest Axis");
	return (bStretchToFit) ? GridRatioText : StretchText;
}

EVisibility SBlendSpaceGridWidget::GetFittingButtonVisibility() const
{
	return (GridType == EGridType::TwoAxis) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SBlendSpaceGridWidget::UpdateCachedBlendParameterData()
{
	checkf(BlendSpace != nullptr, TEXT("Invalid BlendSpace pointer"));
	const FBlendParameter& BlendParameterX = BlendSpace->GetBlendParameter(0);
	const FBlendParameter& BlendParameterY = BlendSpace->GetBlendParameter(1);
	SampleValueRange.X = BlendParameterX.Max - BlendParameterX.Min;
	SampleValueRange.Y = BlendParameterY.Max - BlendParameterY.Min;
	
	SampleValueMin.X = BlendParameterX.Min;
	SampleValueMin.Y = BlendParameterY.Min;

	SampleValueMax.X = BlendParameterX.Max;
	SampleValueMax.Y = BlendParameterY.Max;

	SampleGridDelta = SampleValueRange;
	SampleGridDelta.X /= (BlendParameterX.GridNum);
	SampleGridDelta.Y /= (BlendParameterY.GridNum);

	SampleGridDivisions.X = BlendParameterX.GridNum;
	SampleGridDivisions.Y = BlendParameterY.GridNum;

	ParameterXName = FText::FromString(BlendParameterX.DisplayName);
	ParameterYName = FText::FromString(BlendParameterY.DisplayName);
	
	const TSharedRef< FSlateFontMeasure > FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	MaxVerticalAxisTextWidth = HorizontalAxisMaxTextWidth = MaxHorizontalAxisTextHeight = 0.0f;
	FVector2D TextSize = FontMeasure->Measure(ParameterYName, FontInfo);	
	MaxVerticalAxisTextWidth = FMath::Max(MaxVerticalAxisTextWidth, TextSize.X);

	TextSize = FontMeasure->Measure(FString::SanitizeFloat(SampleValueMin.Y), FontInfo);
	MaxVerticalAxisTextWidth = FMath::Max(MaxVerticalAxisTextWidth, TextSize.X);

	TextSize = FontMeasure->Measure(FString::SanitizeFloat(SampleValueMax.Y), FontInfo);
	MaxVerticalAxisTextWidth = FMath::Max(MaxVerticalAxisTextWidth, TextSize.X);
	
	TextSize = FontMeasure->Measure(ParameterXName, FontInfo);
	MaxHorizontalAxisTextHeight = FMath::Max(MaxHorizontalAxisTextHeight, TextSize.Y);

	TextSize = FontMeasure->Measure(FString::SanitizeFloat(SampleValueMin.X), FontInfo);
	MaxHorizontalAxisTextHeight = FMath::Max(MaxHorizontalAxisTextHeight, TextSize.Y);

	TextSize = FontMeasure->Measure(FString::SanitizeFloat(SampleValueMax.X), FontInfo);
	MaxHorizontalAxisTextHeight = FMath::Max(MaxHorizontalAxisTextHeight, TextSize.Y);
	HorizontalAxisMaxTextWidth = TextSize.X;
}

void SBlendSpaceGridWidget::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bMouseIsOverGeometry = true;
}

void SBlendSpaceGridWidget::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bMouseIsOverGeometry = false;
}

void SBlendSpaceGridWidget::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	HighlightedSampleIndex = DraggedSampleIndex = INDEX_NONE;
	DragState = EDragState::None;
	bSamplePreviewing = false;
	ResetToolTip();
}

bool SBlendSpaceGridWidget::SupportsKeyboardFocus() const
{
	return true;
}

void SBlendSpaceGridWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const int32 PreviousSampleIndex = HighlightedSampleIndex;
	HighlightedSampleIndex = INDEX_NONE;

	if (DragState == EDragState::None)
	{
		// Determine highlighted sample
		const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const FBlendSample& Sample = Samples[SampleIndex];

			// Ranges from 0 to 1
			FVector2D GridPosition = SampleValueToGridPosition(Sample.SampleValue);
			FLinearColor DrawColor = FLinearColor::White;
			const float MouseDistance = FVector2D::Distance(LocalMousePosition, GridPosition);
			if (FMath::Abs(MouseDistance) < 12.0f)
			{
				HighlightedSampleIndex = SampleIndex;
				break;
			}
		}

		// If we started selecting or selected a different sample make sure we show/hide the tooltip
		if (PreviousSampleIndex != HighlightedSampleIndex)
		{
			if (HighlightedSampleIndex != INDEX_NONE)
			{
				ShowToolTip();
			}
			else
			{
				ResetToolTip();
			}
		}
		
	}
	else if (DragState == EDragState::DragSample)
	{
		// If we are dragging a sample, find out whether or not it has actually moved to a different grid position since the last tick and update the blend space accordingly
		const FBlendSample& BlendSample = BlendSpace->GetBlendSample(DraggedSampleIndex);
		const FVector2D GridPosition = SnapToClosestGridPoint(LocalMousePosition);
		const FVector SampleValue = GridPositionToSampleValue(GridPosition);		

		if (SampleValue != LastDragPosition)
		{
			LastDragPosition = SampleValue;
			OnSampleMoved.ExecuteIfBound(DraggedSampleIndex, SampleValue);
		}
	}
	else if (DragState == EDragState::DragDrop || DragState == EDragState::InvalidDragDrop)
	{
		// Validate that the sample is not overlapping with a current sample when doing a drag/drop operation and that we are dropping a valid animation for the blend space (type)
		const FVector DropSampleValue = GridPositionToSampleValue(SnapToClosestGridPoint(LocalMousePosition));
		const bool bValidSample = BlendSpace->ValidateSampleValue(DropSampleValue);
		const bool bValidSequence = ValidateAnimationSequence(DragDropAnimationSequence, InvalidDragDropText);

		if (!bValidSequence)
		{
			DragState = EDragState::InvalidDragDrop;
		}
		else if (!bValidSample)
		{			
			InvalidDragDropText = InvalidSamplePositionDragDropText;
			DragState = EDragState::InvalidDragDrop;
		}
		else if (bValidSample && bValidSequence)
		{
			DragState = EDragState::DragDrop;
		}
	}

	// Check if we should update the preview sample value
	if (bSamplePreviewing)
	{
		// Ensure the preview mouse position is clamped to the grid
		LastPreviewingMousePosition.X = FMath::Clamp(LocalMousePosition.X, CachedGridRectangle.Left, CachedGridRectangle.Right);
		LastPreviewingMousePosition.Y = FMath::Clamp(LocalMousePosition.Y, CachedGridRectangle.Top, CachedGridRectangle.Bottom);
		LastPreviewingSampleValue = GridPositionToSampleValue(LastPreviewingMousePosition);
	}

	// Refresh cache blendspace/grid data if needed
	if (bRefreshCachedData)
	{
		UpdateCachedBlendParameterData();
		GridMargin = FMargin(MaxVerticalAxisTextWidth + (TextMargin * 2.0f), TextMargin, (HorizontalAxisMaxTextWidth *.5f) + TextMargin, MaxHorizontalAxisTextHeight + (TextMargin * 2.0f));
		bRefreshCachedData = false;
	}
	
	// Always need to update the rectangle and grid points according to the geometry (this can differ per tick)
	CachedGridRectangle = GetGridRectangleFromGeometry(AllottedGeometry);
	CalculateGridPoints();
}

const FVector SBlendSpaceGridWidget::GetBlendPreviewValue()
{	
	return LastPreviewingSampleValue;
}

void SBlendSpaceGridWidget::InvalidateCachedData()
{
	bRefreshCachedData = true;	
}

const bool SBlendSpaceGridWidget::IsValidDragDropOperation(const FDragDropEvent& DragDropEvent, FText& InvalidOperationText)
{
	bool bResult = false;

	TSharedPtr<FAssetDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FAssetDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		// Check whether or not this animation is compatible with the blend space
		DragDropAnimationSequence = FAssetData::GetFirstAsset<UAnimSequence>(DragDropOperation->AssetData);
		if (DragDropAnimationSequence)
		{
			bResult = ValidateAnimationSequence(DragDropAnimationSequence, InvalidOperationText);
		}
		else
		{
			// If is isn't an animation set error message
			bResult = false;
			InvalidOperationText = FText::FromString("Invalid Asset Type");
		}		
	}

	if (!bResult)
	{
		DragDropOperation->SetToolTip(InvalidOperationText, DragDropOperation->GetIcon());
	}

	return bResult;
}

bool SBlendSpaceGridWidget::ValidateAnimationSequence(const UAnimSequence* AnimationSequence, FText& InvalidOperationText) const
{	
	if (AnimationSequence != nullptr)
	{
		// If there are any existing blend samples check whether or not the the animation should be additive and if so if the additive matches the existing samples
		if (BlendSpace->GetNumberOfBlendSamples() > 0)
		{
			const bool bIsAdditive = BlendSpace->ShouldAnimationBeAdditive();
			if (AnimationSequence->IsValidAdditive() != bIsAdditive)
			{
				InvalidOperationText = FText::FromString(bIsAdditive ? "Animation should be additive" : "Animation should be non-additive");
				return false;
			}

			// If it is the supported additive type, but does not match existing samples
			if (!BlendSpace->DoesAnimationMatchExistingSamples(AnimationSequence))
			{
				InvalidOperationText = FText::FromString("Additive Animation Type does not match existing Samples");
				return false;
			}
		}

		// Check if the supplied animation is of a different additive animation type 
		if (!BlendSpace->IsAnimationCompatible(AnimationSequence))
		{
			InvalidOperationText = FText::FromString("Invalid Additive Animation Type");
			return false;
		}

		// Check if the supplied animation is compatible with the skeleton
		if (!BlendSpace->IsAnimationCompatibleWithSkeleton(AnimationSequence))
		{
			InvalidOperationText = FText::FromString("Animation is incompatible with the skeleton");
			return false;
		}
	}

	return AnimationSequence != nullptr;
}

#undef LOCTEXT_NAMESPACE // "SAnimationBlendSpaceGridWidget"
