// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshUtilities.h"

#include "Async/Future.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeManager.h"
#include "InterchangeProjectSettings.h"
#include "InterchangeSourceData.h"
#include "Logging/LogMacros.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "LODUtilities.h"

TFuture<bool> UInterchangeMeshUtilities::ImportCustomLodAsync(UObject* MeshObject, const int32 LodIndex)
{
	TSharedPtr<TPromise<bool>> Promise = MakeShared<TPromise<bool>>();
	if (!MeshObject)
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportCustomLod parameter MeshObject cannot be null."));
		Promise->SetValue(false);
		return Promise->GetFuture();
	}
	if (!IsInGameThread())
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportCustomLod Cannot ask user a file path outside of the game thread."));
		Promise->SetValue(false);
		return Promise->GetFuture();
	}
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	//Ask the user for a file path
	const UInterchangeProjectSettings* InterchangeProjectSettings = GetDefault<UInterchangeProjectSettings>();
	UInterchangeFilePickerBase* FilePicker = nullptr;

	//In runtime we do not have any pipeline configurator
#if WITH_EDITORONLY_DATA
	TSoftClassPtr <UInterchangeFilePickerBase> FilePickerClass = InterchangeProjectSettings->FilePickerClass;
	if (FilePickerClass.IsValid())
	{
		UClass* FilePickerClassLoaded = FilePickerClass.LoadSynchronous();
		if (FilePickerClassLoaded)
		{
			FilePicker = NewObject<UInterchangeFilePickerBase>(GetTransientPackage(), FilePickerClassLoaded, NAME_None, RF_NoFlags);
		}
	}
#endif
	if(FilePicker)
	{
		FInterchangeFilePickerParameters Parameters;
		Parameters.bAllowMultipleFiles = false;
		Parameters.Title = FText::Format(NSLOCTEXT("Interchange", "ImportCustomLodAsync_FilePickerTitle", "Choose a file to import a custom LOD for LOD{0}"), FText::AsNumber(LodIndex));
		TArray<FString> Filenames;
		if (FilePicker->ScriptedFilePickerForTranslatorAssetType(EInterchangeTranslatorAssetType::Meshes, Parameters, Filenames))
		{
			//We set bAllowMultipleFile to false, we should have only one result
			if (ensure(Filenames.Num() == 1))
			{
				const UInterchangeSourceData* SourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData(Filenames[0]);
				return InternalImportCustomLodAsync(Promise, MeshObject, LodIndex, SourceData);
			}
		}
	}

	Promise->SetValue(false);
	return Promise->GetFuture();
}

TFuture<bool> UInterchangeMeshUtilities::ImportCustomLodAsync(UObject* MeshObject, const int32 LodIndex, const UInterchangeSourceData* SourceData)
{
	TSharedPtr<TPromise<bool>> Promise = MakeShared<TPromise<bool>>();
	
	return InternalImportCustomLodAsync(Promise, MeshObject, LodIndex, SourceData);
}

TFuture<bool> UInterchangeMeshUtilities::InternalImportCustomLodAsync(TSharedPtr<TPromise<bool>> Promise, UObject* MeshObject, const int32 LodIndex, const UInterchangeSourceData* SourceData)
{
#if WITH_EDITOR
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	UInterchangeAssetImportData* InterchangeAssetImportData = nullptr;
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshObject);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshObject);
	EInterchangeReimportType ImportType = EInterchangeReimportType::AssetCustomLODImport;
	if (SkeletalMesh)
	{
		InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SkeletalMesh->GetAssetImportData());
		if (SkeletalMesh->GetLODNum() > LodIndex)
		{
			ImportType = EInterchangeReimportType::AssetCustomLODReimport;
		}
	}
	else if (StaticMesh)
	{
		InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(StaticMesh->GetAssetImportData());
		if (StaticMesh->GetNumSourceModels() > LodIndex)
		{
			ImportType = EInterchangeReimportType::AssetCustomLODReimport;
		}
	}
	else
	{
		//We support Import custom LOD only for skeletalmesh and staticmesh
		Promise->SetValue(false);
		return Promise->GetFuture();
	}

	FImportAssetParameters ImportAssetParameters;
	ImportAssetParameters.bIsAutomated = true;
	for (TObjectPtr<UInterchangePipelineBase> SelectedPipeline : InterchangeAssetImportData->Pipelines)
	{
		UInterchangePipelineBase* GeneratedPipeline = Cast<UInterchangePipelineBase>(StaticDuplicateObject(SelectedPipeline, GetTransientPackage()));
		GeneratedPipeline->AdjustSettingsForReimportType(ImportType, nullptr);
		ImportAssetParameters.OverridePipelines.Add(GeneratedPipeline);
	}
	FString ImportAssetPath = TEXT("/Engine/TempEditor/Interchange/") + FGuid::NewGuid().ToString(EGuidFormats::Base36Encoded);
	UE::Interchange::FAssetImportResultRef AssetImportResult = InterchangeManager.ImportAssetAsync(ImportAssetPath, SourceData, ImportAssetParameters);
	FString SourceDataFilename = SourceData->GetFilename();
	if (SkeletalMesh)
	{
		AssetImportResult->OnDone([Promise, SkeletalMesh, LodIndex, SourceDataFilename](UE::Interchange::FImportResult& ImportResult)
			{
				USkeletalMesh* SourceSkeletalMesh = Cast< USkeletalMesh >(ImportResult.GetFirstAssetOfClass(USkeletalMesh::StaticClass()));

				if(SourceSkeletalMesh)
				{

					Promise->SetValue(FLODUtilities::SetCustomLOD(SkeletalMesh, SourceSkeletalMesh, LodIndex, SourceDataFilename));
					SourceSkeletalMesh->ClearFlags(RF_Standalone);
					SourceSkeletalMesh->ClearInternalFlags(EInternalObjectFlags::Async);
				}
				else
				{
					Promise->SetValue(false);
				}
				
			});
	}
	else if (StaticMesh)
	{
		AssetImportResult->OnDone([Promise, StaticMesh, LodIndex, SourceDataFilename](UE::Interchange::FImportResult& ImportResult)
			{
				UStaticMesh* SourceStaticMesh = Cast< UStaticMesh >(ImportResult.GetFirstAssetOfClass(UStaticMesh::StaticClass()));
				if(SourceStaticMesh)
				{
					Promise->SetValue(StaticMesh->SetCustomLOD(SourceStaticMesh, LodIndex, SourceDataFilename));
					SourceStaticMesh->ClearFlags(RF_Standalone);
					SourceStaticMesh->ClearInternalFlags(EInternalObjectFlags::Async);
				}
				else
				{
					Promise->SetValue(false);
				}
			});
	}

	return Promise->GetFuture();
#else
	Promise->SetValue(false);
	return Promise->GetFuture();
#endif
}