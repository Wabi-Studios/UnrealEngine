// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PreviewScene.h"
#include "PaperEditorViewportClient.h"
#include "SpriteEditor/SpriteEditorSelections.h"

//////////////////////////////////////////////////////////////////////////
// FSingleTileEditorViewportClient

class FSingleTileEditorViewportClient : public FPaperEditorViewportClient, public ISpriteSelectionContext
{
public:
	FSingleTileEditorViewportClient(UPaperTileSet* InTileSet);

	// FViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	// End of FViewportClient interface

	// FEditorViewportClient interface
	virtual FLinearColor GetBackgroundColor() const override;
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override;
	virtual void TrackingStopped() override;
	// End of FEditorViewportClient interface

	// ISpriteSelectionContext interface
	virtual FVector2D SelectedItemConvertWorldSpaceDeltaToLocalSpace(const FVector& WorldSpaceDelta) const override;
	virtual FVector2D WorldSpaceToTextureSpace(const FVector& SourcePoint) const override;
	virtual FVector TextureSpaceToWorldSpace(const FVector2D& SourcePoint) const override;
	virtual float SelectedItemGetUnitsPerPixel() const override;
	virtual void BeginTransaction(const FText& SessionName) override;
	virtual void MarkTransactionAsDirty() override;
	virtual void EndTransaction() override;
	virtual void InvalidateViewportAndHitProxies() override;
	// End of ISpriteSelectionContext interface

	void SetTileIndex(int32 InTileIndex);
	int32 GetTileIndex() const;
	void OnActiveTileIndexChanged(const FIntPoint& TopLeft, const FIntPoint& Dimensions);

	void ActivateEditMode(TSharedPtr<FUICommandList> InCommandList);

protected:
	// FPaperEditorViewportClient interface
	virtual FBox GetDesiredFocusBounds() const override;
	// End of FPaperEditorViewportClient

public:
	// Tile set
	UPaperTileSet* TileSet;

	int32 TileBeingEditedIndex;

	// Are we currently manipulating something?
	bool bManipulating;

	// Did we dirty something during manipulation?
	bool bManipulationDirtiedSomething;

	// Pointer back to the sprite editor viewport control that owns us
	TWeakPtr<class SEditorViewport> SpriteEditorViewportPtr;

	// The current transaction for undo/redo
	class FScopedTransaction* ScopedTransaction;

	// The preview scene
	FPreviewScene OwnedPreviewScene;

	// The preview sprite in the scene
	UPaperSpriteComponent* PreviewTileSpriteComponent;
};
