// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAnimationModifiersModule.h"

#include "WorkflowOrientedApp/ApplicationMode.h"
#include "WorkflowOrientedApp/WorkflowCentricApplication.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

/** Animation modifiers module, handles injecting of the AnimationModifiersTab into animation and skeleton editor modes */
class FAnimationModifiersModule : public IAnimationModifiersModule
{
public:
	/** Called right after the module DLL has been loaded and the module object has been created */
	virtual void StartupModule() override;

	/** Called before the module is unloaded, right before the module object is destroyed */
	virtual void ShutdownModule() override;

	/** Begin IAnimationModifiersModule overrides */
	virtual void ShowAddAnimationModifierWindow(const TArray<UAnimSequence*>& InSequences) override;
	virtual void ApplyAnimationModifiers(const TArray<UAnimSequence*>& InSequences, bool bForceApply = true) override;
	/** End IAnimationModifiersModule overrides */

protected:
	/** Callback for extending an application mode */
	TSharedRef<FApplicationMode> ExtendApplicationMode(const FName ModeName, TSharedRef<FApplicationMode> InMode);

	/** Weak list of application modes for which a tab factory was registered */
	TArray<TWeakPtr<FApplicationMode>> RegisteredApplicationModes;

	FWorkflowApplicationModeExtender Extender;

	/** Callbacks used to add and apply default animation modifier classes */
	void OnAssetPostImport(UFactory* ImportFactory, UObject* ImportedObject);
	void OnAssetPostReimport(UObject* ReimportedObject);
};
