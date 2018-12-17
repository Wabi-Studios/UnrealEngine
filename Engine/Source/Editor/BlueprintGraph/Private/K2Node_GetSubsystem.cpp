// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "K2Node_GetSubsystem.h"

#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "EditorCategoryUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"

#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"

// ************************************************************************************
//    UK2Node_GetSubsystem
// ************************************************************************************

void UK2Node_GetSubsystem::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	if (CustomClass == nullptr)
	{
		if (UEdGraphPin* ClassPin = FindPin(TEXT("Class"), EGPD_Input))
		{
			ClassPin->SetSavePinIfOrphaned(false);
		}
	}
}

void UK2Node_GetSubsystem::Initialize( UClass* NodeClass )
{
	CustomClass = NodeClass;
}

void UK2Node_GetSubsystem::AllocateDefaultPins()
{
	// If required add the world context pin
	if (GetBlueprint()->ParentClass->HasMetaData(FBlueprintMetadata::MD_ShowWorldContextPin))
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), TEXT("WorldContext"));
	}

	// Add blueprint pin
	if (!CustomClass)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Class, USubsystem::StaticClass(), TEXT("Class"));
	}

	// Result pin
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, (CustomClass ? (UClass*)CustomClass : USubsystem::StaticClass()), UEdGraphSchema_K2::PN_ReturnValue);

	Super::AllocateDefaultPins();
}

bool UK2Node_GetSubsystem::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	return Super::IsCompatibleWithGraph(TargetGraph) &&
		(!Blueprint || FBlueprintEditorUtils::FindUserConstructionScript(Blueprint) != TargetGraph);
}

FSlateIcon UK2Node_GetSubsystem::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

FLinearColor UK2Node_GetSubsystem::GetNodeTitleColor() const
{
	return FLinearColor(1.f, 0.078f, 0.576f, 1.f);
}

FText UK2Node_GetSubsystem::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CustomClass)
	{
		if (TitleType == ENodeTitleType::FullTitle)
		{
			return CustomClass->GetDisplayNameText();
		}

		const FString& ClassName = CustomClass->GetName();
		return FText::FormatNamed(NSLOCTEXT("K2Node", "GetSubsystemNodeTitleFormat", "Get {ClassName}"), TEXT("ClassName"), FText::FromString(ClassName));
	}

	return GetTooltipText();
}

void UK2Node_GetSubsystem::GetNodeAttributes(TArray<TKeyValuePair<FString, FString>>& OutNodeAttributes) const
{
	const FString ClassToSpawnStr = CustomClass ? CustomClass->GetName() : TEXT("InvalidClass");
	OutNodeAttributes.Add(TKeyValuePair<FString, FString>(TEXT("Type"), TEXT("GetSubsystems")));
	OutNodeAttributes.Add(TKeyValuePair<FString, FString>(TEXT("Class"), GetClass()->GetName()));
	OutNodeAttributes.Add(TKeyValuePair<FString, FString>(TEXT("Name"), GetName()));
	OutNodeAttributes.Add(TKeyValuePair<FString, FString>(TEXT("ObjectClass"), ClassToSpawnStr));
}

