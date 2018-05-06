// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MaterialExpressionLandscapeGrassCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/UnrealType.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "ContentBrowserDelegates.h"
#include "AssetRegistry/Public/AssetData.h"
#include "PropertyCustomizationHelpers.h"
#include "LandscapeGrassType.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"

#define LOCTEXT_NAMESPACE "MaterialExpressionLandscapeGrassCustomization"

TSharedRef<IPropertyTypeCustomization> FMaterialExpressionLandscapeGrassInputCustomization::MakeInstance()
{
	return MakeShareable(new FMaterialExpressionLandscapeGrassInputCustomization);
}

void FMaterialExpressionLandscapeGrassInputCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		];
}

void FMaterialExpressionLandscapeGrassInputCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedPtr<IPropertyHandle> GrassTypeHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FGrassInput, GrassType));

	TArray<UObject*> OwningObjects;
	StructPropertyHandle->GetOuterObjects(OwningObjects);
	
	if (OwningObjects.Num() == 1)
	{
		MaterialNode = CastChecked<UMaterialExpressionLandscapeGrassOutput>(OwningObjects[0]);
	}

	IDetailPropertyRow& GrassTypeRow = StructBuilder.AddProperty(GrassTypeHandle.ToSharedRef());
	
	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	FDetailWidgetRow Row;
	GrassTypeRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

	FDetailWidgetRow &DetailWidgetRow = GrassTypeRow.CustomWidget();
	DetailWidgetRow.NameContent()
		.MinDesiredWidth(Row.NameWidget.MinWidth)
		.MaxDesiredWidth(Row.NameWidget.MaxWidth)
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.MinDesiredWidth(Row.ValueContent().MaxWidth)
		.MaxDesiredWidth(Row.ValueContent().MaxWidth)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(ULandscapeGrassType::StaticClass())
				.PropertyHandle(GrassTypeHandle)
				.ThumbnailPool(StructCustomizationUtils.GetThumbnailPool())
				.OnShouldFilterAsset(this, &FMaterialExpressionLandscapeGrassInputCustomization::OnShouldFilterAsset)
			]
		];
}

bool FMaterialExpressionLandscapeGrassInputCustomization::OnShouldFilterAsset(const FAssetData& InAssetData)
{
	if (MaterialNode != nullptr)
	{
		for (const FGrassInput& GrassInput : MaterialNode->GrassTypes)
		{
			if (GrassInput.GrassType != nullptr)
			{
				if (GrassInput.GrassType->GetFName() == InAssetData.AssetName) // already used
				{
					return true;
				}
			}
		}		
	}

	return false;
}

#undef LOCTEXT_NAMESPACE 
