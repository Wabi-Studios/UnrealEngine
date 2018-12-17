// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneCaptureProtocolBase.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Slate/SceneViewport.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"

#define LOCTEXT_NAMESPACE "MovieSceneCaptureProtocol"

UMovieSceneCaptureProtocolBase::UMovieSceneCaptureProtocolBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	State = EMovieSceneCaptureProtocolState::Idle;
	CaptureHost = nullptr;
}

bool UMovieSceneCaptureProtocolBase::Setup(const FCaptureProtocolInitSettings& InSettings, const ICaptureProtocolHost* Host)
{
	InitSettings = InSettings;
	CaptureHost = Host;

	switch (State)
	{
	case EMovieSceneCaptureProtocolState::Capturing:
		BeginFinalize();
		// fallthrough
	case EMovieSceneCaptureProtocolState::Finalizing:
		Finalize();
		// fallthrough
	default:
		break;
	}

	State = EMovieSceneCaptureProtocolState::Idle;
	if (!SetupImpl())
	{
		return false;
	}

	State = EMovieSceneCaptureProtocolState::Initialized;
	return true;
}

UWorld* UMovieSceneCaptureProtocolBase::GetWorld() const
{
	if (InitSettings.IsSet())
	{
		// Retrieve the world from the Scene Viewport client.
		return InitSettings->SceneViewport->GetClient()->GetWorld();
	}

	// Otherwise we don't have a world yet - we might be instances created in the
	// UI that aren't tied to the world yet. When Setup is called then the world
	// will be available. We don't want to rely on the Outer for the world as it requires
	// reinitializing the UMovieSceneCaptureProtocolBase just to change the outer.
	return nullptr;
}

void UMovieSceneCaptureProtocolBase::WarmUp()
{
	if (State == EMovieSceneCaptureProtocolState::Capturing)
	{
		PauseCaptureImpl();
	}

	if (State == EMovieSceneCaptureProtocolState::Capturing || State == EMovieSceneCaptureProtocolState::Initialized)
	{
		State = EMovieSceneCaptureProtocolState::Initialized;
		WarmUpImpl();
	}
}

bool UMovieSceneCaptureProtocolBase::StartCapture()
{
	if (State == EMovieSceneCaptureProtocolState::Idle)
	{
		return false;
	}
	else if (State == EMovieSceneCaptureProtocolState::Capturing)
	{
		return true;
	}

	ensure(State == EMovieSceneCaptureProtocolState::Initialized);

	State = EMovieSceneCaptureProtocolState::Capturing;

	if (!StartCaptureImpl())
	{
		State = EMovieSceneCaptureProtocolState::Initialized;
		return false;
	}
	return true;
}

void UMovieSceneCaptureProtocolBase::CaptureFrame(const FFrameMetrics& FrameMetrics)
{
	if (State == EMovieSceneCaptureProtocolState::Capturing)
	{
		bFrameRequested = true;
		CaptureFrameImpl(FrameMetrics);
	}
}

bool UMovieSceneCaptureProtocolBase::HasFinishedProcessing() const
{
	return !bFrameRequested && HasFinishedProcessingImpl();
}

void UMovieSceneCaptureProtocolBase::PreTick()
{
	bFrameRequested = false;
	PreTickImpl();
}

void UMovieSceneCaptureProtocolBase::Tick()
{
	TickImpl();
}

void UMovieSceneCaptureProtocolBase::BeginFinalize()
{
	if (State == EMovieSceneCaptureProtocolState::Idle)
	{
		return;
	}

	if (State == EMovieSceneCaptureProtocolState::Capturing)
	{
		PauseCaptureImpl();
	}

	State = EMovieSceneCaptureProtocolState::Finalizing;
	BeginFinalizeImpl();
}

void UMovieSceneCaptureProtocolBase::Finalize()
{
	if (State != EMovieSceneCaptureProtocolState::Finalizing)
	{
		BeginFinalize();
	}

	if (State == EMovieSceneCaptureProtocolState::Finalizing)
	{
		State = EMovieSceneCaptureProtocolState::Idle;
		FinalizeImpl();
	}
}

