// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphPrivatePCH.h"
#include "AnimGraphNode_CurveSource.h"

#define LOCTEXT_NAMESPACE "ExternalCurve"

FString UAnimGraphNode_CurveSource::GetNodeCategory() const
{
	return TEXT("Curves");
}

FText UAnimGraphNode_CurveSource::GetTooltipText() const
{
	return LOCTEXT("CurveSourceDescription", "A programmatic source for curves.\nBinds by name to an object that implements ICurveSourceInterface.\nFirst we check the actor that owns this (if any), then we check each of its components to see if we should bind to the source that matches this name.");
}

FText UAnimGraphNode_CurveSource::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType != ENodeTitleType::MenuTitle && Node.SourceBinding != NAME_None)
	{
		return FText::Format(LOCTEXT("AnimGraphNode_CurveSource_Title", "Curve Source: {0}"), FText::FromName(Node.SourceBinding));
	}
	else
	{
		return LOCTEXT("AnimGraphNode_CurveSource_Title", "Curve Source");
	}
}

#undef LOCTEXT_NAMESPACE
