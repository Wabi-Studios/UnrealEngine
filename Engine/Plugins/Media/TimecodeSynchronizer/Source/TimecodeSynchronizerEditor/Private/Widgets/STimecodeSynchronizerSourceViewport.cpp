// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/STimecodeSynchronizerSourceViewport.h"
#include "TimecodeSynchronizer.h"

#include "EditorStyleSet.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/App.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"



#define LOCTEXT_NAMESPACE "STimecodeSynchronizercSourceViewport"


/* STimecodeSynchronizerSourceViewport structors
*****************************************************************************/

STimecodeSynchronizerSourceViewport::STimecodeSynchronizerSourceViewport()
	: Collector(this)
	, SourceTextBox(nullptr)
	, TimecodeSynchronization(nullptr)
	, AttachedSourceIndex(INDEX_NONE)
	, bIsSynchronizedSource(false)
	, Material(nullptr)
	, MaterialBrush(nullptr)
	, TextureSampler(nullptr)
{ }

/* STimecodeSynchronizerSourceViewport interface
*****************************************************************************/

void STimecodeSynchronizerSourceViewport::Construct(const FArguments& InArgs, UTimecodeSynchronizer* InTimecodeSynchronizer, int32 InAttachedSourceIndex, bool bInIsSynchronizedSource, UTexture* InTexture)
{
	TimecodeSynchronization = InTimecodeSynchronizer;
	AttachedSourceIndex = InAttachedSourceIndex;
	bIsSynchronizedSource = bInIsSynchronizedSource;
	
	//If no texture is attached, a default static texture will be used (for non live viewable sources)
	MaterialBrush = nullptr;
	if (InTexture != nullptr)
	{
		// create wrapper material
		Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);

		if (Material != nullptr)
		{
			TextureSampler = NewObject<UMaterialExpressionTextureSample>(Material);
			{
				TextureSampler->Texture = InTexture;
				TextureSampler->AutoSetSampleType();
			}

			FExpressionOutput& Output = TextureSampler->GetOutputs()[0];
			FExpressionInput& Input = Material->EmissiveColor;
			{
				Input.Expression = TextureSampler;
				Input.Mask = Output.Mask;
				Input.MaskR = Output.MaskR;
				Input.MaskG = Output.MaskG;
				Input.MaskB = Output.MaskB;
				Input.MaskA = Output.MaskA;
			}

			Material->Expressions.Add(TextureSampler);
			Material->MaterialDomain = EMaterialDomain::MD_UI;
			Material->PostEditChange();
		}
		
		// create Slate brush
		MaterialBrush = MakeShareable(new FSlateBrush());
		{
			MaterialBrush->SetResourceObject(Material);
		}
	}


	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				//Source display name
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(1.0f, 1.0f, 1.0f, 1.0f)
				[
					// Display name box
					SAssignNew(SourceTextBox, SEditableTextBox)
					.ClearKeyboardFocusOnCommit(true)
					.HintText(LOCTEXT("SourceTextBoxHint", "Source Name"))
					.IsReadOnly(true)
					.Text_Lambda([&]() -> FText
					{
						const FTimecodeSynchronizerActiveTimecodedInputSource* AttachedSource = GetAttachedSource();
						if (AttachedSource && AttachedSource->IsReady())
						{
							return FText::FromString(GetAttachedSource()->GetDisplayName());
						}
						return FText();
					})
				]
			]
		
			+ SVerticalBox::Slot()
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					// Live view of the Source
					SNew(SScaleBox)
					.Stretch_Lambda([]() -> EStretch::Type { return EStretch::Fill;	})
					[
						SNew(SImage)
						.Image(MaterialBrush.IsValid() ? MaterialBrush.Get() : FEditorStyle::GetBrush("WhiteTexture"))
					]
				]

				+ SOverlay::Slot()
				.Padding(FMargin(12.0f, 8.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Top)
					[

						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						[
							//Source Timecode Interval
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							[
								// Min Timecode
								SNew(STextBlock)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.18"))
								.ShadowOffset(FVector2D(1.f, 1.f))
								.Text(this, &STimecodeSynchronizerSourceViewport::HandleIntervalMinTimecodeText)
								.ToolTipText(LOCTEXT("OverlayMinTimecodeDataTooltip", "Buffered minimum Timecode of this source"))
							]
							
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Bottom)
							[
								// Max Timecode
								SNew(STextBlock)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.18"))
								.ShadowOffset(FVector2D(1.f, 1.f))
								.Text(this, &STimecodeSynchronizerSourceViewport::HandleIntervalMaxTimecodeText)
								.ToolTipText(LOCTEXT("OverlayMaxTimecodeDataTooltip", "Buffered maximum Timecode of this source"))
							]
						]
					]
					
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Bottom)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Bottom)
						[
							// Display if source is the master
							SNew(STextBlock)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.18"))
							.ShadowOffset(FVector2D(1.f, 1.f))
							.Text(this, &STimecodeSynchronizerSourceViewport::HandleIsSourceMasterText)
							.ToolTipText(LOCTEXT("OverlayMasterSourceTooltip", "Is this source used as the master"))
						]
						
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						[
							// Current Timecode
							SNew(STextBlock)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.18"))
							.ShadowOffset(FVector2D(1.f, 1.f))
							.Text(this, &STimecodeSynchronizerSourceViewport::HandleCurrentTimecodeText)
							.ToolTipText(LOCTEXT("OverlayCurrentTimecodeDataTooltip", "Current Timecode of system"))
						]
					]
				]
			]
		]
	];
}


