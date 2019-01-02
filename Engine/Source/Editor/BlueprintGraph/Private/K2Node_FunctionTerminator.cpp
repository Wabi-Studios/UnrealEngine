// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "K2Node_FunctionTerminator.h"
#include "UObject/UnrealType.h"
#include "UObject/FrameworkObjectVersion.h"
#include "GraphEditorSettings.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_FunctionTerminator::UK2Node_FunctionTerminator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_FunctionTerminator::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::FunctionTerminatorNodesUseMemberReference)
		{
			FunctionReference.SetExternalMember(SignatureName_DEPRECATED, SignatureClass_DEPRECATED);
		}
	}
}

FLinearColor UK2Node_FunctionTerminator::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->FunctionTerminatorNodeTitleColor;
}

FName UK2Node_FunctionTerminator::CreateUniquePinName(FName InSourcePinName) const
{
	const UFunction* FoundFunction = FFunctionFromNodeHelper::FunctionFromNode(this);

	FName ResultName = InSourcePinName;
	int UniqueNum = 0;
	// Prevent the unique name from being the same as another of the UFunction's properties
	while(FindPin(ResultName) || FindField<const UProperty>(FoundFunction, ResultName) != nullptr)
	{
		ResultName = *FString::Printf(TEXT("%s%d"), *InSourcePinName.ToString(), ++UniqueNum);
	}
	return ResultName;
}

bool UK2Node_FunctionTerminator::CanCreateUserDefinedPin(const FEdGraphPinType& InPinType, EEdGraphPinDirection InDesiredDirection, FText& OutErrorMessage)
{
	const bool bIsNodeEditable = IsEditable();

	// Make sure that if this is an exec node we are allowed one.
	if (bIsNodeEditable && InPinType.PinCategory == UEdGraphSchema_K2::PC_Exec && !CanModifyExecutionWires())
	{
		OutErrorMessage = LOCTEXT("MultipleExecPinError", "Cannot support more exec pins!");
		return false;
	}
	else if (!bIsNodeEditable)
	{
		OutErrorMessage = LOCTEXT("NotEditableError", "Cannot edit this node!");
	}

	return bIsNodeEditable;
}

bool UK2Node_FunctionTerminator::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	const UBlueprint* SourceBlueprint = GetBlueprint();

	UClass* SourceClass = FunctionReference.GetMemberParentClass(GetBlueprintClassFromNode());
	bool bResult = (SourceClass != nullptr) && (SourceClass->ClassGeneratedBy != SourceBlueprint);
	if (bResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(SourceClass);
	}

	// All structures, that are required for the BP compilation, should be gathered
	for (auto Pin : Pins)
	{
		UStruct* DepStruct = Pin ? Cast<UStruct>(Pin->PinType.PinSubCategoryObject.Get()) : nullptr;

		UClass* DepClass = Cast<UClass>(DepStruct);
		if (DepClass && (DepClass->ClassGeneratedBy == SourceBlueprint))
		{
			//Don't include self
			continue;
		}

		if (DepStruct && !DepStruct->IsNative())
		{
			if (OptionalOutput)
			{
				OptionalOutput->AddUnique(DepStruct);
			}
			bResult = true;
		}
	}

	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bResult;
}

void UK2Node_FunctionTerminator::PromoteFromInterfaceOverride(bool bIsPrimaryTerminator)
{
	// Remove the signature class, that is not relevant.
	FunctionReference.SetSelfMember(FunctionReference.GetMemberName());
	TArray<UEdGraphPin*> OriginalPins = Pins;
	for (const UEdGraphPin* Pin : OriginalPins)
	{
		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			CreateUserDefinedPin(Pin->PinName, Pin->PinType, Pin->Direction, false);
		}
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->ReconstructNode(*this, true);
}

void UK2Node_FunctionTerminator::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->PinType.bIsWeakPointer && !Pin->PinType.IsContainer())
		{
			const FString ErrorString = FText::Format(
				LOCTEXT("WeakPtrNotSupportedErrorFmt", "Weak pointers are not supported as function parameters. Pin '{0}' @@"),
				FText::FromString(Pin->GetName())
			).ToString();
			MessageLog.Error(*ErrorString, this);
		}
	}
}

#undef LOCTEXT_NAMESPACE
