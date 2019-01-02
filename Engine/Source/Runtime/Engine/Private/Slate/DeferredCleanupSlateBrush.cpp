// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Slate/DeferredCleanupSlateBrush.h"
#include "Engine/Texture.h"

TSharedRef<FDeferredCleanupSlateBrush> FDeferredCleanupSlateBrush::CreateBrush(const FSlateBrush& Brush)
{
	return MakeShareable(new FDeferredCleanupSlateBrush(Brush), [](FDeferredCleanupSlateBrush* ObjectToDelete) { BeginCleanup(ObjectToDelete); });
}

TSharedRef<FDeferredCleanupSlateBrush> FDeferredCleanupSlateBrush::CreateBrush(
	UTexture* InTexture,
	const FLinearColor& InTint,
	ESlateBrushTileType::Type InTiling,
	ESlateBrushImageType::Type InImageType)
{
	FSlateBrush Brush;
	Brush.SetResourceObject(InTexture);
	Brush.ImageSize = FVector2D(InTexture->GetSurfaceWidth(), InTexture->GetSurfaceHeight());
	Brush.TintColor = InTint;
	Brush.Tiling = InTiling;
	Brush.ImageType = InImageType;

	return MakeShareable(new FDeferredCleanupSlateBrush(Brush), [](FDeferredCleanupSlateBrush* ObjectToDelete) { BeginCleanup(ObjectToDelete); });
}

TSharedRef<FDeferredCleanupSlateBrush> FDeferredCleanupSlateBrush::CreateBrush(
	UObject* InResource,
	const FVector2D& InImageSize,
	const FLinearColor& InTint,
	ESlateBrushTileType::Type InTiling,
	ESlateBrushImageType::Type InImageType)
{
	FSlateBrush Brush;
	Brush.SetResourceObject(InResource);
	Brush.ImageSize = InImageSize;
	Brush.TintColor = InTint;
	Brush.Tiling = InTiling;
	Brush.ImageType = InImageType;

	return MakeShareable(new FDeferredCleanupSlateBrush(Brush), [](FDeferredCleanupSlateBrush* ObjectToDelete) { BeginCleanup(ObjectToDelete); });
}

void FDeferredCleanupSlateBrush::AddReferencedObjects(FReferenceCollector& Collector)
{
	InternalBrush.AddReferencedObjects(Collector);
}

FString FDeferredCleanupSlateBrush::GetReferencerName() const
{
	return "FDeferredCleanupSlateBrush";
}