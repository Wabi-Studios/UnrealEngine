// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditor.h"

#include "PCGEditorCommands.h"
#include "PCGEditorCommon.h"
#include "PCGEditorGraph.h"
#include "PCGEditorGraphNodeBase.h"
#include "PCGEditorGraphSchema.h"
#include "PCGGraph.h"
#include "SPCGEditorGraphFind.h"
#include "SPCGEditorGraphNodePalette.h"
#include "PCGEditorSettings.h"

#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SNodePanel.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "SourceCodeNavigation.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Preferences/UnrealEdOptions.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PCGEditorGraphSchemaActions.h"
#include "GraphEditAction.h"

#define LOCTEXT_NAMESPACE "PCGGraphEditor"

namespace FPCGEditor_private
{
	const FName GraphEditorID = FName(TEXT("GraphEditor"));
	const FName PropertyDetailsID = FName(TEXT("PropertyDetails"));
	const FName PaletteID = FName(TEXT("Palette"));
	const FName AttributesID = FName(TEXT("Attributes"));
	const FName ViewportID = FName(TEXT("Viewport"));
	const FName FindID = FName(TEXT("Find"));
}

void FPCGEditor::Initialize(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UPCGGraph* InPCGGraph)
{
	PCGGraphBeingEdited = InPCGGraph;

	PCGEditorGraph = NewObject<UPCGEditorGraph>(PCGGraphBeingEdited, UPCGEditorGraph::StaticClass(), NAME_None, RF_Transactional | RF_Transient);
	PCGEditorGraph->Schema = UPCGEditorGraphSchema::StaticClass();
	PCGEditorGraph->InitFromNodeGraph(InPCGGraph);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	PropertyDetailsWidget = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	PropertyDetailsWidget->SetObject(PCGGraphBeingEdited);

	GraphEditorWidget = CreateGraphEditorWidget();
	PaletteWidget = CreatePaletteWidget();
	FindWidget = CreateFindWidget();

	BindCommands();

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_PCGGraphEditor_Layout_v0.4")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.10f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.16)
					->SetHideTabWell(true)
					->AddTab(FPCGEditor_private::ViewportID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.84)
					->SetHideTabWell(true)
					->AddTab(FPCGEditor_private::PaletteID, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.70f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.72)
					->SetHideTabWell(true)
					->AddTab(FPCGEditor_private::GraphEditorID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.28)
					->SetHideTabWell(true)
					->AddTab(FPCGEditor_private::AttributesID, ETabState::OpenedTab)
					->AddTab(FPCGEditor_private::FindID, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.20f)
				->SetHideTabWell(true)
				->AddTab(FPCGEditor_private::PropertyDetailsID, ETabState::OpenedTab)
			)
		);

	const FName PCGGraphEditorAppName = FName(TEXT("PCGEditorApp"));

	InitAssetEditor(InMode, InToolkitHost, PCGGraphEditorAppName, StandaloneDefaultLayout, /*bCreateDefaultStandaloneMenu=*/ true, /*bCreateDefaultToolbar=*/ true, InPCGGraph);
}

UPCGEditorGraph* FPCGEditor::GetPCGEditorGraph()
{
	return PCGEditorGraph;
}

void FPCGEditor::JumpToNode(const UEdGraphNode* InNode)
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->JumpToNode(InNode);
	}
}

void FPCGEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_PCGEditor", "PCG Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	//TODO: Add Icons
	InTabManager->RegisterTabSpawner(FPCGEditor_private::GraphEditorID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_GraphEditor))
		.SetDisplayName(LOCTEXT("GraphTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FPCGEditor_private::PropertyDetailsID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_PropertyDetails))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FPCGEditor_private::PaletteID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_Palette))
		.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FPCGEditor_private::AttributesID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_Attributes))
		.SetDisplayName(LOCTEXT("AttributesTab", "Attributes"))
		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FPCGEditor_private::ViewportID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef);

	InTabManager->RegisterTabSpawner(FPCGEditor_private::FindID, FOnSpawnTab::CreateSP(this, &FPCGEditor::SpawnTab_Find))
		.SetDisplayName(LOCTEXT("FindTab", "Find"))
		.SetGroup(WorkspaceMenuCategoryRef);
}

void FPCGEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(FPCGEditor_private::GraphEditorID);
	InTabManager->UnregisterTabSpawner(FPCGEditor_private::PropertyDetailsID);
	InTabManager->UnregisterTabSpawner(FPCGEditor_private::PaletteID);
	InTabManager->UnregisterTabSpawner(FPCGEditor_private::AttributesID);
	InTabManager->UnregisterTabSpawner(FPCGEditor_private::ViewportID);

	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

bool FPCGEditor::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	return InContext.Context == FPCGEditorCommon::ContextIdentifier;
}

void FPCGEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		if (PCGGraphBeingEdited)
		{
			PCGGraphBeingEdited->NotifyGraphChanged(true);
		}

		if (GraphEditorWidget.IsValid())
		{
			GraphEditorWidget->ClearSelectionSet();
			GraphEditorWidget->NotifyGraphChanged();

			FSlateApplication::Get().DismissAllMenus();
		}
	}
}

FName FPCGEditor::GetToolkitFName() const
{
	return FName(TEXT("PCGEditor"));
}

FText FPCGEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "PCG Editor");
}

FLinearColor FPCGEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FPCGEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "PCG ").ToString();
}

void FPCGEditor::BindCommands()
{
	const FPCGEditorCommands& PCGEditorCommands = FPCGEditorCommands::Get();

	ToolkitCommands->MapAction(
		PCGEditorCommands.Find,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnFind));
}

void FPCGEditor::OnFind()
{
	if (TabManager.IsValid() && FindWidget.IsValid())
	{
		TabManager->TryInvokeTab(FPCGEditor_private::FindID);
		FindWidget->FocusForUse();
	}
}

void FPCGEditor::SelectAllNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->SelectAllNodes();
	}
}

bool FPCGEditor::CanSelectAllNodes() const
{
	return GraphEditorWidget.IsValid();
}

void FPCGEditor::DeleteSelectedNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		UPCGGraph* PCGGraph = PCGEditorGraph->GetPCGGraph();
		check(PCGEditorGraph && PCGGraph);

		bool bChanged = false;
		{
			const FScopedTransaction Transaction(*FPCGEditorCommon::ContextIdentifier, LOCTEXT("PCGEditorDeleteTransactionMessage", "PCG Editor: Delete"), nullptr);
			PCGEditorGraph->Modify();

			for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
			{
				if (UPCGEditorGraphNodeBase* PCGEditorGraphNode = Cast<UPCGEditorGraphNodeBase>(Object))
				{
					if (PCGEditorGraphNode->CanUserDeleteNode())
					{
						UPCGNode* PCGNode = PCGEditorGraphNode->GetPCGNode();
						check(PCGNode);

						PCGGraph->RemoveNode(PCGNode);
						PCGEditorGraphNode->DestroyNode();
						bChanged = true;
					}
				}
				else if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
				{
					if (GraphNode->CanUserDeleteNode())
					{
						GraphNode->DestroyNode();
						bChanged = true;
					}
				}
			}

			PCGEditorGraph->Modify();
		}

		if (bChanged)
		{
			GraphEditorWidget->ClearSelectionSet();
			GraphEditorWidget->NotifyGraphChanged();
			PCGGraphBeingEdited->NotifyGraphChanged(true);
		}
	}
}

bool FPCGEditor::CanDeleteSelectedNodes() const
{
	if (GraphEditorWidget.IsValid())
	{
		for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
		{
			UEdGraphNode* GraphNode = CastChecked<UEdGraphNode>(Object);

			if (GraphNode->CanUserDeleteNode())
			{
				return true;
			}
		}
	}

	return false;
}

