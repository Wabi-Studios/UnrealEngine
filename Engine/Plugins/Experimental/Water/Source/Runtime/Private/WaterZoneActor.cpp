// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterZoneActor.h"
#include "WaterModule.h"
#include "WaterSubsystem.h"
#include "WaterMeshComponent.h"
#include "WaterBodyActor.h"
#include "WaterInfoRendering.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "WaterUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "WaterBodyOceanComponent.h"
#include "RenderCaptureInterface.h"

#if	WITH_EDITOR
#include "Algo/Transform.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "WaterIconHelper.h"
#include "Components/BoxComponent.h"
#endif // WITH_EDITOR

static int32 ForceUpdateWaterInfoNextFrames = 0;
static FAutoConsoleVariableRef CVarForceUpdateWaterInfoNextFrames(
	TEXT("r.Water.WaterInfo.ForceUpdateWaterInfoNextFrames"),
	ForceUpdateWaterInfoNextFrames,
	TEXT("Force the water info texture to regenerate on the next N frames. A negative value will force update every frame."));


AWaterZone::AWaterZone(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, RenderTargetResolution(512, 512)
{
	WaterMesh = CreateDefaultSubobject<UWaterMeshComponent>(TEXT("WaterMesh"));
	SetRootComponent(WaterMesh);
	ZoneExtent = FVector2D(51200.f, 51200.f);
	
#if	WITH_EDITOR
	// Setup bounds component
	{
		BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
		BoundsComponent->SetCollisionObjectType(ECC_WorldStatic);
		BoundsComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoundsComponent->SetGenerateOverlapEvents(false);
		BoundsComponent->SetupAttachment(WaterMesh);
		// Bounds component extent is half-extent, ZoneExtent is full extent.
		BoundsComponent->SetBoxExtent(FVector(ZoneExtent / 2.f, 8192.f));
	}

	if (GIsEditor && !IsTemplate())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnActorSelectionChanged().AddUObject(this, &AWaterZone::OnActorSelectionChanged);
	}

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterZoneActorSprite"));
#endif // WITH_EDITOR
}

void AWaterZone::SetZoneExtent(FVector2D NewExtent)
{
	ZoneExtent = NewExtent;
	OnExtentChanged();
}

void AWaterZone::SetRenderTargetResolution(FIntPoint NewResolution)
{
	RenderTargetResolution = NewResolution;
	MarkForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
}

void AWaterZone::BeginPlay()
{
	Super::BeginPlay();

	MarkForRebuild(EWaterZoneRebuildFlags::All);
}

void AWaterZone::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	// Water mesh component was made new root component. Make sure it doesn't have a parent
	WaterMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	Super::PostLoadSubobjects(OuterInstanceGraph);
}

void AWaterZone::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const FVector2D ExtentInTiles = WaterMesh->GetExtentInTiles();
	ZoneExtent = FVector2D(ExtentInTiles * WaterMesh->GetTileSize());
	OnExtentChanged();
#endif // WITH_EDITORONLY_DATA
}

void AWaterZone::MarkForRebuild(EWaterZoneRebuildFlags Flags)
{
	if (EnumHasAnyFlags(Flags, EWaterZoneRebuildFlags::UpdateWaterMesh))
	{
		WaterMesh->MarkWaterMeshGridDirty();
	}
	if (EnumHasAnyFlags(Flags, EWaterZoneRebuildFlags::UpdateWaterInfoTexture))
	{
		bNeedsWaterInfoRebuild = true;
	}
}

void AWaterZone::ForEachWaterBodyComponent(TFunctionRef<bool(UWaterBodyComponent*)> Predicate)
{
	FWaterBodyManager::ForEachWaterBodyComponent(GetWorld(), [this, Predicate](UWaterBodyComponent* Component)
		{
			if (Component->GetWaterZone() == this)
			{
				return Predicate(Component);
			}
			return true;
		});
}

