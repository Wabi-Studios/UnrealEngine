// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/PCGTestsCommon.h"

#include "PCGParamData.h"
#include "PCGSettings.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGSurfaceData.h"
#include "Data/PCGVolumeData.h"
#include "Data/PCGPrimitiveData.h"

#include "GameFramework/Actor.h"

namespace PCGTestsCommon
{
	AActor* CreateTemporaryActor()
	{
		return NewObject<AActor>();
	}

	UPCGPointData* CreateEmptyPointData()
	{
		return NewObject<UPCGPointData>();
	}

	UPCGPointData* CreatePointData()
	{
		UPCGPointData* SinglePointData = CreateEmptyPointData();

		check(SinglePointData);

		SinglePointData->GetMutablePoints().Emplace();

		return SinglePointData;
	}

	UPCGPointData* CreatePointData(const FVector& InLocation)
	{
		UPCGPointData* SinglePointData = CreatePointData();

		check(SinglePointData);
		check(SinglePointData->GetMutablePoints().Num() == 1);

		SinglePointData->GetMutablePoints()[0].Transform.SetLocation(InLocation);

		return SinglePointData;
	}

	UPCGPolyLineData* CreatePolyLineData()
	{
		// TODO: spline, landscape spline
		return nullptr;
	}

	UPCGSurfaceData* CreateSurfaceData()
	{
		// TODO: either landscape, texture, render target
		return nullptr;
	}

	UPCGVolumeData* CreateVolumeData(const FBox& InBounds)
	{
		UPCGVolumeData* VolumeData = NewObject<UPCGVolumeData>();
		VolumeData->Initialize(InBounds, nullptr);
		return VolumeData;
	}

	UPCGPrimitiveData* CreatePrimitiveData()
	{
		// TODO: need UPrimitiveComponent on an actor
		return nullptr;
	}

	UPCGParamData* CreateEmptyParamData()
	{
		return NewObject<UPCGParamData>();
	}

	TArray<FPCGDataCollection> GenerateAllowedData(const FPCGPinProperties& PinProperties)
	{
		TArray<FPCGDataCollection> Data;

		TMap<EPCGDataType, TFunction<UPCGData*(void)>> TypeToDataFn;
		TypeToDataFn.Add(EPCGDataType::Point, []() { return PCGTestsCommon::CreatePointData(); });
		TypeToDataFn.Add(EPCGDataType::PolyLine, []() { return PCGTestsCommon::CreatePolyLineData(); });
		TypeToDataFn.Add(EPCGDataType::Surface, []() { return PCGTestsCommon::CreateSurfaceData(); });
		TypeToDataFn.Add(EPCGDataType::Volume, []() { return PCGTestsCommon::CreateVolumeData(); });
		TypeToDataFn.Add(EPCGDataType::Primitive, []() { return PCGTestsCommon::CreatePrimitiveData(); });
		TypeToDataFn.Add(EPCGDataType::Param, []() { return PCGTestsCommon::CreateEmptyParamData(); });

		// Create empty data
		Data.Emplace();

		// Create single data & data pairs
		for (const auto& TypeToData : TypeToDataFn)
		{
			if (!(TypeToData.Key & PinProperties.AllowedTypes))
			{
				continue;
			}

			FPCGDataCollection& SingleCollection = Data.Emplace_GetRef();
			FPCGTaggedData& SingleTaggedData = SingleCollection.TaggedData.Emplace_GetRef();
			SingleTaggedData.Data = TypeToData.Value();
			SingleTaggedData.Pin = PinProperties.Label;

			if (!PinProperties.bAllowMultipleConnections)
			{
				continue;
			}

			for (const auto& SecondaryTypeToData : TypeToDataFn)
			{
				if (!(SecondaryTypeToData.Key & PinProperties.AllowedTypes))
				{
					continue;
				}

				FPCGDataCollection& MultiCollection = Data.Emplace_GetRef();
				FPCGTaggedData& FirstTaggedData = MultiCollection.TaggedData.Emplace_GetRef();
				FirstTaggedData.Data = TypeToData.Value();
				FirstTaggedData.Pin = PinProperties.Label;

				FPCGTaggedData& SecondTaggedData = MultiCollection.TaggedData.Emplace_GetRef();
				SecondTaggedData.Data = SecondaryTypeToData.Value();
				SecondTaggedData.Pin = PinProperties.Label;
			}
		}

		return Data;
	}

	bool PointsAreIdentical(const FPCGPoint& FirstPoint, const FPCGPoint& SecondPoint)
	{
		// TODO: should do a full point comparison, not only on a positional basis
		return (FirstPoint.Transform.GetLocation() - SecondPoint.Transform.GetLocation()).SquaredLength() < KINDA_SMALL_NUMBER;
	}
}

bool FPCGTestBaseClass::SmokeTestAnyValidInput(UPCGSettings* InSettings, TFunction<bool(const FPCGDataCollection&, const FPCGDataCollection&)> ValidationFn)
{
	TestTrue("Valid settings", InSettings != nullptr);

	if (!InSettings)
	{
		return false;
	}

	FPCGElementPtr Element = InSettings->GetElement();

	TestTrue("Valid element", Element != nullptr);

	if (!Element)
	{
		return false;
	}

	TArray<FPCGPinProperties> InputProperties = InSettings->InputPinProperties();
	// For each pin: take nothing, take 1 of any supported type, take 2 of any supported types (if enabled)
	TArray<TArray<FPCGDataCollection>> InputsPerProperties;
	TArray<uint32> InputIndices;

	if (!InputProperties.IsEmpty())
	{
		for (const FPCGPinProperties& InputProperty : InputProperties)
		{
			InputsPerProperties.Add(PCGTestsCommon::GenerateAllowedData(InputProperty));
			InputIndices.Add(0);
		}
	}
	else
	{
		TArray<FPCGDataCollection>& EmptyCollection = InputsPerProperties.Emplace_GetRef();
		EmptyCollection.Emplace();
		InputIndices.Add(0);
	}

	check(InputIndices.Num() == InputsPerProperties.Num());

	while (1)
	{
		// Prepare input
		FPCGDataCollection InputData;
		for (int32 PinIndex = 0; PinIndex < InputIndices.Num(); ++PinIndex)
		{
			InputData.TaggedData.Append(InputsPerProperties[PinIndex][InputIndices[PinIndex]].TaggedData);
		}

		// Perform execution
		FPCGContext* Context = Element->Initialize(InputData, nullptr, nullptr);
		Context->NumAvailableTasks = 1;
		Element->Execute(Context);

		if (ValidationFn)
		{
			TestTrue("Validation", ValidationFn(Context->InputData, Context->OutputData));
		}

		delete Context;

		// Bump indices
		int BumpIndex = 0;
		while (BumpIndex < InputIndices.Num())
		{
			if (InputIndices[BumpIndex] == InputsPerProperties[BumpIndex].Num() - 1)
			{
				InputIndices[BumpIndex] = 0;
				++BumpIndex;
			}
			else
			{
				++InputIndices[BumpIndex];
				break;
			}
		}

		if (BumpIndex == InputIndices.Num())
		{
			break;
		}
	}

	return true;
}
