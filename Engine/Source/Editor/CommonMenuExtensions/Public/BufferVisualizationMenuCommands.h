// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Templates/SharedPointer.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/Commands.h"

class FEditorViewportClient;

class COMMONMENUEXTENSIONS_API FBufferVisualizationMenuCommands : public TCommands<FBufferVisualizationMenuCommands>
{
public:
	struct FBufferVisualizationRecord
	{
		FName Name;
		TSharedPtr<FUICommandInfo> Command;

		FBufferVisualizationRecord()
			: Name(),
			  Command()
		{
		}
	};

	typedef TMultiMap<FName, FBufferVisualizationRecord> TBufferVisualizationModeCommandMap;
	typedef TBufferVisualizationModeCommandMap::TConstIterator TCommandConstIterator;

	FBufferVisualizationMenuCommands();

	TCommandConstIterator CreateCommandConstIterator() const;
	const FBufferVisualizationRecord& OverviewCommand() const;

	static void BuildVisualisationSubMenu(FMenuBuilder& Menu);

	virtual void RegisterCommands() override;

	void BindCommands(FUICommandList& CommandList, const TSharedPtr<FEditorViewportClient>& Client) const;

private:
	void BuildCommandMap();

	void CreateOverviewCommand();
	void CreateVisualizationCommands();

	void AddOverviewCommandToMenu(FMenuBuilder& Menu) const;
	void AddVisualizationCommandsToMenu(FMenuBuilder& menu) const;

	static void ChangeBufferVisualizationMode(const TSharedPtr<FEditorViewportClient>& Client, FName InName);
	static bool IsBufferVisualizationModeSelected(const TSharedPtr<FEditorViewportClient>& Client, FName InName);

	TBufferVisualizationModeCommandMap CommandMap;
};