void AWaterZone::Update()
{
	if (bNeedsWaterInfoRebuild || (ForceUpdateWaterInfoNextFrames != 0))
	{
		ForceUpdateWaterInfoNextFrames = (ForceUpdateWaterInfoNextFrames < 0) ? ForceUpdateWaterInfoNextFrames : FMath::Max(0, ForceUpdateWaterInfoNextFrames - 1);
		if (UpdateWaterInfoTexture())
		{
			bNeedsWaterInfoRebuild = false;
		}
	}
	
	if (WaterMesh)
	{
		WaterMesh->Update();
	}
}

#if	WITH_EDITOR
void AWaterZone::ForceUpdateWaterInfoTexture()
{
	UpdateWaterInfoTexture();
}

void AWaterZone::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Ensure that the water mesh is rebuilt if it moves
	MarkForRebuild(EWaterZoneRebuildFlags::All);
}

void AWaterZone::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty != nullptr &&
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AWaterZone, ZoneExtent))
	{
		OnExtentChanged();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterZone, BoundsComponent))
	{
		OnBoundsComponentModified();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterZone, RenderTargetResolution))
	{
		MarkForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterZone, bHalfPrecisionTexture))
	{
		MarkForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterZone, VelocityBlurRadius))
	{
		MarkForRebuild(EWaterZoneRebuildFlags::UpdateWaterInfoTexture);
	}
}

void AWaterZone::OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	TArray<AWaterBody*> NewWaterBodiesSelection;
	Algo::TransformIf(NewSelection, NewWaterBodiesSelection, [](UObject* Obj) { return Obj->IsA<AWaterBody>(); }, [](UObject* Obj) { return static_cast<AWaterBody*>(Obj); });
	NewWaterBodiesSelection.Sort();
	TArray<TWeakObjectPtr<AWaterBody>> NewWeakWaterBodiesSelection;
	NewWeakWaterBodiesSelection.Reserve(NewWaterBodiesSelection.Num());
	Algo::Transform(NewWaterBodiesSelection, NewWeakWaterBodiesSelection, [](AWaterBody* Body) { return TWeakObjectPtr<AWaterBody>(Body); });

	// Ensure that the water mesh is rebuilt if water body selection changed
	if (SelectedWaterBodies != NewWeakWaterBodiesSelection)
	{
		SelectedWaterBodies = NewWeakWaterBodiesSelection;
		MarkForRebuild(EWaterZoneRebuildFlags::All);
	}
}
#endif // WITH_EDITOR

void AWaterZone::OnExtentChanged()
{
	// Compute the new tile extent based on the new bounds
	const float MeshTileSize = WaterMesh->GetTileSize();

	int32 NewExtentInTilesX = FMath::FloorToInt(ZoneExtent.X / MeshTileSize);
	int32 NewExtentInTilesY = FMath::FloorToInt(ZoneExtent.Y / MeshTileSize);
	
	// We must ensure that the zone is always at least 1x1
	NewExtentInTilesX = FMath::Max(1, NewExtentInTilesX);
	NewExtentInTilesY = FMath::Max(1, NewExtentInTilesY);

	WaterMesh->SetExtentInTiles(FIntPoint(NewExtentInTilesX, NewExtentInTilesY));

#if WITH_EDITOR
	// Bounds component extent is half-extent, ZoneExtent is full extent.
	BoundsComponent->SetBoxExtent(FVector(ZoneExtent / 2.f, 8192.f));
#endif // WITH_EDITOR

	MarkForRebuild(EWaterZoneRebuildFlags::All);
}


#if WITH_EDITOR
void AWaterZone::OnBoundsComponentModified()
{
	const FVector2D NewBounds = FVector2D(BoundsComponent->GetUnscaledBoxExtent());

	SetZoneExtent(NewBounds);
}
#endif // WITH_EDITOR

