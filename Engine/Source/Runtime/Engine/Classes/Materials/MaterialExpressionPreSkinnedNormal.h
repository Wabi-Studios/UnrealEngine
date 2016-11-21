// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionPreSkinnedNormal.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionPreSkinnedNormal : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
#endif
	//~ End UMaterialExpression Interface
};