void FPCGEditor::CopySelectedNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		const FGraphPanelSelectionSet SelectedNodes = GraphEditorWidget->GetSelectedNodes();

		//TODO: evaluate creating a clipboard object instead of ownership hack
		for (UObject* SelectedNode : SelectedNodes)
		{
			UEdGraphNode* GraphNode = CastChecked<UEdGraphNode>(SelectedNode);
			GraphNode->PrepareForCopying();
		}

		FString ExportedText;
		FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);
		FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

		for (UObject* SelectedNode : SelectedNodes)
		{
			if (UPCGEditorGraphNodeBase* PCGGraphNode = Cast<UPCGEditorGraphNodeBase>(SelectedNode))
			{
				PCGGraphNode->PostCopy();
			}
		}
	}
}

bool FPCGEditor::CanCopySelectedNodes() const
{
	if (GraphEditorWidget.IsValid())
	{
		for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
		{
			if (UEdGraphNode* GraphNode = CastChecked<UEdGraphNode>(Object))
			{
				if (GraphNode->CanDuplicateNode())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FPCGEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	DeleteSelectedNodes();
}

bool FPCGEditor::CanCutSelectedNodes() const
{
	return CanCopySelectedNodes() && CanDeleteSelectedNodes();
}

void FPCGEditor::PasteNodes()
{
	if (GraphEditorWidget.IsValid())
	{
		PasteNodesHere(GraphEditorWidget->GetPasteLocation());
	}
}

void FPCGEditor::PasteNodesHere(const FVector2D& Location)
{
	if (!GraphEditorWidget.IsValid() || !PCGEditorGraph)
	{
		return;
	}

	const FScopedTransaction Transaction(*FPCGEditorCommon::ContextIdentifier, LOCTEXT("PCGEditorPasteTransactionMessage", "PCG Editor: Paste"), nullptr);
	PCGEditorGraph->Modify();

	// Clear the selection set (newly pasted stuff will be selected)
	GraphEditorWidget->ClearSelectionSet();

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Import the nodes
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(PCGEditorGraph, TextToImport, /*out*/ PastedNodes);

	//Average position of nodes so we can move them while still maintaining relative distances to each other
	FVector2D AvgNodePosition(0.0f, 0.0f);

	// Number of nodes used to calculate AvgNodePosition
	int32 AvgCount = 0;

	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		if (PastedNode)
		{
			AvgNodePosition.X += PastedNode->NodePosX;
			AvgNodePosition.Y += PastedNode->NodePosY;
			++AvgCount;
		}
	}

	if (AvgCount > 0)
	{
		float InvNumNodes = 1.0f / float(AvgCount);
		AvgNodePosition.X *= InvNumNodes;
		AvgNodePosition.Y *= InvNumNodes;
	}

	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		GraphEditorWidget->SetNodeSelection(PastedNode, true);

		PastedNode->NodePosX = (PastedNode->NodePosX - AvgNodePosition.X) + Location.X;
		PastedNode->NodePosY = (PastedNode->NodePosY - AvgNodePosition.Y) + Location.Y;

		PastedNode->SnapToGrid(SNodePanel::GetSnapGridSize());

		PastedNode->CreateNewGuid();

		if (UPCGEditorGraphNodeBase* PastedPCGGraphNode = Cast<UPCGEditorGraphNodeBase>(PastedNode))
		{
			if (UPCGNode* PastedPCGNode = PastedPCGGraphNode->GetPCGNode())
			{
				PCGGraphBeingEdited->AddNode(PastedPCGNode);

				PastedPCGGraphNode->PostPaste();
			}
		}
	}

	GraphEditorWidget->NotifyGraphChanged();
}

bool FPCGEditor::CanPasteNodes() const
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return FEdGraphUtilities::CanImportNodesFromText(PCGEditorGraph, ClipboardContent);
}

void FPCGEditor::DuplicateNodes()
{
	CopySelectedNodes();
	PasteNodes();
}

bool FPCGEditor::CanDuplicateNodes() const
{
	return CanCopySelectedNodes();
}

void FPCGEditor::OnAlignTop()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignTop();
	}
}

void FPCGEditor::OnAlignMiddle()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignMiddle();
	}
}

void FPCGEditor::OnAlignBottom()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignBottom();
	}
}

