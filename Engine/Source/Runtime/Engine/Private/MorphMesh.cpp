// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MorphMesh.cpp: Unreal morph target mesh and blending implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "EngineUtils.h"
#include "Animation/MorphTarget.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/LowLevelMemTracker.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "UObject/EditorObjectVersion.h"

//////////////////////////////////////////////////////////////////////////

UMorphTarget::UMorphTarget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


void UMorphTarget::Serialize( FArchive& Ar )
{
	LLM_SCOPE(ELLMTag::Animation);
	
	Super::Serialize( Ar );
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	FStripDataFlags StripFlags( Ar );
	if( !StripFlags.IsDataStrippedForServer() )
	{
		Ar << MorphLODModels;
	}
}


void UMorphTarget::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	for (const auto& LODModel : MorphLODModels)
	{
		LODModel.GetResourceSizeEx(CumulativeResourceSize);
	}
}


void UMorphTarget::PostLoad()
{
	Super::PostLoad();
	
#if WITH_EDITOR
	if (GetLinkerCustomVersion(FEditorObjectVersion::GUID) < FEditorObjectVersion::AddedMorphTargetSectionIndices &&
		BaseSkelMesh)
	{
		const int32 MaxLOD = FMath::Min(BaseSkelMesh->GetImportedModel()->LODModels.Num(), MorphLODModels.Num());
		for (int32 LODIndex = 0; LODIndex < MaxLOD; ++LODIndex)
		{
			FMorphTargetLODModel& MorphLODModel = MorphLODModels[LODIndex];
			MorphLODModel.SectionIndices.Empty();
			const FSkeletalMeshLODModel& LODModel = BaseSkelMesh->GetImportedModel()->LODModels[LODIndex];
			TArray<int32> BaseIndexes;
			TArray<int32> LastIndexes;
			for (int32 SectionIdx = 0; SectionIdx < LODModel.Sections.Num(); ++SectionIdx)
			{
				const int32 BaseVertexBufferIndex = LODModel.Sections[SectionIdx].GetVertexBufferIndex();
				BaseIndexes.Add(BaseVertexBufferIndex);
				LastIndexes.Add(BaseVertexBufferIndex + LODModel.Sections[SectionIdx].GetNumVertices());
			}
			// brute force
			for (int32 VertIndex = 0; VertIndex < MorphLODModel.Vertices.Num() && MorphLODModel.SectionIndices.Num() < BaseIndexes.Num(); ++VertIndex)
			{
				int32 SourceVertexIdx = MorphLODModel.Vertices[VertIndex].SourceIdx;
				for (int32 SectionIdx = 0; SectionIdx < BaseIndexes.Num(); ++SectionIdx)
				{
					if (!MorphLODModel.SectionIndices.Contains(SectionIdx))
					{
						if (BaseIndexes[SectionIdx] <= SourceVertexIdx && SourceVertexIdx < LastIndexes[SectionIdx])
						{
							MorphLODModel.SectionIndices.AddUnique(SectionIdx);
							break;
						}
					}
				}
			}
		}
	}
#endif //#if WITH_EDITOR
}

void FMorphTargetLODModel::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddUnknownMemoryBytes(Vertices.GetAllocatedSize() + sizeof(int32));
}
