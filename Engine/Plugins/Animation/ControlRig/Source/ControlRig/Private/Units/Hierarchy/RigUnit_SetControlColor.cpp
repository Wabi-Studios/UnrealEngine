// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_SetControlColor.h"
#include "Units/RigUnitContext.h"

FRigUnit_GetControlColor_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

    Color = FLinearColor::Black;

	const URigHierarchy* Hierarchy = Context.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex.UpdateCache(FRigElementKey(Control, ERigElementType::Control), Hierarchy))
				{
					const FRigControlElement* ControlElement = Hierarchy->GetChecked<FRigControlElement>(CachedControlIndex);
					Color = ControlElement->Settings.ShapeColor;
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}

FRigUnit_SetControlColor_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedControlIndex.Reset();
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedControlIndex.UpdateCache(FRigElementKey(Control, ERigElementType::Control), Hierarchy))
				{
					FRigControlElement* ControlElement = Hierarchy->GetChecked<FRigControlElement>(CachedControlIndex);
					ControlElement->Settings.ShapeColor = Color;
					Hierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}