/* STimecodeSynchronizerSourceViewport callbacks
*****************************************************************************/
FText STimecodeSynchronizerSourceViewport::HandleIntervalMinTimecodeText() const
{
	FTimecode Timecode;
	const FTimecodeSynchronizerActiveTimecodedInputSource* AttachedSource = GetAttachedSource();
	if (AttachedSource && AttachedSource->IsReady())
	{
		const FFrameNumber OldestFrame = AttachedSource->GetInputSourceState().OldestAvailableSample.GetFrame();
		const bool bIsDropFrame = FTimecode::IsDropFormatTimecodeSupported(AttachedSource->GetFrameRate());
		Timecode = FTimecode::FromFrameNumber(OldestFrame, AttachedSource->GetFrameRate(), bIsDropFrame);
	}

	return FText::FromString(Timecode.ToString());
}

FText STimecodeSynchronizerSourceViewport::HandleIntervalMaxTimecodeText() const
{
	FTimecode Timecode;
	const FTimecodeSynchronizerActiveTimecodedInputSource* AttachedSource = GetAttachedSource();
	if (AttachedSource && AttachedSource->IsReady())
	{
		const FFrameNumber NewestFrame = AttachedSource->GetInputSourceState().NewestAvailableSample.GetFrame();
		const bool bIsDropFrame = FTimecode::IsDropFormatTimecodeSupported(AttachedSource->GetFrameRate());
		Timecode = FTimecode::FromFrameNumber(NewestFrame, AttachedSource->GetFrameRate(), bIsDropFrame);
	}

	return FText::FromString(Timecode.ToString());
}

FText STimecodeSynchronizerSourceViewport::HandleCurrentTimecodeText() const
{
	return FText::FromString(FApp::GetTimecode().ToString());
}

FText STimecodeSynchronizerSourceViewport::HandleIsSourceMasterText() const
{
	FString Role;
	if (TimecodeSynchronization && AttachedSourceIndex != INDEX_NONE && bIsSynchronizedSource && TimecodeSynchronization->GetActiveMasterSynchronizationTimecodedSourceIndex() == AttachedSourceIndex)
	{
		Role = "Master";
	}

	return FText::FromString(Role);
}

const FTimecodeSynchronizerActiveTimecodedInputSource* STimecodeSynchronizerSourceViewport::GetAttachedSource() const
{
	const FTimecodeSynchronizerActiveTimecodedInputSource* Result = nullptr;

	if (TimecodeSynchronization && AttachedSourceIndex != INDEX_NONE)
	{
		const TArray<FTimecodeSynchronizerActiveTimecodedInputSource>& Sources = bIsSynchronizedSource ? TimecodeSynchronization->GetSynchronizedSources() : TimecodeSynchronization->GetNonSynchronizedSources();
		if (Sources.IsValidIndex(AttachedSourceIndex))
		{
			Result = &Sources[AttachedSourceIndex];
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
