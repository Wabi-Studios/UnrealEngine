// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"

#define MIN_NUM_OBJECTS 5

namespace Chaos
{
template<class OBJECT_ARRAY, class T, int d>
class CHAOS_API TBoundingVolumeHierarchy
{
  public:
	TBoundingVolumeHierarchy(const OBJECT_ARRAY& Objects, const int32 MaxLevels = 12);
	TBoundingVolumeHierarchy(const TBoundingVolumeHierarchy<OBJECT_ARRAY, T, d>& Other) = delete;
	TBoundingVolumeHierarchy(TBoundingVolumeHierarchy<OBJECT_ARRAY, T, d>&& Other)
	    : MObjects(Other.MObjects), MGlobalObjects(MoveTemp(Other.MGlobalObjects)), MWorldSpaceBoxes(MoveTemp(Other.MWorldSpaceBoxes)), MMaxLevels(Other.MMaxLevels), Elements(MoveTemp(Other.Elements))
	{
	}

	TBoundingVolumeHierarchy& operator=(TBoundingVolumeHierarchy<OBJECT_ARRAY, T, d>&& Other)
	{
		MObjects = Other.MObjects;
		MGlobalObjects = MoveTemp(Other.MGlobalObjects);
		MWorldSpaceBoxes = MoveTemp(Other.MWorldSpaceBoxes);
		MMaxLevels = Other.MMaxLevels;
		Elements = MoveTemp(Other.Elements);
		return *this;
	}

	void UpdateHierarchy(const bool AllowMultipleSplitting = false);

	template<class T_INTERSECTION>
	TArray<int32> FindAllIntersections(const T_INTERSECTION& Intersection) const
	{
		if (Elements.Num())
		{
			TArray<int32> IntersectionList = FindAllIntersectionsHelper(Elements[0], Intersection);
			IntersectionList.Append(MGlobalObjects);
			return IntersectionList;
		}
		else
		{
			return MGlobalObjects;
		}
	}
	TArray<int32> FindAllIntersections(const TGeometryParticles<T, d>& InParticles, const int32 i) const;

	const TArray<int32>& GlobalObjects() const
	{
		return MGlobalObjects;
	}

  private:
	struct Node
	{
		Node() {}
		~Node() {}
		TVector<T, d> MMin, MMax;
		int32 MAxis;
		TArray<int32> MObjects;
		TArray<int32> MChildren;
	};

	void PrintTree(FString Prefix, const Node* MyNode) const
	{
		UE_LOG(LogChaos, Verbose, TEXT("%sNode has Box: (%f, %f, %f) to (%f, %f, %f) with %d Children and %d Objects"), *Prefix, MyNode->MMin[0], MyNode->MMin[1], MyNode->MMin[2], MyNode->MMax[0], MyNode->MMax[1], MyNode->MMax[2], MyNode->MChildren.Num(), MyNode->MObjects.Num());
		for (auto& Child : MyNode->MChildren)
		{
			PrintTree(Prefix + " ", &Elements[Child]);
		}
	}

	TArray<int32> FindAllIntersectionsHelper(const Node& MyNode, const TVector<T, d>& Point) const;
	TArray<int32> FindAllIntersectionsHelper(const Node& MyNode, const TBox<T, d>& ObjectBox) const;
	void FindAllIntersectionsHelperRecursive(const Node& MyNode, const TBox<T, d>& ObjectBox, TArray<int32>& AccumulateElements, TSet<int32>& AccumulateSet) const;

	int32 GenerateNextLevel(const TVector<T, d>& GlobalMin, const TVector<T, d>& GlobalMax, const TArray<int32>& Objects, const int32 Axis, const int32 Level, const bool AllowMultipleSplitting);
	int32 GenerateNextLevel(const TVector<T, d>& GlobalMin, const TVector<T, d>& GlobalMax, const TArray<int32>& Objects, const int32 Level);

	OBJECT_ARRAY const* MObjects;
	TArray<int32> MGlobalObjects;
	TArray<TBox<T, d>> MWorldSpaceBoxes;
	int32 MMaxLevels;
	TArray<Node> Elements;
	FCriticalSection CriticalSection;
};
}
