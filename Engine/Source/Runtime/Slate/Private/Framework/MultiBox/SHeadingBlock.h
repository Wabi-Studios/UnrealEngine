// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/MultiBox/MultiBox.h"

/**
 * Heading MultiBlock
 */
class FHeadingBlock
	: public FMultiBlock
{

public:

	/**
	 * Constructor
	 *
	 * @param	InHeadingText	Heading text
	 */
	FHeadingBlock( const FName& InExtensionHook, const TAttribute< FText >& InHeadingText );


private:

	/** FMultiBlock private interface */
	virtual TSharedRef< class IMultiBlockBaseWidget > ConstructWidget() const override;


private:

	// Friend our corresponding widget class
	friend class SHeadingBlock;

	/** Text for this heading */
	TAttribute< FText > HeadingText;
};




/**
 * Heading MultiBlock widget
 */
class SLATE_API SHeadingBlock
	: public SMultiBlockBaseWidget
{

public:

	SLATE_BEGIN_ARGS( SHeadingBlock ){}

	SLATE_END_ARGS()


	/**
	 * Builds this MultiBlock widget up from the MultiBlock associated with it
	 */
	virtual void BuildMultiBlockWidget(const ISlateStyle* StyleSet, const FName& StyleName) override;


	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs );

private:

};
