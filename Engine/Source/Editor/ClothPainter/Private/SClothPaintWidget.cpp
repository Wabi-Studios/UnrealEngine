// Copyright Epic Games, Inc. All Rights Reserved.

#include "SClothPaintWidget.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

#include "ClothPaintSettingsCustomization.h"
#include "MeshPaintSettings.h"
#include "ClothPainter.h"
#include "ClothPaintSettings.h"
#include "ClothingAsset.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "ClothPaintToolBase.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Images/SImage.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "ApexClothingUtils.h"

#define LOCTEXT_NAMESPACE "ClothPaintWidget"

void SClothPaintWidget::Construct(const FArguments& InArgs, FClothPainter* InPainter)
{
	Painter = InPainter;

	if(Painter)
	{
		Objects.Add(Painter->GetBrushSettings());
		Objects.Add(Painter->GetPainterSettings());

		UObject* ToolSettings = Painter->GetSelectedTool()->GetSettingsObject();
		if(ToolSettings)
		{
			Objects.Add(ToolSettings);
			Painter->GetSelectedTool()->RegisterSettingsObjectCustomizations(DetailsView.Get());
		}

		ClothPainterSettings = Cast<UClothPainterSettings>(InPainter->GetPainterSettings());
		CreateDetailsView(InPainter);
	}

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Content()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0)
				[
					DetailsView->AsShared()
				]
			]
		]
	];
}

void SClothPaintWidget::CreateDetailsView(FClothPainter* InPainter)
{
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	
	DetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	DetailsView->RegisterInstancedCustomPropertyLayout(UClothPainterSettings::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FClothPaintSettingsCustomization::MakeInstance, InPainter));
	DetailsView->RegisterInstancedCustomPropertyLayout(UPaintBrushSettings::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FClothPaintBrushSettingsCustomization::MakeInstance));

	DetailsView->SetObjects(Objects, true);
}

void SClothPaintWidget::OnRefresh()
{
	if(DetailsView.IsValid())
	{
		Objects.Reset();

		Objects.Add(Painter->GetPainterSettings());

		UObject* ToolSettings = Painter->GetSelectedTool()->GetSettingsObject();
		if(ToolSettings)
		{
			Objects.Add(ToolSettings);
			Painter->GetSelectedTool()->RegisterSettingsObjectCustomizations(DetailsView.Get());
		}

		Objects.Add(Painter->GetBrushSettings());

		DetailsView->SetObjects(Objects, true);
	}
}

void SClothPaintWidget::Reset()
{
	OnRefresh();
}

#undef LOCTEXT_NAMESPACE
