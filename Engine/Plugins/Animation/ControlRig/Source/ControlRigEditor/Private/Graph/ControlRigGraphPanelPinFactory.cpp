// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphPanelPinFactory.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/SControlRigGraphPinNameList.h"
#include "Graph/SControlRigGraphPinCurveFloat.h"
#include "Graph/SControlRigGraphPinVariableName.h"
#include "Graph/SControlRigGraphPinVariableBinding.h"
#include "KismetPins/SGraphPinExec.h"
#include "SGraphPinComboBox.h"
#include "ControlRig.h"
#include "NodeFactory.h"
#include "EdGraphSchema_K2.h"
#include "Curves/CurveFloat.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "IPropertyAccessEditor.h"

TSharedPtr<SGraphPin> FControlRigGraphPanelPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (InPin)
	{
		if (const UEdGraphNode* OwningNode = InPin->GetOwningNode())
		{
			// only create pins within control rig graphs
			if (Cast<UControlRigGraph>(OwningNode->GetGraph()) == nullptr)
			{
				return nullptr;
			}
		}

		if (UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InPin->GetOwningNode()))
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(RigNode->GetGraph());

			if (URigVMPin* ModelPin = RigNode->GetModelPinFromPinPath(InPin->GetName()))
			{
				if (ModelPin->IsBoundToVariable())
				{
					if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(RigGraph->GetOuter()))
					{
						return SNew(SControlRigGraphPinVariableBinding, InPin)
							.ModelPins({ModelPin})
							.Blueprint(Blueprint);
					}
				}

				FName CustomWidgetName = ModelPin->GetCustomWidgetName();
				if (CustomWidgetName == TEXT("BoneName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetBoneNameList);
				}
				else if (CustomWidgetName == TEXT("ControlName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetControlNameList);
				}
				else if (CustomWidgetName == TEXT("SpaceName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetNullNameList);
				}
				else if (CustomWidgetName == TEXT("CurveName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetCurveNameList);
				}
				else if (CustomWidgetName == TEXT("ElementName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetElementNameList);
				}
				else if (CustomWidgetName == TEXT("EntryName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetEntryNameList);
				}
				else if (CustomWidgetName == TEXT("DrawingName"))
				{
					return SNew(SControlRigGraphPinNameList, InPin)
						.ModelPin(ModelPin)
						.OnGetNameFromSelection_UObject(RigGraph, &UControlRigGraph::GetSelectedElementsNameList)
						.OnGetNameListContent_UObject(RigGraph, &UControlRigGraph::GetDrawingNameList);
				}
				else if (CustomWidgetName == TEXT("VariableName"))
				{
					return SNew(SControlRigGraphPinVariableName, InPin);
				}
			}

			if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				const UStruct* Struct = Cast<UStruct>(InPin->PinType.PinSubCategoryObject);
				if (Struct && Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					return SNew(SGraphPinExec, InPin);
				}
				else if (InPin->PinType.PinSubCategoryObject == FRuntimeFloatCurve::StaticStruct())
				{
					return SNew(SControlRigGraphPinCurveFloat, InPin);
				}
			}
		}

		TSharedPtr<SGraphPin> K2PinWidget = FNodeFactory::CreateK2PinWidget(InPin);
		if(K2PinWidget.IsValid())
		{
			if(InPin->Direction == EEdGraphPinDirection::EGPD_Input)
			{
				// if we are an enum pin - and we are inside a RigElementKey,
				// let's remove the "all" entry.
				if(InPin->PinType.PinSubCategoryObject == StaticEnum<ERigElementType>())
				{
					if(InPin->ParentPin)
					{
						if(InPin->ParentPin->PinType.PinSubCategoryObject == FRigElementKey::StaticStruct())
						{
							TSharedPtr<SWidget> ValueWidget = K2PinWidget->GetValueWidget();
							if(ValueWidget.IsValid())
							{
								if(TSharedPtr<SPinComboBox> EnumCombo = StaticCastSharedPtr<SPinComboBox>(ValueWidget))
								{
									if(EnumCombo.IsValid())
									{
										EnumCombo->RemoveItemByIndex(StaticEnum<ERigElementType>()->GetIndexByValue((int64)ERigElementType::All));
									}
								}
							}
						}
					}
				}
			}

			return K2PinWidget;
		}
	}

	return nullptr;
}
