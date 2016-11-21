// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Toolkits/BaseToolkit.h"
#include "Layout/Visibility.h"

class FGameplayDebuggerToolkit : public FModeToolkit
{
public:
	FGameplayDebuggerToolkit(class FEdMode* InOwningMode);

	// IToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual class FEdMode* GetEditorMode() const override { return DebuggerEdMode; }
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return MyWidget; }
	// End of IToolkit interface

	// FModeToolkit interface
	virtual void Init(const TSharedPtr<class IToolkitHost>& InitToolkitHost) override;
	// End of FModeToolkit interface

private:
	class FEdMode* DebuggerEdMode;
	TSharedPtr<class SWidget> MyWidget;

	EVisibility GetScreenMessageWarningVisibility() const;
	FReply OnClickedDisableTool();
};

#endif // WITH_EDITOR