void UK2Node_GetSubsystem::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	static const FName WorldContextObject_ParamName(TEXT("ContextObject"));
	static const FName Class_ParamName(TEXT("Class"));

	UK2Node_GetSubsystem* GetSubsystemNode = this;
	UEdGraphPin* SpawnWorldContextPin = GetSubsystemNode->GetWorldContextPin();
	UEdGraphPin* SpawnClassPin = GetSubsystemNode->GetClassPin();
	UEdGraphPin* SpawnNodeResult = GetSubsystemNode->GetResultPin();

	UClass* SpawnClass = (SpawnClassPin != nullptr) ? Cast<UClass>(SpawnClassPin->DefaultObject) : nullptr;
	if (SpawnClassPin && (SpawnClassPin->LinkedTo.Num() == 0) && !SpawnClass)
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("K2Node", "GetSubsystem_Error", "Node @@ must have a class specified.").ToString(), GetSubsystemNode);
		GetSubsystemNode->BreakAllNodeLinks();
		return;
	}

	// Choose appropriate underlying Getter
	FName Get_FunctionName;
	if (CustomClass->IsChildOf<UGameInstanceSubsystem>())
	{
		Get_FunctionName = GET_FUNCTION_NAME_CHECKED(USubsystemBlueprintLibrary, GetGameInstanceSubsystem);
	}
	else if (CustomClass->IsChildOf<ULocalPlayerSubsystem>())
	{
		Get_FunctionName = GET_FUNCTION_NAME_CHECKED(USubsystemBlueprintLibrary, GetLocalPlayerSubsystem);
	}
	else
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("K2Node", "GetSubsystem_Error", "Node @@ must have a class specified.").ToString(), GetSubsystemNode);
		GetSubsystemNode->BreakAllNodeLinks();
		return;
	}

	//////////////////////////////////////////////////////////////////////////
	// create 'USubsystemBlueprintLibrary::Get[something]Subsystem' call node
	UK2Node_CallFunction* CallGetNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(GetSubsystemNode, SourceGraph);
	CallGetNode->FunctionReference.SetExternalMember(Get_FunctionName, USubsystemBlueprintLibrary::StaticClass());
	CallGetNode->AllocateDefaultPins();

	UEdGraphPin* CallCreateWorldContextPin = CallGetNode->FindPinChecked(WorldContextObject_ParamName);
	UEdGraphPin* CallCreateClassTypePin = CallGetNode->FindPinChecked(Class_ParamName);
	UEdGraphPin* CallCreateResult = CallGetNode->GetReturnValuePin();

	if (SpawnClassPin && SpawnClassPin->LinkedTo.Num() > 0)
	{
		// Copy the 'class' connection from the spawn node to 'USubsystemBlueprintLibrary::Get[something]Subsystem'
		CompilerContext.MovePinLinksToIntermediate(*SpawnClassPin, *CallCreateClassTypePin);
	}
	else
	{
		// Copy class literal onto 'USubsystemBlueprintLibrary::Get[something]Subsystem' call
		CallCreateClassTypePin->DefaultObject = *CustomClass;
	}

	// Copy the world context connection from the spawn node to 'USubsystemBlueprintLibrary::Get[something]Subsystem' if necessary
	if (SpawnWorldContextPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*SpawnWorldContextPin, *CallCreateWorldContextPin);
	}

	// Move result connection from spawn node to 'USubsystemBlueprintLibrary::Get[something]Subsystem'
	CallCreateResult->PinType = SpawnNodeResult->PinType;
	CompilerContext.MovePinLinksToIntermediate(*SpawnNodeResult, *CallCreateResult);

	//////////////////////////////////////////////////////////////////////////

	// Break any links to the expanded node
	GetSubsystemNode->BreakAllNodeLinks();
}

void UK2Node_GetSubsystem::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	if (!CustomClass)
	{
		if (auto ClassNode = GetClassPin(&OldPins))
		{
			CustomClass = Cast<UClass>(ClassNode->DefaultObject);
		}
	}

	AllocateDefaultPins();

	if (CustomClass)
	{
		UEdGraphPin* ResultPin = GetResultPin();
		ResultPin->PinType.PinSubCategoryObject = *CustomClass;
	}
}

class FNodeHandlingFunctor* UK2Node_GetSubsystem::CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const
{
	return new FNodeHandlingFunctor(CompilerContext);
}

void UK2Node_GetSubsystem::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	static TArray<UClass*> Subclasses;
	Subclasses.Reset();
	GetDerivedClasses(UGameInstanceSubsystem::StaticClass(), Subclasses);
	GetDerivedClasses(ULocalPlayerSubsystem::StaticClass(), Subclasses);

	auto CustomizeCallback = [](UEdGraphNode* Node, bool bIsTemplateNode, UClass* Subclass)
	{
		auto TypedNode = CastChecked<UK2Node_GetSubsystem>(Node);
		TypedNode->Initialize(Subclass);
	};

	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		for (auto& Iter : Subclasses)
		{
			UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
			check(Spawner);

			Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeCallback, Iter);
			ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
		}
	}
}

