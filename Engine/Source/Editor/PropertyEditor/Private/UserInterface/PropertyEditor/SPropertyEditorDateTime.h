// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SWidget.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Presentation/PropertyEditor/PropertyEditor.h"
#include "UserInterface/PropertyEditor/PropertyEditorConstants.h"

class SPropertyEditorDateTime : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SPropertyEditorDateTime )
		: _Font( FAppStyle::GetFontStyle( PropertyEditorConstants::PropertyFontStyle ) ) 
		{}
		SLATE_ATTRIBUTE( FSlateFontInfo, Font )
	SLATE_END_ARGS()

	static bool Supports( const TSharedRef< class FPropertyEditor >& InPropertyEditor );

	void Construct( const FArguments& InArgs, const TSharedRef< class FPropertyEditor >& InPropertyEditor );

	void GetDesiredWidth( float& OutMinDesiredWidth, float& OutMaxDesiredWidth );

private:

	virtual void HandleTextCommitted( const FText& NewText, ETextCommit::Type CommitInfo );

	bool CanEdit() const;

private:

	TSharedPtr< class FPropertyEditor > PropertyEditor;

	TSharedPtr< class SWidget > PrimaryWidget;
};
