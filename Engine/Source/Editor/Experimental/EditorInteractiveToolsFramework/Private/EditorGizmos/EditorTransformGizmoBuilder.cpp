// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformGizmoBuilder.h"
#include "EditorGizmos/EditorTransformGizmo.h"
#include "EditorGizmos/EditorTransformGizmoSource.h"
#include "EditorGizmos/EditorTransformProxy.h"
#include "BaseGizmos/GizmoElementGroup.h"

UInteractiveGizmo* UEditorTransformGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UEditorTransformGizmo* TransformGizmo = NewObject<UEditorTransformGizmo>(SceneState.GizmoManager);
	TransformGizmo->Setup();
	TransformGizmo->TransformGizmoSource = UEditorTransformGizmoSource::Construct(TransformGizmo);

	// @todo: Gizmo element construction to be moved here from UTransformGizmo.
	// A UGizmoElementRenderMultiTarget will be constructed and both the
	// render and hit target's Construct methods will take the gizmo element root as input.
	TransformGizmo->HitTarget = UGizmoElementHitMultiTarget::Construct(TransformGizmo->GizmoElementRoot);

	return TransformGizmo;
}

void UEditorTransformGizmoBuilder::UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState)
{
	if (UTransformGizmo* TransformGizmo = Cast<UTransformGizmo>(Gizmo))
	{
		UEditorTransformProxy* TransformProxy = NewObject<UEditorTransformProxy>();
		TransformGizmo->SetActiveTarget(TransformProxy);
		TransformGizmo->SetVisibility(true);
	}
}