bool AWaterZone::UpdateWaterInfoTexture()
{
	if (UWorld* World = GetWorld(); World && FApp::CanEverRender())
	{
		float WaterZMin(TNumericLimits<float>::Max());
		float WaterZMax(TNumericLimits<float>::Lowest());
	
		bool bHasIncompleteShaderMaps = false;
		// #todo_water [roey]: we should try caching this list to avoid potentially iterating over a lot of water bodies which may not belong to this zone specifically.
		// For now whenever we update the water info texture we will collect all water bodies within the zone and pass those to the renderer each time this function is called.
		TArray<UWaterBodyComponent*> WaterBodies;
		ForEachWaterBodyComponent([World, &WaterBodies, &WaterZMax, &WaterZMin, &bHasIncompleteShaderMaps](UWaterBodyComponent* Component)
		{
			if (UMaterialInterface* WaterInfoMaterial = Component->GetWaterInfoMaterialInstance())
			{
				if (FMaterialResource* MaterialResource = WaterInfoMaterial->GetMaterialResource(World->Scene->GetFeatureLevel()))
				{
					if (!MaterialResource->IsGameThreadShaderMapComplete())
					{
						MaterialResource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::ForceLocal);
						bHasIncompleteShaderMaps = true;
						return true;
					}
				}
			}

			WaterBodies.Add(Component);
			const FBox WaterBodyBounds = Component->CalcBounds(Component->GetComponentToWorld()).GetBox();
			WaterZMax = FMath::Max(WaterZMax, WaterBodyBounds.Max.Z);
			WaterZMin = FMath::Min(WaterZMin, WaterBodyBounds.Min.Z);
			return true;
		});

		if (bHasIncompleteShaderMaps)
		{
			return false;
		}

		// If we don't have any water bodies we don't need to do anything.
		if (WaterBodies.Num() == 0)
		{
			return true;
		}

		WaterHeightExtents = FVector2f(WaterZMin, WaterZMax);

		// Only compute the ground min since we can use the water max z as the ground max z for more precision.
		GroundZMin = TNumericLimits<float>::Max();
		float GroundZMax = TNumericLimits<float>::Lowest();

		int32 LandscapeLODOverride = 0;
		TArray<TWeakObjectPtr<AActor>> GroundActors;
		for (ALandscapeProxy* LandscapeProxy : TActorRange<ALandscapeProxy>(World))
		{
			const FBox LandscapeBox = LandscapeProxy->GetComponentsBoundingBox();
			GroundZMin = FMath::Min(GroundZMin, LandscapeBox.Min.Z);
			GroundZMax = FMath::Max(GroundZMax, LandscapeBox.Max.Z);
			GroundActors.Add(LandscapeProxy);

			// Target mip 64x64 for a capture
			int32 LOD64x64 = FMath::CeilLogTwo(LandscapeProxy->SubsectionSizeQuads + 1) - 6;
			LandscapeLODOverride = FMath::Max(LandscapeLODOverride, LOD64x64);
		}

		const ETextureRenderTargetFormat Format = bHalfPrecisionTexture ? ETextureRenderTargetFormat::RTF_RGBA16f : RTF_RGBA32f;
		WaterInfoTexture = FWaterUtils::GetOrCreateTransientRenderTarget2D(WaterInfoTexture, TEXT("WaterInfoTexture"), RenderTargetResolution, Format);

		UE::WaterInfo::FRenderingContext Context;
		Context.ZoneToRender = this;
		Context.WaterBodies = WaterBodies;
		Context.GroundActors = MoveTemp(GroundActors);
		Context.CaptureZ = FMath::Max(WaterZMax, GroundZMax) + CaptureZOffset;
		Context.TextureRenderTarget = WaterInfoTexture;
		Context.LandscapeLODOverride = LandscapeLODOverride;

		if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(World))
		{
			WaterSubsystem->MarkWaterInfoTextureForRebuild(Context);
		}

		for (UWaterBodyComponent* Component : WaterBodies)
		{
			Component->UpdateMaterialInstances();
		}

		UE_LOG(LogWater, Verbose, TEXT("Queued Water Info texture update"));
	}

	return true;
}