FText UK2Node_GetSubsystem::GetMenuCategory() const
{
	if (CustomClass->IsChildOf<UGameInstanceSubsystem>())
	{
		return NSLOCTEXT("K2Node", "GameInstanceSubsystems", "GameInstance Subsystems");
	}
	else if (CustomClass->IsChildOf<ULocalPlayerSubsystem>())
	{
		return NSLOCTEXT("K2Node", "LocalPlayerSubsystems", "LocalPlayer Subsystems");
	}

	return NSLOCTEXT("K2Node", "InvalidSubsystemType", "Invalid Subsystem Type");
}

FText UK2Node_GetSubsystem::GetTooltipText() const
{
	if (CustomClass)
	{
		FText SubsystemTypeText;
		if (CustomClass->IsChildOf<UGameInstanceSubsystem>())
		{
			SubsystemTypeText =  NSLOCTEXT("K2Node", "GameInstanceSubsystem", "GameInstance Subsystem");
		}
		else
		{
			SubsystemTypeText = NSLOCTEXT("K2Node", "LocalPlayerSubsystem", "LocalPlayer Subsystem");
		}
		return FText::FormatNamed(NSLOCTEXT("K2Node", "GetSubsystemTooltipFormat", "Get {ClassName} a {SubsystemType}"), TEXT("ClassName"), CustomClass->GetDisplayNameText(), TEXT("SubsystemType"), SubsystemTypeText);
	}

	return NSLOCTEXT("K2Node", "InvalidSubsystemType", "InvalidSubsystemType");
}

UEdGraphPin* UK2Node_GetSubsystem::GetWorldContextPin() const
{
	UEdGraphPin* Pin = FindPin(TEXT("WorldContext"));
	check(Pin == NULL || Pin->Direction == EGPD_Input);
	return Pin;
}

UEdGraphPin* UK2Node_GetSubsystem::GetResultPin() const
{
	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}


UEdGraphPin* UK2Node_GetSubsystem::GetClassPin(const TArray<UEdGraphPin*>* InPinsToSearch /*= nullptr */) const
{
	const TArray<UEdGraphPin*>* PinsToSearch = InPinsToSearch ? InPinsToSearch : &Pins;

	UEdGraphPin* Pin = NULL;
	for (auto PinIt = PinsToSearch->CreateConstIterator(); PinIt; ++PinIt)
	{
		UEdGraphPin* TestPin = *PinIt;
		if (TestPin && TestPin->PinName == TEXT("Class"))
		{
			Pin = TestPin;
			break;
		}
	}

	return Pin;
}

// ************************************************************************************
//    UK2Node_GetSubsystemFromPC
// ************************************************************************************

void UK2Node_GetSubsystemFromPC::AllocateDefaultPins()
{
	// If required add the world context pin
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, APlayerController::StaticClass(), TEXT("PlayerController"));

	// Add blueprint pin
	if (!CustomClass)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Class, USubsystem::StaticClass(), TEXT("Class"));
	}

	// Result pin
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, (CustomClass ? (UClass*)CustomClass : ULocalPlayerSubsystem::StaticClass()), UEdGraphSchema_K2::PN_ReturnValue);

	// Skip the UK2Node_GetSubsystem implementation
	UK2Node::AllocateDefaultPins();
}

