// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGEditorSettings.generated.h"

struct FEdGraphPinType;

UCLASS(config=EditorPerProjectUserSettings)
class PCGEDITOR_API UPCGEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	UPCGEditorSettings(const FObjectInitializer& ObjectInitializer);

	FLinearColor GetColor(UPCGSettings* InSettings) const;
	FLinearColor GetPinColor(const FEdGraphPinType& InPinType) const;

	/** Default node color */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor DefaultNodeColor;

	/** Color used for input & output nodes */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor InputOutputNodeColor;

	/** Color used for Difference, Intersection, Projection, Union */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor SetOperationNodeColor;

	/** Color used for density remap */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor DensityOperationNodeColor;

	/** Color used for blueprints */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor BlueprintNodeColor;

	/** Color used for metadata operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor MetadataNodeColor;

	/** Color used for filter-like operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor FilterNodeColor;

	/** Color used for sampler operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor SamplerNodeColor;

	/** Color used for artifact-generating operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor SpawnerNodeColor;

	/** Color used for subgraph-like operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor SubgraphNodeColor;

	/** Color used for debug operations */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor DebugNodeColor;

	/** Default pin color */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor DefaultPinColor;

	/** Color used for spatial data pins */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor SpatialDataPinColor;

	/** Color used for render targets / GPU pins */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor RenderTargetDataPinColor;

	/** Color used for param data pins */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor ParamDataPinColor;

	/** Color used for other/unknown data types */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	FLinearColor UnknownDataPinColor;

	/** User-driven color overrides */
	UPROPERTY(EditAnywhere, config, Category = Node, meta = (HideAlphaChannel))
	TMap<TSubclassOf<UPCGSettings>, FLinearColor> OverrideNodeColorByClass;

	/** Specify if we want to jump to definition in case of double click on native PCG Nodes */
	UPROPERTY(EditAnywhere, config, Category = Workflow)
	bool bEnableNavigateToNativeNodes;
};