﻿// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FontFaceInterface.h"
#include "FontFace.generated.h"

/**
 * A font face asset contains the raw payload data for a source TTF/OTF file as used by FreeType.
 * During cook this asset type generates a ".ufont" file containing the raw payload data.
 */
UCLASS(hidecategories=Object, autoexpandcategories=FontFace, MinimalAPI, BlueprintType)
class UFontFace : public UObject, public IFontFaceInterface
{
	GENERATED_BODY()

public:
	/** Default constructor */
	UFontFace();

	//~ Begin UObject Interface
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual void CookAdditionalFiles(const TCHAR* PackageFilename, const ITargetPlatform* TargetPlatform) override;
#endif // WITH_EDITOR
	//~ End UObject interface

	//~ Begin IFontFaceInterface Interface
#if WITH_EDITORONLY_DATA
	virtual void InitializeFromBulkData(const FString& InFilename, const EFontHinting InHinting, const void* InBulkDataPtr, const int32 InBulkDataSizeBytes) override;
#endif // WITH_EDITORONLY_DATA
	virtual const FString& GetFontFilename() const override;
	virtual EFontHinting GetHinting() const override;
	virtual EFontLoadingPolicy GetLoadingPolicy() const override;
#if WITH_EDITORONLY_DATA
	virtual const TArray<uint8>& GetFontFaceData() const override;
#endif // WITH_EDITORONLY_DATA
	virtual FString GetCookedFilename() const override;
	//~ End IFontFaceInterface interface

	/** The filename of the font face we were created from. This may not always exist on disk, as we may have previously loaded and cached the font data inside this asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=FontFace)
	FString SourceFilename;

	/** The hinting algorithm to use with the font face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FontFace)
	EFontHinting Hinting;

	/** Enum controlling how this font face should be loaded at runtime. See the enum for more explanations of the options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=FontFace)
	EFontLoadingPolicy LoadingPolicy;

#if WITH_EDITORONLY_DATA
	/** The data associated with the font face. This should always be filled in providing the source filename is valid. */
	UPROPERTY()
	TArray<uint8> FontFaceData;
#endif // WITH_EDITORONLY_DATA
};
