// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/DMXLibraryFromMVRFactory.h"

#include "DMXEditorLog.h"
#include "DMXEditorModule.h"
#include "DMXEditorUtils.h"
#include "Factories/DMXGDTFFactory.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXGDTFAssetImportData.h"
#include "Library/DMXImportGDTF.h"
#include "Library/DMXLibrary.h"
#include "MVR/DMXMVRAssetImportData.h"
#include "MVR/DMXMVRGeneralSceneDescription.h"
#include "MVR/DMXMVRUnzip.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "XmlFile.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"


#define LOCTEXT_NAMESPACE "DMXLibraryFromMVRFactory"

const FName UDMXLibraryFromMVRFactory::MVRFileExtension = "MVR";
const FName UDMXLibraryFromMVRFactory::GDTFFileExtension = "GDTF";

UDMXLibraryFromMVRFactory::UDMXLibraryFromMVRFactory()
{
	bEditorImport = true;
	bEditAfterNew = true;
	SupportedClass = UDMXLibrary::StaticClass();

	Formats.Add(TEXT("mvr;My Virtual Rig"));
}

UObject* UDMXLibraryFromMVRFactory::FactoryCreateFile(UClass* InClass, UObject* Parent, FName InName, EObjectFlags Flags, const FString& InFilename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	CurrentFilename = InFilename;

	UDMXLibrary* NewDMXLibrary = CreateDMXLibraryAsset(Parent, Flags, InFilename);
	if (!NewDMXLibrary)
	{
		UE_LOG(LogDMXEditor, Error, TEXT("Failed to create DMX Library for MVR '%s'."), *InFilename);
		return nullptr;
	}

	const TSharedPtr<FDMXMVRUnzip> MVRUnzip = FDMXMVRUnzip::CreateFromFile(InFilename);
	if (!MVRUnzip.IsValid())
	{
		UE_LOG(LogDMXEditor, Error, TEXT("Cannot read MVR '%s'. File is not a valid MVR."), *InFilename);
		return nullptr;
	}

	TArray64<uint8> XMLData;
	MVRUnzip->GetFileContent(TEXT("GeneralSceneDescription.xml"), XMLData);

	// MVR implicitly adpots UTF-8 encoding of Xml Files by adopting the GDT standard (DIN-15800).
	// Content is NOT null-terminated; we need to specify lengths here.
	const FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(XMLData.GetData()), XMLData.Num());

	const TSharedRef<FXmlFile> GeneralSceneDescriptionXml = MakeShared<FXmlFile>(FString(TCHARData.Length(), TCHARData.Get()), EConstructMethod::ConstructFromBuffer);

	const FName GeneralSceneDescriptionName = FName(GetName() + TEXT("_MVRGeneralSceneDescription"));
	UDMXMVRGeneralSceneDescription* GeneralSceneDescription = UDMXMVRGeneralSceneDescription::CreateFromXmlFile(GeneralSceneDescriptionXml, NewDMXLibrary, GeneralSceneDescriptionName);
	if (!GeneralSceneDescription)
	{
		return nullptr;
	}
	UDMXMVRAssetImportData* MVRAssetImportData = GeneralSceneDescription->GetMVRAssetImportData();
	MVRAssetImportData->SetSourceFile(InFilename);

	TArray<UDMXImportGDTF*> GDTFs = CreateGDTFAssets(Parent, Flags, MVRUnzip.ToSharedRef(), *GeneralSceneDescription);
	InitDMXLibrary(NewDMXLibrary, GDTFs, GeneralSceneDescription);

	return NewDMXLibrary;
}

bool UDMXLibraryFromMVRFactory::FactoryCanImport(const FString& Filename)
{
	const FString TargetExtension = FPaths::GetExtension(Filename);

	if (TargetExtension.Equals(UDMXLibraryFromMVRFactory::MVRFileExtension.ToString(), ESearchCase::IgnoreCase))
	{
		return true;
	}

	return false;
}

UDMXLibrary* UDMXLibraryFromMVRFactory::CreateDMXLibraryAsset(UObject* Parent, EObjectFlags Flags, const FString& InFilename)
{
	constexpr bool bRemovePathFromDesiredName = true;
	const FString BaseFileName = FPaths::GetBaseFilename(InFilename, bRemovePathFromDesiredName);
	const FString PackageName = Parent->GetName();
	FString AssetName = ObjectTools::SanitizeObjectName(BaseFileName);
	if (PackageName.Contains(AssetName))
	{
		AssetName = AssetName + TEXT("_DMXLibrary");
	}

	FString DMXLibraryAssetName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(PackageName / AssetName, TEXT(""), DMXLibraryPackageName, DMXLibraryAssetName);

	UPackage* Package = CreatePackage(*DMXLibraryPackageName);
	check(Package);
	Package->FullyLoad();

	UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>();
	ImportSubsystem->BroadcastAssetPreImport(this, UDMXLibrary::StaticClass(), Parent, *DMXLibraryAssetName, *MVRFileExtension.ToString());

	UDMXLibrary* NewDMXLibrary = NewObject<UDMXLibrary>(Package, *DMXLibraryAssetName, Flags | RF_Public);
	if (!NewDMXLibrary)
	{
		return nullptr;
	}

	ImportSubsystem->BroadcastAssetPostImport(this, NewDMXLibrary);
	
	return NewDMXLibrary;
}