void FPCGEditor::OnAlignLeft()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignLeft();
	}
}

void FPCGEditor::OnAlignCenter()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignCenter();
	}
}

void FPCGEditor::OnAlignRight()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnAlignRight();
	}
}

void FPCGEditor::OnStraightenConnections()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnStraightenConnections();
	}
}

void FPCGEditor::OnDistributeNodesH()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnDistributeNodesH();
	}
}

void FPCGEditor::OnDistributeNodesV()
{
	if (GraphEditorWidget.IsValid())
	{
		GraphEditorWidget->OnDistributeNodesV();
	}
}

void FPCGEditor::OnCreateComment()
{
	if (PCGEditorGraph)
	{
		FPCGEditorGraphSchemaAction_NewComment CommentAction;

		TSharedPtr<SGraphEditor> GraphEditorPtr = SGraphEditor::FindGraphEditorForGraph(PCGEditorGraph);
		FVector2D Location;
		if (GraphEditorPtr)
		{
			Location = GraphEditorPtr->GetPasteLocation();
		}

		CommentAction.PerformAction(PCGEditorGraph, nullptr, Location);
	}
}

TSharedRef<SGraphEditor> FPCGEditor::CreateGraphEditorWidget()
{
	GraphEditorCommands = MakeShareable(new FUICommandList);

	// Editing commands
	GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateSP(this, &FPCGEditor::SelectAllNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanSelectAllNodes));

	GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FPCGEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanDeleteSelectedNodes));

	GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FPCGEditor::CopySelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanCopySelectedNodes));

	GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FPCGEditor::CutSelectedNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanCutSelectedNodes));

	GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FPCGEditor::PasteNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanPasteNodes));

	GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FPCGEditor::DuplicateNodes),
		FCanExecuteAction::CreateSP(this, &FPCGEditor::CanDuplicateNodes));

	// Alignment Commands
	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignTop)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignMiddle)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignBottom)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesLeft,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignLeft)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesCenter,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignCenter)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesRight,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnAlignRight)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().StraightenConnections,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnStraightenConnections)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnCreateComment)
	);

	// Distribution Commands
	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnDistributeNodesH)
	);

	GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
		FExecuteAction::CreateSP(this, &FPCGEditor::OnDistributeNodesV)
	);

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("PCGGraphEditorCornerText", "Procedural Graph");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FPCGEditor::OnSelectedNodesChanged);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FPCGEditor::OnNodeTitleCommitted);
	InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FPCGEditor::OnNodeDoubleClicked);

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.Appearance(AppearanceInfo)
		.GraphToEdit(PCGEditorGraph)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}

void FPCGEditor::ReplicateExtraNodes() const
{
	if (PCGEditorGraph)
	{
		if (UPCGGraph* PCGGraph = PCGEditorGraph->GetPCGGraph())
		{
			TArray<TObjectPtr<const UObject>> ExtraNodes;
			for (const UEdGraphNode* GraphNode : PCGEditorGraph->Nodes)
			{
				check(GraphNode);
				if (!GraphNode->IsA<UPCGEditorGraphNodeBase>())
				{
					ExtraNodes.Add(GraphNode);
				}

			}

			PCGGraph->SetExtraEditorNodes(ExtraNodes);
		}
	}
}

void FPCGEditor::SaveAsset_Execute()
{
	// Extra nodes are replicated on asset save, to be saved in the underlying PCGGraph
	ReplicateExtraNodes();

	FAssetEditorToolkit::SaveAsset_Execute();
}

void FPCGEditor::OnClose()
{
	// Extra nodes are replicated on editor close, to be saved in the underlying PCGGraph
	ReplicateExtraNodes();

	FAssetEditorToolkit::OnClose();
}

TSharedRef<SPCGEditorGraphNodePalette> FPCGEditor::CreatePaletteWidget()
{
	return SNew(SPCGEditorGraphNodePalette);
}

TSharedRef<SPCGEditorGraphFind> FPCGEditor::CreateFindWidget()
{
	return SNew(SPCGEditorGraphFind, SharedThis(this));
}

void FPCGEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;

	if (NewSelection.Num() == 0)
	{
		SelectedObjects.Add(PCGGraphBeingEdited);
	}
	else
	{
		for (UObject* Object : NewSelection)
		{
			if (UPCGEditorGraphNodeBase* PCGGraphNode = Cast<UPCGEditorGraphNodeBase>(Object))
			{
				if (UPCGNode* PCGNode = PCGGraphNode->GetPCGNode())
				{
					SelectedObjects.Add(PCGNode->DefaultSettings);
				}
			}
			else if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
			{
				SelectedObjects.Add(GraphNode);
			}
		}
	}

	PropertyDetailsWidget->SetObjects(SelectedObjects, /*bForceRefresh=*/true);

	GetTabManager()->TryInvokeTab(FPCGEditor_private::PropertyDetailsID);
}

void FPCGEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		const FScopedTransaction Transaction(*FPCGEditorCommon::ContextIdentifier, LOCTEXT("PCGEditorRenameNode", "PCG Editor: Rename Node"), nullptr);
		NodeBeingChanged->Modify();
		NodeBeingChanged->OnRenameNode(NewText.ToString());
	}
}

void FPCGEditor::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	if (Node != nullptr)
	{
		if (UObject* Object = Node->GetJumpTargetForDoubleClick())
		{
			if (UPCGSettings* PCGSettings = Cast<UPCGSettings>(Object))
			{
				JumpToDefinition(PCGSettings->GetClass());
			}
			else
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
			}
		}
	}
}

void FPCGEditor::JumpToDefinition(const UClass* Class) const
{
	if (ensure(GUnrealEd) && GUnrealEd->GetUnrealEdOptions()->IsCPPAllowed())
	{
		const bool bEnableNavigateToNativeNodes = GetDefault<UPCGEditorSettings>()->bEnableNavigateToNativeNodes;
		if (bEnableNavigateToNativeNodes)
		{
			FSourceCodeNavigation::NavigateToClass(Class);
		}
		else
		{
			// Inform user that the node is native, give them opportunity to enable navigation to native nodes:
			FNotificationInfo Info(LOCTEXT("NavigateToNativeDisabled", "Navigation to Native (c++) PCG Nodes Disabled"));
			Info.ExpireDuration = 10.0f;
			Info.CheckBoxState = bEnableNavigateToNativeNodes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;

			Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateStatic(
				[](ECheckBoxState NewState)
				{
					const FScopedTransaction Transaction(LOCTEXT("ChangeEnableNavigateToNativeNodes", "Change Enable Navigate to Native Nodes Setting"));

					UPCGEditorSettings* MutableEditorSetings = GetMutableDefault<UPCGEditorSettings>();
					MutableEditorSetings->Modify();
					MutableEditorSetings->bEnableNavigateToNativeNodes = (NewState == ECheckBoxState::Checked) ? true : false;
					MutableEditorSetings->SaveConfig();
				}
			);
			Info.CheckBoxText = LOCTEXT("EnableNavigationToNative", "Enable Navigate to Native Nodes?");

			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_GraphEditor(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGGraphTitle", "Graph"))
		.TabColorScale(GetTabColorScale())
		[
			GraphEditorWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_PropertyDetails(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGDetailsTitle", "Details"))
		.TabColorScale(GetTabColorScale())
		[
			PropertyDetailsWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_Palette(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGPaletteTitle", "Palette"))
		.TabColorScale(GetTabColorScale())
		[
			PaletteWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_Attributes(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGAttributesTitle", "Attributes"))
		.TabColorScale(GetTabColorScale())
		[
			SNullWidget::NullWidget
		];
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGViewportTitle", "Viewport"))
		.TabColorScale(GetTabColorScale())
		[
			SNullWidget::NullWidget
		];
}

TSharedRef<SDockTab> FPCGEditor::SpawnTab_Find(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PCGFindTitle", "Find"))
		.TabColorScale(GetTabColorScale())
		[
			FindWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
