// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.


#include "UnrealEd.h"
#include "EditorViewportCommands.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"

#define LOCTEXT_NAMESPACE "EditorViewportCommands"

void FEditorViewportCommands::RegisterCommands()
{
	UI_COMMAND( Perspective, "Perspective", "Switches the viewport to perspective view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::G ) );
	UI_COMMAND( Front, "Front", "Switches the viewport to front view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::H ) );
	UI_COMMAND( Back, "Back", "Switches the viewport to back view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt | EModifierKey::Shift, EKeys::H ) );
	UI_COMMAND( Top, "Top", "Switches the viewport to top view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::J ) );
	UI_COMMAND( Bottom, "Bottom", "Switches the viewport to bottom view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt | EModifierKey::Shift, EKeys::J ) );
	UI_COMMAND( Left, "Left", "Switches the viewport to left view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::K ) );
	UI_COMMAND( Right, "Right", "Switches the viewport to right view", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt | EModifierKey::Shift, EKeys::K ) );
	UI_COMMAND( Next, "Next", "Rotate through each view options", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Control | EModifierKey::Shift, EKeys::SpaceBar ) );

	UI_COMMAND( WireframeMode, "Brush Wireframe View Mode", "Renders the scene in brush wireframe", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Two ) );
	UI_COMMAND( UnlitMode, "Unlit View Mode", "Renders the scene with no lights", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Three ) );
	UI_COMMAND( LitMode, "Lit View Mode", "Renders the scene with normal lighting", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Four ) );
	UI_COMMAND( DetailLightingMode, "Detail Lighting View Mode", "Renders the scene with detailed lighting only", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Five ) );
	UI_COMMAND( LightingOnlyMode, "Lighting Only View Mode", "Renders the scene with lights only, no textures", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Six ) );
	UI_COMMAND( LightComplexityMode, "Light Complexity View Mode", "Renders the scene with light complexity visualization", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Seven ) );
	UI_COMMAND( ShaderComplexityMode, "Shader Complexity View Mode", "Renders the scene with shader complexity visualization", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Eight ) );
	UI_COMMAND( QuadOverdrawMode, "Quad Complexity View Mode", "Renders the scene with quad complexity visualization", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( ShaderComplexityWithQuadOverdrawMode, "Shader Complexity & Quads visualization", "Renders the scene with shader complexity and quad overdraw visualization", EUserInterfaceActionType::RadioButton, FInputChord() );

	UI_COMMAND( TexStreamAccPrimitiveDistanceMode, "Primitive Distance Accuracy View Mode", "Visualize the accuracy of the primitive distance computed for texture streaming", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( TexStreamAccMeshUVDensityMode, "Mesh UV Densities Accuracy View Mode", "Visualize the accuracy of the mesh UV densities computed for texture streaming", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( TexStreamAccMeshUVDensityAll, "All UV Channels", "Visualize the densities accuracy of all UV channels", EUserInterfaceActionType::RadioButton, FInputChord() );

	for (int32 TexCoordIndex = 0; TexCoordIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++TexCoordIndex)
	{
		const FName CommandName = *FString::Printf(TEXT("ShowUVChannel%d"), TexCoordIndex);
		const FText LocalizedName = FText::Format(LOCTEXT("ShowTexCoordCommands", "UV Channel {0}"), TexCoordIndex);
		const FText LocalizedTooltip = FText::Format(LOCTEXT("ShowTexCoordCommands_ToolTip","Visualize the size accuracy of UV density for channel {0}"), TexCoordIndex);

		TexStreamAccMeshUVDensitySingle[TexCoordIndex]
			= FUICommandInfoDecl(this->AsShared(), CommandName, LocalizedName, LocalizedTooltip)
				.UserInterfaceType(EUserInterfaceActionType::RadioButton);
	}

	UI_COMMAND( TexStreamAccMaterialTextureScaleMode, "Material Texture Scales Accuracy View Mode", "Visualize the accuracy of the material texture scales used for texture streaming", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( TexStreamAccMaterialTextureScaleAll, "All Textures", "Visualize the scales accuracy of all textures", EUserInterfaceActionType::RadioButton, FInputChord() );

	for (int32 TextureIndex = 0; TextureIndex < TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL; ++TextureIndex)
	{
		const FName CommandName = *FString::Printf(TEXT("ShowTexture%d"), TextureIndex);
		const FText LocalizedName = FText::Format(LOCTEXT("ShowTextureCommands", "Texture {0}"), TextureIndex);
		const FText LocalizedTooltip = FText::Format(LOCTEXT("ShowTextureCommandss_ToolTip", "Visualize the scale accuracy of texture {0}"), TextureIndex);

		TexStreamAccMaterialTextureScaleSingle[TextureIndex]
			= FUICommandInfoDecl(this->AsShared(), CommandName, LocalizedName, LocalizedTooltip)
				.UserInterfaceType(EUserInterfaceActionType::RadioButton);
	}

	UI_COMMAND( StationaryLightOverlapMode, "Stationary Light Overlap View Mode", "Visualizes overlap of stationary lights", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( LightmapDensityMode, "Lightmap Density View Mode", "Renders the scene with lightmap density visualization", EUserInterfaceActionType::RadioButton, FInputChord( EModifierKey::Alt, EKeys::Zero ) );

	UI_COMMAND( GroupLODColorationMode, "Level of Detail Coloration View Mode", "Renders the scene using Level of Detail visualization", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND( LODColorationMode, "LOD Coloration View Mode", "Renders the scene using LOD color visualization", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( HLODColorationMode, "HLOD Coloration View Mode", "Renders the scene using HLOD color visualization", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND( VisualizeBufferMode, "Buffer Visualization View Mode", "Renders a set of selected post process materials, which visualize various intermediate render buffers (material attributes)", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND( ReflectionOverrideMode, "Reflections View Mode", "Renders the scene with reflections only", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( CollisionPawn, "Player Collision", "Renders player collision visualization", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( CollisionVisibility, "Visibility Collision", "Renders visibility collision visualization", EUserInterfaceActionType::RadioButton, FInputChord() );

	UI_COMMAND( ToggleRealTime, "Realtime", "Toggles real time rendering in this viewport", EUserInterfaceActionType::ToggleButton, FInputChord( EModifierKey::Control, EKeys::R ) );
	UI_COMMAND( ToggleStats, "Show Stats", "Toggles the ability to show stats in this viewport (enables realtime)", EUserInterfaceActionType::ToggleButton, FInputChord( EModifierKey::Shift, EKeys::L ) );
	UI_COMMAND( ToggleFPS, "Show FPS", "Toggles showing frames per second in this viewport (enables realtime)", EUserInterfaceActionType::ToggleButton, FInputChord( EModifierKey::Control|EModifierKey::Shift, EKeys::H ) );

	UI_COMMAND( ScreenCapture, "Screen Capture", "Take a screenshot of the active viewport.", EUserInterfaceActionType::Button, FInputChord(EKeys::F9) );
	UI_COMMAND( ScreenCaptureForProjectThumbnail, "Update Project Thumbnail", "Take a screenshot of the active viewport for use as the project thumbnail.", EUserInterfaceActionType::Button, FInputChord() );

	UI_COMMAND( IncrementPositionGridSize, "Grid Size (Position): Increment", "Increases the position grid size setting by one", EUserInterfaceActionType::Button, FInputChord( EKeys::RightBracket ) );
	UI_COMMAND( DecrementPositionGridSize, "Grid Size (Position): Decrement", "Decreases the position grid size setting by one", EUserInterfaceActionType::Button, FInputChord( EKeys::LeftBracket ) );
	UI_COMMAND( IncrementRotationGridSize, "Grid Size (Rotation): Increment", "Increases the rotation grid size setting by one", EUserInterfaceActionType::Button, FInputChord( EModifierKey::Shift, EKeys::RightBracket ) );
	UI_COMMAND( DecrementRotationGridSize, "Grid Size (Rotation): Decrement", "Decreases the rotation grid size setting by one", EUserInterfaceActionType::Button, FInputChord( EModifierKey::Shift, EKeys::LeftBracket ) );

	
	UI_COMMAND( TranslateMode, "Translate Mode", "Select and translate objects", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::W) );
	UI_COMMAND( RotateMode, "Rotate Mode", "Select and rotate objects", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::E) );
	UI_COMMAND( ScaleMode, "Scale Mode", "Select and scale objects", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::R) );
	UI_COMMAND( TranslateRotateMode, "Combined Translate and Rotate Mode", "Select and translate or rotate objects", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND( TranslateRotate2DMode, "2D Mode", "Select and translate or rotate objects in 2D", EUserInterfaceActionType::ToggleButton, FInputGesture());

	UI_COMMAND( ShrinkTransformWidget, "Shrink Transform Widget", "Shrink the level editor transform widget", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::LeftBracket) );
	UI_COMMAND( ExpandTransformWidget, "Expand Transform Widget", "Expand the level editor transform widget", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::RightBracket) );

	UI_COMMAND( RelativeCoordinateSystem_World, "World-relative Transform", "Move and rotate objects relative to the cardinal world axes", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( RelativeCoordinateSystem_Local, "Local-relative Transform", "Move and rotate objects relative to the object's local axes", EUserInterfaceActionType::RadioButton, FInputChord() );

#if PLATFORM_MAC
	UI_COMMAND( CycleTransformGizmoCoordSystem, "Cycle Transform Coordinate System", "Cycles the transform gizmo coordinate systems between world and local (object) space", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Command, EKeys::Tilde));
#else
	UI_COMMAND( CycleTransformGizmoCoordSystem, "Cycle Transform Coordinate System", "Cycles the transform gizmo coordinate systems between world and local (object) space", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::Tilde));
#endif
	UI_COMMAND( CycleTransformGizmos, "Cycle Between Translate, Rotate, and Scale", "Cycles the transform gizmos between translate, rotate, and scale", EUserInterfaceActionType::Button, FInputChord(EKeys::SpaceBar) );
	
	UI_COMMAND( FocusViewportToSelection, "Focus Selected", "Moves the camera in front of the selection", EUserInterfaceActionType::Button, FInputChord( EKeys::F ) );

	UI_COMMAND( LocationGridSnap, "Grid Snap", "Enables or disables snapping to the grid when dragging objects around", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND( RotationGridSnap, "Rotation Snap", "Enables or disables snapping objects to a rotation grid", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND( Layer2DSnap, "Layer2D Snap", "Enables or disables snapping objects to a 2D layer", EUserInterfaceActionType::ToggleButton, FInputGesture());
	UI_COMMAND( ScaleGridSnap, "Scale Snap", "Enables or disables snapping objects to a scale grid", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND( SurfaceSnapping, "Surface Snapping", "If enabled, actors will snap to surfaces in the world when dragging", EUserInterfaceActionType::ToggleButton, FInputChord() );

	UI_COMMAND( ToggleAutoExposure, "Automatic (Default in-game)", "Enable automatic expose", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure4m, "Fixed Exposure: -4", "Set the fixed exposure to -4", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure3m, "Fixed Exposure: -3", "Set the fixed exposure to -3", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure2m, "Fixed Exposure: -2", "Set the fixed exposure to -2", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure1m, "Fixed Exposure: -1", "Set the fixed exposure to -1", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure0, "Fixed Exposure: 0 (Indoor)", "Set the fixed exposure to 0", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure1p, "Fixed Exposure: +1", "Set the fixed exposure to 1", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure2p, "Fixed Exposure: +2", "Set the fixed exposure to 2", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure3p, "Fixed Exposure: +3", "Set the fixed exposure to 3", EUserInterfaceActionType::RadioButton, FInputChord() );
	UI_COMMAND( FixedExposure4p, "Fixed Exposure: +4", "Set the fixed exposure to 4", EUserInterfaceActionType::RadioButton, FInputChord() );
}

#undef LOCTEXT_NAMESPACE

#define LOCTEXT_NAMESPACE "EditorViewModeOptionsMenu"

FText GetViewModeOptionsMenuLabel(EViewModeIndex ViewModeIndex)
{
	if (ViewModeIndex == VMI_MeshUVDensityAccuracy)
	{
		return LOCTEXT("ViewParamMenuTitle_UVChannels", "UV Channels");
	}
	else if (ViewModeIndex == VMI_MaterialTextureScaleAccuracy)
	{
		// Get form the content browser
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<FAssetData> SelectedAssetData;
			ContentBrowserModule.Get().GetSelectedAssets(SelectedAssetData);

			for ( auto AssetIt = SelectedAssetData.CreateConstIterator(); AssetIt; ++AssetIt )
			{
				// Grab the object if it is loaded
				if (AssetIt->IsAssetLoaded())
				{
					const UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetIt->GetAsset());
					if (MaterialInterface)
					{
						return LOCTEXT("ViewParamMenuTitle_TexturesFromContentBrowser", "Textures (Content Browser)");
					}
				}
			}
		}

		// Get form the selection
		{
			TArray<UPrimitiveComponent*> SelectedComponents;
			GEditor->GetSelectedComponents()->GetSelectedObjects(SelectedComponents);

			TArray<AActor*> SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects(SelectedActors);
			for (AActor* Actor : SelectedActors)
			{
				TArray<UPrimitiveComponent*> ActorComponents;
				Actor->GetComponents(ActorComponents);
				SelectedComponents.Append(ActorComponents);
			}

			for (const UPrimitiveComponent* SelectedComponent : SelectedComponents)
			{
				if (SelectedComponent)
				{
					const int32 NumMaterials = SelectedComponent->GetNumMaterials();
					for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
					{
						const UMaterialInterface* MaterialInterface = SelectedComponent->GetMaterial(MaterialIndex);
						if (MaterialInterface)
						{
							return LOCTEXT("ViewParamMenuTitle_TexturesFromSceneSelection", "Textures (Scene Selection)");
						}
					}
				}
			}
		}
		
		return LOCTEXT("ViewParamMenuTitle_Textures", "Textures");
	}
	else
	{

	}
	return LOCTEXT("ViewParamMenuTitle", "View Mode Options");
}

TSharedRef<SWidget> BuildViewModeOptionsMenu(TSharedPtr<FUICommandList> CommandList, EViewModeIndex ViewModeIndex)
{
	const FEditorViewportCommands& Commands = FEditorViewportCommands::Get();
	FMenuBuilder MenuBuilder( true, CommandList );

	if (ViewModeIndex == VMI_MeshUVDensityAccuracy)
	{
		MenuBuilder.AddMenuEntry(Commands.TexStreamAccMeshUVDensityAll, NAME_None, LOCTEXT("TexStreamAccMeshUVDensityAllDisplayName", "All UV Channels"));

		const FString MenuName = LOCTEXT("TexStreamAccMeshUVDensitySingleDisplayName", "UV Channel ").ToString();
		for (int32 TexCoordIndex = 0; TexCoordIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++TexCoordIndex)
		{
			MenuBuilder.AddMenuEntry(Commands.TexStreamAccMeshUVDensitySingle[TexCoordIndex], NAME_None, FText::FromString(FString::Printf(TEXT("%s %d"), *MenuName, TexCoordIndex)));
		}
	}
	else if (ViewModeIndex == VMI_MaterialTextureScaleAccuracy)
	{
		MenuBuilder.AddMenuEntry(Commands.TexStreamAccMaterialTextureScaleAll, NAME_None, LOCTEXT("TexStreamAccMaterialTextureScaleAllDisplayName", "All Textures"));
		const FString MenuName = LOCTEXT("TexStreamAccMaterialTextureScaleSingleDisplayName", "Texture").ToString();

		TArray<const UMaterialInterface*> SelectedMaterials;

		// Get selected materials form the content browser
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			TArray<FAssetData> SelectedAssetData;
			ContentBrowserModule.Get().GetSelectedAssets(SelectedAssetData);

			for ( auto AssetIt = SelectedAssetData.CreateConstIterator(); AssetIt; ++AssetIt )
			{
				// Grab the object if it is loaded
				if (AssetIt->IsAssetLoaded())
				{
					const UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetIt->GetAsset());
					if (MaterialInterface)
					{
						SelectedMaterials.AddUnique(MaterialInterface);
					}
				}
			}
		}

		// Get selected materials from component selection and actors
		if (!SelectedMaterials.Num())
		{
			TArray<UPrimitiveComponent*> SelectedComponents;
			GEditor->GetSelectedComponents()->GetSelectedObjects(SelectedComponents);

			TArray<AActor*> SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects(SelectedActors);
			for (AActor* Actor : SelectedActors)
			{
				TArray<UPrimitiveComponent*> ActorComponents;
				Actor->GetComponents(ActorComponents);
				SelectedComponents.Append(ActorComponents);
			}

			for (const UPrimitiveComponent* SelectedComponent : SelectedComponents)
			{
				if (SelectedComponent)
				{
					const int32 NumMaterials = SelectedComponent->GetNumMaterials();
					for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
					{
						const UMaterialInterface* MaterialInterface = SelectedComponent->GetMaterial(MaterialIndex);
						if (MaterialInterface)
						{
							SelectedMaterials.AddUnique(MaterialInterface);
						}
					}
				}
			}
		}

		TArray<FString> TextureDataAtIndex[TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL];
		for (const UMaterialInterface* MaterialInterface : SelectedMaterials)
		{
			if (MaterialInterface)
			{
				for (const FMaterialTextureInfo& TextureData : MaterialInterface->GetTextureStreamingData())
				{
					if (TextureData.IsValid(true))
					{
						if (SelectedMaterials.Num() == 1)
						{
							TextureDataAtIndex[TextureData.TextureIndex].AddUnique(FString::Printf(TEXT("%.2f X UV%d : %s"), TextureData.SamplingScale, TextureData.UVChannelIndex, *TextureData.TextureName.ToString()));
						}
						else
						{
							TextureDataAtIndex[TextureData.TextureIndex].AddUnique(FString::Printf(TEXT("%.2f X UV%d : %s.%s"), TextureData.SamplingScale, TextureData.UVChannelIndex, *MaterialInterface->GetName(), *TextureData.TextureName.ToString()));
						}
					}
				}
			}
		}

		for (int32 TextureIndex = 0; TextureIndex < TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL; ++TextureIndex)
		{
			if (!SelectedMaterials.Num())
			{
				MenuBuilder.AddMenuEntry(Commands.TexStreamAccMaterialTextureScaleSingle[TextureIndex], NAME_None, FText::FromString(FString::Printf(TEXT("%s %d"), *MenuName, TextureIndex)));
			}
			else if (TextureDataAtIndex[TextureIndex].Num())
			{
				if (TextureDataAtIndex[TextureIndex].Num() == 1)
				{
					const FString NameOverride = FString::Printf(TEXT("%s %d (%s)"), *MenuName, TextureIndex, *TextureDataAtIndex[TextureIndex][0]);
					MenuBuilder.AddMenuEntry(Commands.TexStreamAccMaterialTextureScaleSingle[TextureIndex], NAME_None, FText::FromString(NameOverride));

				}
				else
				{
					const FString NameOverride = FString::Printf(TEXT("%s %d (%s) ..."), *MenuName, TextureIndex, *TextureDataAtIndex[TextureIndex][0]);
					FString ToolTipOverride = TextureDataAtIndex[TextureIndex][0];
					for (int32 Index = 1; Index < TextureDataAtIndex[TextureIndex].Num(); ++Index)
					{
						ToolTipOverride = FString::Printf(TEXT("%s\n%s"), *ToolTipOverride, *TextureDataAtIndex[TextureIndex][Index]);
					}
					MenuBuilder.AddMenuEntry(Commands.TexStreamAccMaterialTextureScaleSingle[TextureIndex], NAME_None, FText::FromString(NameOverride), FText::FromString(ToolTipOverride));
				}
			}
		}
	}
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