void UMovieSceneCaptureProtocolBase::AddFormatMappings(TMap<FString, FStringFormatArg>& FormatMappings) const
{
	AddFormatMappingsImpl(FormatMappings);
}

void UMovieSceneCaptureProtocolBase::OnReleaseConfig(FMovieSceneCaptureSettings& InSettings)
{
	OnReleaseConfigImpl(InSettings);
}

void UMovieSceneCaptureProtocolBase::OnLoadConfig(FMovieSceneCaptureSettings& InSettings)
{
	OnLoadConfigImpl(InSettings);
}

bool UMovieSceneCaptureProtocolBase::CanWriteToFile(const TCHAR* InFilename, bool bOverwriteExisting) const
{
	return CanWriteToFileImpl(InFilename, bOverwriteExisting);
}

bool UMovieSceneCaptureProtocolBase::CanWriteToFileImpl(const TCHAR* InFilename, bool bOverwriteExisting) const
{
	return bOverwriteExisting || IFileManager::Get().FileSize(InFilename) == -1;
}

FCaptureProtocolInitSettings FCaptureProtocolInitSettings::FromSlateViewport(TSharedRef<FSceneViewport> InSceneViewport)
{
	FCaptureProtocolInitSettings Settings;
	Settings.SceneViewport = InSceneViewport;
	Settings.DesiredSize = InSceneViewport->GetSize();

	// hack for FORT-94554
	if (Settings.DesiredSize == FIntPoint::ZeroValue)
	{
        ensureAlwaysMsgf(false, TEXT("Invalid viewport size settings in FCaptureProtocalInitSettings::FromSlateViewport! Using default size! (1920x1080)"));
		Settings.DesiredSize.X = 1920;
		Settings.DesiredSize.Y = 1080;
		InSceneViewport->SetViewportSize(Settings.DesiredSize.X, Settings.DesiredSize.Y);
	}
	// end hack

	return Settings;
}

void UMovieSceneCaptureProtocolBase::EnsureFileWritableImpl(const FString& File) const
{
	FString Directory = FPaths::GetPath(File);

	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		IFileManager::Get().MakeDirectory(*Directory);
	}

	if (CaptureHost->GetSettings().bOverwriteExisting)
	{
		// Try and delete it first
		while (IFileManager::Get().FileSize(*File) != -1 && !FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*File))
		{
			// Pop up a message box
			FText MessageText = FText::Format(LOCTEXT("UnableToRemoveFile_Format", "The destination file '{0}' could not be deleted because it's in use by another application.\n\nPlease close this application before continuing."), FText::FromString(File));
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *MessageText.ToString(), *LOCTEXT("UnableToRemoveFile", "Unable to remove file").ToString());
		}
	}
}

FString UMovieSceneCaptureProtocolBase::GenerateFilenameImpl(const FFrameMetrics& FrameMetrics, const TCHAR* Extension, const FString* NameFormatString) const
{
	FString OutputDirectoryPath = CaptureHost->GetSettings().OutputDirectory.Path;
	FPaths::NormalizeFilename(OutputDirectoryPath);

	if (!NameFormatString)
	{
		NameFormatString = &CaptureHost->GetSettings().OutputFormat;
	}
	
	const FString BaseFilename = CaptureHost->ResolveFileFormat(OutputDirectoryPath, FrameMetrics) / CaptureHost->ResolveFileFormat(*NameFormatString, FrameMetrics);
	
	FString ThisTry = BaseFilename + Extension;
	
	if (CanWriteToFile(*ThisTry, CaptureHost->GetSettings().bOverwriteExisting))
	{
		return ThisTry;
	}
	
	int32 DuplicateIndex = 2;
	for (;;)
	{
		ThisTry = BaseFilename + FString::Printf(TEXT("_(%d)"), DuplicateIndex) + Extension;
	
		// If the file doesn't exist, we can use that, else, increment the index and try again
		if (CanWriteToFile(*ThisTry, CaptureHost->GetSettings().bOverwriteExisting))
		{
			return ThisTry;
		}
	
		++DuplicateIndex;
	}
	
	return ThisTry;
}

#undef LOCTEXT_NAMESPACE // "MovieSceneCaptureProtocol"