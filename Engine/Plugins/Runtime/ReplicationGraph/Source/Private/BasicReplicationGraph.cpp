// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
// 
#include "BasicReplicationGraph.h"
#include "Net/UnrealNetwork.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "CoreGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Engine/NetConnection.h"

UBasicReplicationGraph::UBasicReplicationGraph()
{

}

void UBasicReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	// ReplicationGraph stores internal associative data for actor classes. 
	// We build this data here based on actor CDO values.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());
		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}

		// Skip SKEL and REINST classes.
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		FClassReplicationInfo ClassInfo;

		// Replication Graph is frame based. Convert NetUpdateFrequency to ReplicationPeriodFrame based on Server MaxTickRate.
		ClassInfo.ReplicationPeriodFrame = FMath::Max<uint32>( (uint32)FMath::RoundToFloat(NetDriver->NetServerMaxTickRate / ActorCDO->NetUpdateFrequency), 1);
		
		if (ActorCDO->bAlwaysRelevant || ActorCDO->bOnlyRelevantToOwner)
		{
			ClassInfo.CullDistanceSquared = 0.f;
		}
		else
		{
			ClassInfo.CullDistanceSquared = ActorCDO->NetCullDistanceSquared;
		}
		
		GlobalActorReplicationInfoMap.SetClassInfo( Class, ClassInfo );
	}
}

void UBasicReplicationGraph::InitGlobalGraphNodes()
{
	// Preallocate some replication lists.
	PreAllocateRepList(3, 12);
	PreAllocateRepList(6, 12);
	PreAllocateRepList(128, 64);

	// -----------------------------------------------
	//	Spatial Actors
	// -----------------------------------------------

	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = 10000.f;
	GridNode->SpatialBias = FVector2D(-WORLD_MAX, -WORLD_MAX);

	AddGlobalGraphNode(GridNode);

	// -----------------------------------------------
	//	Always Relevant (to everyone) Actors
	// -----------------------------------------------
	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);
}

void UBasicReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection)
{
	Super::InitConnectionGraphNodes(RepGraphConnection);

	UReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantNodeForConnection = CreateNewNode<UReplicationGraphNode_AlwaysRelevant_ForConnection>();
	AddConnectionGraphNode(AlwaysRelevantNodeForConnection, RepGraphConnection);

	AlwaysRelevantForConnectionList.Emplace(RepGraphConnection->NetConnection, AlwaysRelevantNodeForConnection);
}

void UBasicReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	if (ActorInfo.Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
	}
	else if (ActorInfo.Actor->bOnlyRelevantToOwner)
	{
		ActorsWithoutNetConnection.Add(ActorInfo.Actor);
	}
	else
	{
		// Note that UReplicationGraphNode_GridSpatialization2D has 3 methods for adding actor based on the mobility of the actor. Since AActor lacks this information, we will
		// add all spatialized actors as dormant actors: meaning they will be treated as possibly dynamic (moving) when not dormant, and as static (not moving) when dormant.
		GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
	}
}

void UBasicReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	if (ActorInfo.Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
	}
	else if (ActorInfo.Actor->bOnlyRelevantToOwner)
	{
		if (UReplicationGraphNode* Node = GetAlwaysRelevantNodeForConnection(ActorInfo.Actor->GetNetConnection()))
		{
			Node->NotifyRemoveNetworkActor(ActorInfo);
		}
	}
	else
	{
		GridNode->RemoveActor_Dormancy(ActorInfo);
	}
}

UReplicationGraphNode_AlwaysRelevant_ForConnection* UBasicReplicationGraph::GetAlwaysRelevantNodeForConnection(UNetConnection* Connection)
{
	UReplicationGraphNode_AlwaysRelevant_ForConnection* Node = nullptr;
	if (Connection)
	{
		if (FConnectionAlwaysRelevantNodePair* Pair = AlwaysRelevantForConnectionList.FindByKey(Connection))
		{
			if (Pair->Node)
			{
				Node = Pair->Node;
			}
			else
			{
				UE_LOG(LogNet, Warning, TEXT("AlwaysRelevantNode for connection %s is null."), *GetNameSafe(Connection));
			}
		}
		else
		{
			UE_LOG(LogNet, Warning, TEXT("Could not find AlwaysRelevantNode for connection %s. This should have been created in UBasicReplicationGraph::InitConnectionGraphNodes."), *GetNameSafe(Connection));
		}
	}
	else
	{
		// Basic implementation requires owner is set on spawn that never changes. A more robust graph would have methods or ways of listening for owner to change
		UE_LOG(LogNet, Warning, TEXT("Actor: %s is bOnlyRelevantToOwner but does not have an owning Netconnection. It will not be replicated"));
	}

	return Node;
}

int32 UBasicReplicationGraph::ServerReplicateActors(float DeltaSeconds)
{
	// Route Actors needing owning net connections to appropriate nodes
	for (int32 idx=ActorsWithoutNetConnection.Num()-1; idx>=0; --idx)
	{
		bool bRemove = false;
		if (AActor* Actor = ActorsWithoutNetConnection[idx])
		{
			if (UNetConnection* Connection = Actor->GetNetConnection())
			{
				bRemove = true;
				if (UReplicationGraphNode_AlwaysRelevant_ForConnection* Node = GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()))
				{
					Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Actor));
				}
			}
		}
		else
		{
			bRemove = true;
		}

		if (bRemove)
		{
			ActorsWithoutNetConnection.RemoveAtSwap(idx, 1, false);
		}
	}


	return Super::ServerReplicateActors(DeltaSeconds);
}