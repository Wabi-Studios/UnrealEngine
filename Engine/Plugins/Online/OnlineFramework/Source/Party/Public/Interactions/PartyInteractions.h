// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SocialInteractionMacros.h"

/**
* Default Party interactions supported by the Social framework
*/

DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, InviteToParty);
DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, JoinParty);

DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, AcceptPartyInvite);
DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, RejectPartyInvite);

DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, LeaveParty);
DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, KickPartyMember);
DECLARE_SOCIAL_INTERACTION_EXPORT(PARTY_API, PromoteToPartyLeader);