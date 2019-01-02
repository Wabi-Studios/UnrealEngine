// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Modules/ModuleInterface.h"

class FLocationServicesAndroidEditorModule :
	public IModuleInterface
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