TArray<UDMXImportGDTF*> UDMXLibraryFromMVRFactory::CreateGDTFAssets(UObject* Parent, EObjectFlags Flags, const TSharedRef<FDMXMVRUnzip>& MVRUnzip, const UDMXMVRGeneralSceneDescription& GeneralSceneDescription)
{
	const FString Path = FPaths::GetPath(DMXLibraryPackageName) + TEXT("/GDTFs");

	TArray<FAssetData> ExistingGDTFAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssetsByPath(FName(Path), ExistingGDTFAssets);

	TArray<UDMXImportGDTF*> ExistingGDTFs;
	for (const FAssetData& AssetData : ExistingGDTFAssets)
	{
		if (UDMXImportGDTF* GDTF = Cast<UDMXImportGDTF>(AssetData.GetAsset()))
		{
			ExistingGDTFs.AddUnique(GDTF);
		}
	}

	UDMXGDTFFactory* GDTFFactory = NewObject<UDMXGDTFFactory>();
	TArray<UDMXImportGDTF*> ImportedGDTFs;
	TArray<FString> ImportedGDTFNames;
	if (ExistingGDTFs.Num() > 0)
	{
		const FText MessageText = LOCTEXT("MVRImportReimportsGDTFDialog", "MVR contains existing GDTFs. Do you want to reimport the existing GDTF assets?");
		if (FMessageDialog::Open(EAppMsgType::YesNo, MessageText) == EAppReturnType::Yes)
		{
			for (UDMXImportGDTF* GDTFAsset : ExistingGDTFs)
			{
				if (GDTFFactory->Reimport(GDTFAsset) == EReimportResult::Succeeded)
				{
					ImportedGDTFs.Add(GDTFAsset);

					if (UDMXGDTFAssetImportData* GDTFAssetImportData = GDTFAsset->GetGDTFAssetImportData())
					{
						const FString SourceFilename = FPaths::GetCleanFilename(GDTFAssetImportData->GetSourceFilePathAndName());
						ImportedGDTFNames.Add(SourceFilename);
					}
				}
			}
		}
	}

	// Import GDTF Assets that aren't yet imported in the reimport procedure
	for (const FDMXMVRFixture& MVRFixture : GeneralSceneDescription.GetMVRFixtures())
	{
		// Don't import the same GDTF twice
		if (!ImportedGDTFNames.Contains(MVRFixture.GDTFSpec))
		{
			const FDMXMVRUnzip::FDMXScopedUnzipToTempFile ScopedUnzipGDTF(MVRUnzip, MVRFixture.GDTFSpec);
			if (!ScopedUnzipGDTF.TempFilePathAndName.IsEmpty())
			{
				constexpr bool bRemovePathFromDesiredName = true;
				const FString BaseFileName = FPaths::GetBaseFilename(ScopedUnzipGDTF.TempFilePathAndName, bRemovePathFromDesiredName);
				FString AssetName = ObjectTools::SanitizeObjectName(BaseFileName);

				FString GDTFPackageName;
				FString GDTFAssetName;
				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
				AssetToolsModule.Get().CreateUniqueAssetName(Path / AssetName, TEXT(""), GDTFPackageName, GDTFAssetName);

				UPackage* Package = CreatePackage(*GDTFPackageName);
				check(Package);
				Package->FullyLoad();

				bool bCanceled;
				UObject* NewGDTFObject = GDTFFactory->FactoryCreateFile(UDMXImportGDTF::StaticClass(), Package, FName(*MVRFixture.GDTFSpec), Flags | RF_Public, ScopedUnzipGDTF.TempFilePathAndName, nullptr, GWarn, bCanceled);
				
				if (UDMXImportGDTF* NewGDTF = Cast<UDMXImportGDTF>(NewGDTFObject))
				{
					ImportedGDTFs.Add(NewGDTF);
					ImportedGDTFNames.Add(MVRFixture.GDTFSpec);

					FAssetRegistryModule::AssetCreated(NewGDTF);
					Package->MarkPackageDirty();
				}
			}
		}
	}

	return ImportedGDTFs;
}

