// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BuildSettings.h"

namespace BuildSettings
{
	bool IsLicenseeVersion()
	{
		return ENGINE_IS_LICENSEE_VERSION;
	}

	int GetCurrentChangelist()
	{
		return CURRENT_CHANGELIST;
	}

	int GetCompatibleChangelist()
	{
		return COMPATIBLE_CHANGELIST;
	}

	const TCHAR* GetBranchName()
	{
		return TEXT(BRANCH_NAME);
	}

	const TCHAR* GetBuildVersion()
	{
		return TEXT(BUILD_VERSION);
	}

	bool IsPromotedBuild()
	{
		return ENGINE_IS_PROMOTED_BUILD;
	}
}


