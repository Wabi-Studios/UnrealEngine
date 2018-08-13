// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace BuildPatchServices
{
	/**
	 * An enum defining the installation mode that should be used.
	 */
	enum class EInstallMode : uint32
	{
		// Construct all required files, but only stage them ready to be completed later.
		StageFiles,

		// Full installation, allowing immediate changes to be made to existing files. The installation is unusable until complete.
		DestructiveInstall,

		// Full installation, staging all required files before moving them all into place in a final step. The installation is still usable if canceled before the moving staging begins.
		NonDestructiveInstall,

		// Execute the prerequisite installer only, downloading it first if necessary. If the specified manifest has no prerequisites, this will result in an error.
		PrereqOnly
	};

	/**
	 * Returns the string representation of the EInstallMode value. Used for analytics and logging only.
	 * @param InstallMode     The value.
	 * @return the enum's string representation.
	 */
	inline const FString& EnumToString(const EInstallMode& InstallMode)
	{
		// Const enum strings, special case no error.
		static const FString StageFiles(TEXT("EInstallMode::StageFiles"));
		static const FString DestructiveInstall(TEXT("EInstallMode::DestructiveInstall"));
		static const FString NonDestructiveInstall(TEXT("EInstallMode::NonDestructiveInstall"));
		static const FString PrereqOnly(TEXT("EInstallMode::PrereqOnly"));
		static const FString InvalidOrMax(TEXT("InvalidOrMax"));

		switch (InstallMode)
		{
			case EInstallMode::StageFiles: return StageFiles;
			case EInstallMode::DestructiveInstall: return DestructiveInstall;
			case EInstallMode::NonDestructiveInstall: return NonDestructiveInstall;
			case EInstallMode::PrereqOnly: return PrereqOnly;
			default: return InvalidOrMax;
		}
	}
}