void UDMXLibraryFromMVRFactory::InitDMXLibrary(UDMXLibrary* DMXLibrary, const TArray<UDMXImportGDTF*>& GDTFAssets, UDMXMVRGeneralSceneDescription* GeneralSceneDescription) const
{
	if (!ensureAlwaysMsgf(DMXLibrary, TEXT("Trying to initialize a DMX Library from MVR, but the DMX Library is not valid.")))
	{
		return;
	}
	if (!ensureAlwaysMsgf(GeneralSceneDescription, TEXT("Trying to initialize a DMX Library '%s' from MVR, but the DMX Library is not valid."), *DMXLibrary->GetName()))
	{
		return;
	}
	DMXLibrary->SetMVRGeneralSceneDescription(GeneralSceneDescription);

	// Create a Fixture Type for each GDTF
	TMap<FString, UDMXEntityFixtureType*> GDTFSpecToFixtureTypeMap;
	TMap<UDMXEntityFixtureType*, FLinearColor> FixtureTypeToColorMap;
	for (UDMXImportGDTF* GDTF : GDTFAssets)
	{
		UDMXGDTFAssetImportData* GDTFAssetImportData = GDTF->GetGDTFAssetImportData();
		if (!GDTFAssetImportData)
		{
			continue;
		}

		const FString GDTFSourceFilename = GDTFAssetImportData->GetSourceFilePathAndName();
		const FString GDTFFilename = FPaths::GetCleanFilename(GDTFSourceFilename);
		FDMXEntityFixtureTypeConstructionParams FixtureTypeConstructionParams;
		FixtureTypeConstructionParams.DMXCategory = FDMXFixtureCategory(FDMXFixtureCategory::GetFirstValue());
		FixtureTypeConstructionParams.ParentDMXLibrary = DMXLibrary;

		UDMXEntityFixtureType* FixtureType = UDMXEntityFixtureType::CreateFixtureTypeInLibrary(FixtureTypeConstructionParams, FPaths::GetBaseFilename(GDTFFilename));
		FixtureType->SetGDTF(GDTF);

		GDTFSpecToFixtureTypeMap.Add(GDTFFilename, FixtureType);
	}

	// Create Fixture Patches for the MVR Fixtures
	for (const FDMXMVRFixture& MVRFixture : GeneralSceneDescription->GetMVRFixtures())
	{
		if (!GDTFSpecToFixtureTypeMap.Contains(MVRFixture.GDTFSpec))
		{
			continue;
		}

		UDMXEntityFixtureType* FixtureType = GDTFSpecToFixtureTypeMap[MVRFixture.GDTFSpec];
		UDMXEntityFixturePatch* const* FixturePatchPtr = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>().FindByPredicate([MVRFixture, FixtureType](UDMXEntityFixturePatch* FixturePatch)
			{
				const FDMXFixtureMode* ActiveModePtr = FixturePatch->GetActiveMode();
				return
					FixturePatch->GetFixtureType() == FixtureType &&
					FixturePatch->GetUniverseID() == MVRFixture.GetUniverseID() &&
					FixturePatch->GetStartingChannel() == MVRFixture.GetStartingChannel() &&
					ActiveModePtr && ActiveModePtr->ModeName == MVRFixture.GDTFMode;
			});

		if (!FixtureTypeToColorMap.Contains(FixtureType))
		{
			FLinearColor FixtureTypeColor = FLinearColor::MakeRandomColor();

			// Avoid dominant red values for a bit more of a professional feel
			if (FixtureTypeColor.R > 0.75f)
			{
				FixtureTypeColor.R = FMath::Abs(FixtureTypeColor.R - 1.0f);
			}

			FixtureTypeToColorMap.Add(FixtureType, FixtureTypeColor);
		}

		if (!FixturePatchPtr)
		{
			int32 ActiveModeIndex = FixtureType->Modes.IndexOfByPredicate([MVRFixture](const FDMXFixtureMode& Mode)
				{
					return Mode.ModeName == MVRFixture.GDTFMode;
				});

			if (ActiveModeIndex != INDEX_NONE)
			{
				FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
				FixturePatchConstructionParams.ActiveMode = ActiveModeIndex;
				FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
				FixturePatchConstructionParams.UniverseID = MVRFixture.GetUniverseID();
				FixturePatchConstructionParams.StartingAddress = MVRFixture.GetStartingChannel();
				FixturePatchConstructionParams.MVRFixtureUUID = MVRFixture.UUID;

				UDMXEntityFixturePatch* FixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, MVRFixture.Name);
				FixturePatch->EditorColor = FixtureTypeToColorMap.FindChecked(FixtureType);
				FixturePatchPtr = &FixturePatch;
			}
			else
			{
				UE_LOG(LogDMXEditor, Warning, TEXT("Skipped creating a Fixture Patch for '%s', as no valid Mode could be imported when importing %s."), *MVRFixture.Name, *MVRFixture.GDTFSpec)
			}
		}
	}

	DMXLibrary->UpdateGeneralSceneDescription();
}

#undef LOCTEXT_NAMESPACE
