// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Delegates/IDelegateInstance.h"
#include "ISequencerModule.h"
#include "Modules/ModuleManager.h"

#include "ISequenceRecorder.h"
#include "MediaCompositingEditorStyle.h"
#include "MediaSequenceRecorderExtender.h"
#include "MediaTrackEditor.h"


#define LOCTEXT_NAMESPACE "MediaCompositingEditorModule"


/**
 * Implements the MediaCompositing module.
 */
class FMediaCompositingEditorModule
	: public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FMediaCompositingEditorStyle::Get();

		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		TrackEditorBindingHandle = SequencerModule.RegisterPropertyTrackEditor<FMediaTrackEditor>();

		ISequenceRecorder& SequenceRecorder = FModuleManager::LoadModuleChecked<ISequenceRecorder>("SequenceRecorder");
		RecorderExtender = MakeShared<FMediaSequenceRecorderExtender>();
		SequenceRecorder.AddSequenceRecorderExtender(RecorderExtender);
	}
	
	virtual void ShutdownModule() override
	{
		ISequenceRecorder* SequenceRecorder = FModuleManager::Get().GetModulePtr<ISequenceRecorder>("SequenceRecorder");
		if (SequenceRecorder && RecorderExtender.IsValid())
		{
			SequenceRecorder->RemoveSequenceRecorderExtender(RecorderExtender);
		}
		RecorderExtender.Reset();

		FMediaCompositingEditorStyle::Destroy();

		ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");	
		
		if (SequencerModulePtr)
		{
			SequencerModulePtr->UnRegisterTrackEditor(TrackEditorBindingHandle);
		}
	}

	FDelegateHandle TrackEditorBindingHandle;
	TSharedPtr<FMediaSequenceRecorderExtender> RecorderExtender;
};


IMPLEMENT_MODULE(FMediaCompositingEditorModule, MediaCompositingEditor);


#undef LOCTEXT_NAMESPACE