void UK2Node_GetSubsystemFromPC::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	// Skip the UK2Node_GetSubsystem implementation
	UK2Node::ExpandNode(CompilerContext, SourceGraph);

	static const FName PlayerController_ParamName(TEXT("PlayerController"));
	static const FName Class_ParamName(TEXT("Class"));

	UK2Node_GetSubsystemFromPC* GetSubsystemFromPCNode = this;
	UEdGraphPin* SpawnPlayerControllerPin = GetSubsystemFromPCNode->GetPlayerControllerPin();
	UEdGraphPin* SpawnClassPin = GetSubsystemFromPCNode->GetClassPin();
	UEdGraphPin* SpawnNodeResult = GetSubsystemFromPCNode->GetResultPin();

	UClass* SpawnClass = (SpawnClassPin != nullptr) ? Cast<UClass>(SpawnClassPin->DefaultObject) : nullptr;
	if (SpawnClassPin && (SpawnClassPin->LinkedTo.Num() == 0) && !SpawnClass)
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("K2Node", "GetSubsystem_Error", "Node @@ must have a class specified.").ToString(), GetSubsystemFromPCNode);
		GetSubsystemFromPCNode->BreakAllNodeLinks();
		return;
	}

	// Choose appropriate underlying Getter
	FName Get_FunctionName;
	if (CustomClass->IsChildOf<ULocalPlayerSubsystem>())
	{
		Get_FunctionName = GET_FUNCTION_NAME_CHECKED(USubsystemBlueprintLibrary, GetLocalPlayerSubSystemFromPlayerController);
	}
	else
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("K2Node", "GetSubsystem_Error", "Node @@ must have a class specified.").ToString(), GetSubsystemFromPCNode);
		GetSubsystemFromPCNode->BreakAllNodeLinks();
		return;
	}

	//////////////////////////////////////////////////////////////////////////
	// create 'USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController' call node
	UK2Node_CallFunction* CallGetNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(GetSubsystemFromPCNode, SourceGraph);
	CallGetNode->FunctionReference.SetExternalMember(Get_FunctionName, USubsystemBlueprintLibrary::StaticClass());
	CallGetNode->AllocateDefaultPins();

	UEdGraphPin* CallCreatePlayerControllerPin = CallGetNode->FindPinChecked(PlayerController_ParamName);
	UEdGraphPin* CallCreateClassTypePin = CallGetNode->FindPinChecked(Class_ParamName);
	UEdGraphPin* CallCreateResult = CallGetNode->GetReturnValuePin();

	if (SpawnClassPin && SpawnClassPin->LinkedTo.Num() > 0)
	{
		// Copy the 'class' connection from the spawn node to 'USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController'
		CompilerContext.MovePinLinksToIntermediate(*SpawnClassPin, *CallCreateClassTypePin);
	}
	else
	{
		// Copy class literal onto 'USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController' call
		CallCreateClassTypePin->DefaultObject = *CustomClass;
	}

	// Copy the world context connection from the spawn node to 'USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController' if necessary
	if (SpawnPlayerControllerPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*SpawnPlayerControllerPin, *CallCreatePlayerControllerPin);
	}

	// Move result connection from spawn node to 'USubsystemBlueprintLibrary::Get[something]Subsystem'
	CallCreateResult->PinType = SpawnNodeResult->PinType;
	CompilerContext.MovePinLinksToIntermediate(*SpawnNodeResult, *CallCreateResult);

	//////////////////////////////////////////////////////////////////////////

	// Break any links to the expanded node
	GetSubsystemFromPCNode->BreakAllNodeLinks();
}

void UK2Node_GetSubsystemFromPC::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	static TArray<UClass*> Subclasses;
	Subclasses.Reset();
	GetDerivedClasses(ULocalPlayerSubsystem::StaticClass(), Subclasses);

	auto CustomizeCallback = [](UEdGraphNode* Node, bool bIsTemplateNode, UClass* Subclass)
	{
		auto TypedNode = CastChecked<UK2Node_GetSubsystemFromPC>(Node);
		TypedNode->Initialize(Subclass);
	};

	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		for (auto& Iter : Subclasses)
		{
			UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
			check(Spawner);

			Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(CustomizeCallback, Iter);
			ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
		}
	}
}

FText UK2Node_GetSubsystemFromPC::GetMenuCategory() const
{
	return NSLOCTEXT("K2Node", "LocalPlayerSubsystemsFromPC", "PlayerController|LocalPlayer Subsystems");
}

FText UK2Node_GetSubsystemFromPC::GetTooltipText() const
{
	if (CustomClass)
	{
		return FText::FormatNamed(NSLOCTEXT("K2Node", "GetSubsystemTooltipFormat", "Get {ClassName} from Player Controller"), TEXT("ClassName"), CustomClass->GetDisplayNameText());
	}

	return NSLOCTEXT("K2Node", "InvalidSubsystemType", "InvalidSubsystemType");
}

UEdGraphPin* UK2Node_GetSubsystemFromPC::GetPlayerControllerPin() const
{
	UEdGraphPin* Pin = FindPin(TEXT("PlayerController"));
	check(Pin == NULL || Pin->Direction == EGPD_Input);
	return Pin;
}