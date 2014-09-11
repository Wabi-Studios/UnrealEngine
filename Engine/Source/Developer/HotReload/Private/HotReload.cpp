// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "HotReloadPrivatePCH.h"
#include "Editor/UnrealEd/Public/Kismet2/KismetReinstanceUtilities.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "EngineAnalytics.h"
#include "ScopedTimers.h"
#include "DesktopPlatformModule.h"

DEFINE_LOG_CATEGORY(LogHotReload);

#define LOCTEXT_NAMESPACE "HotReload"

/**
 * Module for HotReload support
 */
class FHotReloadModule : public IHotReloadModule, FSelfRegisteringExec
{
public:
	FHotReloadModule()
	{
		ModuleCompileReadPipe = nullptr;
		bRequestCancelCompilation = false;
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** FSelfRegisteringExec implementation */
	virtual bool Exec( UWorld* Inworld, const TCHAR* Cmd, FOutputDevice& Ar ) override;

	/** IHotReloadInterface implementation */
	virtual void Tick() override;
	virtual void SaveConfig() override;
	virtual bool RecompileModule( const FName InModuleName, const bool bReloadAfterRecompile, FOutputDevice &Ar ) override;
	virtual bool IsCurrentlyCompiling() const override { return ModuleCompileProcessHandle.IsValid(); }
	virtual void RequestStopCompilation() override { bRequestCancelCompilation = true; }
	virtual void AddHotReloadFunctionRemap(Native NewFunctionPointer, Native OldFunctionPointer) override;	
	virtual void RebindPackages(TArray< UPackage* > Packages, TArray< FName > DependentModules, const bool bWaitForCompletion, FOutputDevice &Ar) override;
	virtual void DoHotReloadFromEditor() override;
	virtual FHotReloadEvent& OnHotReload() override { return HotReloadEvent; }	
	virtual FModuleCompilerStartedEvent& OnModuleCompilerStarted() override { return ModuleCompilerStartedEvent; }
	virtual FModuleCompilerFinishedEvent& OnModuleCompilerFinished() override { return ModuleCompilerFinishedEvent; }
	virtual FString GetModuleCompileMethod(FName InModuleName) override;
	virtual bool IsAnyGameModuleLoaded() const override;

private:
	/**
	 * Enumerates compilation methods for modules.
	 */
	enum class EModuleCompileMethod
	{
		Runtime,
		External,
		Unknown
	};

	/**
	 * Helper structure to hold on to module state while asynchronously recompiling DLLs
	 */
	struct FModuleToRecompile
	{
		/** Name of the module */
		FString ModuleName;

		/** Desired module file name suffix, or empty string if not needed */
		FString ModuleFileSuffix;

		/** The module file name to use after a compilation succeeds, or an empty string if not changing */
		FString NewModuleFilename;
	};

	/**
	 * Helper structure to store the compile time and method for a module
	 */
	struct FModuleCompilationData
	{
		/** Has a timestamp been set for the .dll file */
		bool bHasFileTimeStamp;

		/** Last known timestamp for the .dll file */
		FDateTime FileTimeStamp;

		/** Last known compilation method of the .dll file */
		EModuleCompileMethod CompileMethod;

		FModuleCompilationData()
			: bHasFileTimeStamp(false)
			, CompileMethod(EModuleCompileMethod::Unknown)
		{ }
	};

	/**
	 * Adds a callback to directory watcher for the game binaries folder.
	 */
	void InitHotReloadWatcher();

	/**
	 * Removes a directory watcher callback
	 */
	void ShutdownHotReloadWatcher();

	/**
	 * Performs hot-reload from IDE (when game DLLs change)
	 */
	void DoHotReloadFromIDE();

	/**
	* Performs internal module recompilation
	*/
	ECompilationResult::Type RebindPackagesInternal(TArray< UPackage* > Packages, TArray< FName > DependentModules, const bool bWaitForCompletion, FOutputDevice &Ar);

	/**
	 * Does the actual hot-reload, unloads old modules, loads new ones
	 */
	ECompilationResult::Type DoHotReloadInternal(bool bRecompileFinished, bool bRecompileSucceeded, TArray<UPackage*> Packages, TArray<FName> InDependentModules, FOutputDevice &HotReloadAr);

	/**
	* Callback for async ompilation
	*/
	void DoHotReloadCallback(bool bRecompileFinished, bool bRecompileSucceeded, TArray<UPackage*> Packages, TArray<FName> InDependentModules, FOutputDevice &HotReloadAr);

	/**
	 * Gets all currently loaded game module names.
	 */
	void GetGameModules(TArray<FString>& OutGameModules);

	/**
	 * Gets packages to re-bind and dependent modules.
	 */
	void GetPackagesToRebindAndDependentModules(const TArray<FString>& InGameModuleNames, TArray<UPackage*>& OutPackagesToRebind, TArray<FName>& OutDependentModules);

	/**
	 * Called from CoreUObject to re-instance hot-reloaded classes
	 */
	void ReinstanceClass(UClass* OldClass, UClass* NewClass);

	/**
	 * Tick function for FTicker: checks for re-loaded modules and does hot-reload from IDE
	 */
	bool Tick(float DeltaTime);

	/**
	 * Directory watcher callback
	 */
	void OnHotReloadBinariesChanged(const TArray<struct FFileChangeData>& FileChanges);

	/**
	 * Broadcasts that a hot reload just finished. 
	 *
	 * @param	bWasTriggeredAutomatically	True if the hot reload was invoked automatically by the hot reload system after detecting a changed DLL
	 */
	void BroadcastHotReload( bool bWasTriggeredAutomatically ) 
	{ 
		HotReloadEvent.Broadcast( bWasTriggeredAutomatically ); 
	}

	/**
	 * Sends analytics event about the re-load
	 */
	static void RecordAnalyticsEvent(const TCHAR* ReloadFrom, ECompilationResult::Type Result, double Duration, int32 PackageCount, int32 DependentModulesCount);

	/**
	 * Declares a type of delegates that is executed after a module recompile has finished.
	 *
	 * The first argument signals whether compilation has finished.
	 * The second argument shows whether compilation was successful or not.
	 */
	DECLARE_DELEGATE_TwoParams( FRecompileModulesCallback, bool, bool );

	/**
	 * Tries to recompile the specified modules in the background.  When recompiling finishes, the specified callback
	 * delegate will be triggered, passing along a bool that tells you whether the compile action succeeded.  This
	 * function never tries to unload modules or to reload the modules after they finish compiling.  You should do
	 * that yourself in the recompile completion callback!
	 *
	 * @param ModuleNames Names of the modules to recompile
	 * @param RecompileModulesCallback Callback function to execute after compilation finishes (whether successful or not.)
	 * @param bWaitForCompletion True if the function should not return until recompilation attempt has finished and callbacks have fired
	 * @param Ar Output device for logging compilation status
	 * @return	True if the recompile action was kicked off successfully.  If this returns false, then the recompile callback will never fire.  In the case where bWaitForCompletion=false, this will also return false if the compilation failed for any reason.
	 */
	bool RecompileModulesAsync( const TArray< FName > ModuleNames, const FRecompileModulesCallback& InRecompileModulesCallback, const bool bWaitForCompletion, FOutputDevice &Ar );

	/** Called for successfully re-complied module */
	void OnModuleCompileSucceeded(FName ModuleName, const FString& NewModuleFilename);

	/**
	 * Tries to recompile the specified DLL using UBT. Does not interact with modules. This is a low level routine.
	 *
	 * @param ModuleNames List of modules to recompile, including the module name and optional file suffix.
	 * @param Ar Output device for logging compilation status.
	 */
	bool RecompileModuleDLLs( const TArray< FModuleToRecompile >& ModuleNames, FOutputDevice& Ar );

	/** Returns arguments to pass to UnrealBuildTool when compiling modules */
	static FString MakeUBTArgumentsForModuleCompiling();

	/** 
	 *	Starts compiling DLL files for one or more modules.
	 *
	 *	@param GameName The name of the game.
	 *	@param ModuleNames The list of modules to compile.
	 *	@param InRecompileModulesCallback Callback function to make when module recompiles.
	 *	@param Ar
	 *	@param bInFailIfGeneratedCodeChanges If true, fail the compilation if generated headers change.
	 *	@param InAdditionalCmdLineArgs Additional arguments to pass to UBT.
	 *	@return true if successful, false otherwise.
	 */
	bool StartCompilingModuleDLLs(const FString& GameName, const TArray< FModuleToRecompile >& ModuleNames, 
		const FRecompileModulesCallback& InRecompileModulesCallback, FOutputDevice& Ar, bool bInFailIfGeneratedCodeChanges, 
		const FString& InAdditionalCmdLineArgs = FString() );

	/** Launches UnrealBuildTool with the specified command line parameters */
	bool InvokeUnrealBuildToolForCompile(const FString& InCmdLineParams, FOutputDevice &Ar);

	/** Checks to see if a pending compilation action has completed and optionally waits for it to finish.  If completed, fires any appropriate callbacks and reports status provided bFireEvents is true. */
	void CheckForFinishedModuleDLLCompile(const bool bWaitForCompletion, bool& bCompileStillInProgress, bool& bCompileSucceeded, FOutputDevice& Ar, const FText& SlowTaskOverrideText = FText::GetEmpty(), bool bFireEvents = true);

	/** Called when the compile data for a module need to be update in memory and written to config */
	void UpdateModuleCompileData(FName ModuleName);

	/** Called when a new module is added to the manager to get the saved compile data from config */
	static void ReadModuleCompilationInfoFromConfig(FName ModuleName, FModuleCompilationData& CompileData);

	/** Saves the module's compile data to config */
	static void WriteModuleCompilationInfoToConfig(FName ModuleName, const FModuleCompilationData& CompileData);

	/** Access the module's file and read the timestamp from the file system. Returns true if the timestamp was read successfully. */
	bool GetModuleFileTimeStamp(FName ModuleName, FDateTime& OutFileTimeStamp) const;

	/** FTicker delegate (hot-reload from IDE) */
	FTickerDelegate TickerDelegate;

	/** Callback when game binaries folder changes */
	IDirectoryWatcher::FDirectoryChanged BinariesFolderChangedDelegate;

	/** True if currently hot-reloading from editor (suppresses hot-reload from IDE) */
	bool bIsHotReloadingFromEditor;
	
	struct FRecompiledModule
	{
		FString Name;
		FString NewFilename;
		FRecompiledModule() {}
		FRecompiledModule(const FString& InName, const FString& InFilename)
			: Name(InName)
			, NewFilename(InFilename)
		{}
	};

	/** New module DLLs */
	TArray<FRecompiledModule> NewModules;

	/** Delegate broadcast when a module has been hot-reloaded */
	FHotReloadEvent HotReloadEvent;

	/** Array of modules that we're currently recompiling */
	TArray< FModuleToRecompile > ModulesBeingCompiled;

	/** Array of modules that we're going to recompile */
	TArray< FModuleToRecompile > ModulesThatWereBeingRecompiled;

	/** Last known compilation data for each module */
	TMap<FName, TSharedRef<FModuleCompilationData>> ModuleCompileData;

	/** Multicast delegate which will broadcast a notification when the compiler starts */
	FModuleCompilerStartedEvent ModuleCompilerStartedEvent;
	
	/** Multicast delegate which will broadcast a notification when the compiler finishes */
	FModuleCompilerFinishedEvent ModuleCompilerFinishedEvent;

	/** When compiling a module using an external application, stores the handle to the process that is running */
	FProcHandle ModuleCompileProcessHandle;

	/** When compiling a module using an external application, this is the process read pipe handle */
	void* ModuleCompileReadPipe;

	/** When compiling a module using an external application, this is the text that was read from the read pipe handle */
	FString ModuleCompileReadPipeText;

	/** Callback to execute after an asynchronous recompile has completed (whether successful or not.) */
	FRecompileModulesCallback RecompileModulesCallback;

	/** true if we should attempt to cancel the current async compilation */
	bool bRequestCancelCompilation;
};

namespace HotReloadDefs
{
	const static FString CompilationInfoConfigSection("ModuleFileTracking");

	// These strings should match the values of the enum EModuleCompileMethod in ModuleManager.h
	// and should be handled in ReadModuleCompilationInfoFromConfig() & WriteModuleCompilationInfoToConfig() below
	const static FString CompileMethodRuntime("Runtime");
	const static FString CompileMethodExternal("External");
	const static FString CompileMethodUnknown("Unknown");

	// Add one minute epsilon to timestamp comparision
	const static FTimespan TimeStampEpsilon(0, 1, 0);
}

IMPLEMENT_MODULE(FHotReloadModule, HotReload);

void FHotReloadModule::StartupModule()
{
	bIsHotReloadingFromEditor = false;

	// Register re-instancing delegate (Core)
	FCoreUObjectDelegates::ReplaceHotReloadClassDelegate.BindRaw(this, &FHotReloadModule::ReinstanceClass);
	
	// Register directory watcher delegate
	InitHotReloadWatcher();

	// Register hot-reload from IDE ticker
	TickerDelegate = FTickerDelegate::CreateRaw(this, &FHotReloadModule::Tick);
	FTicker::GetCoreTicker().AddTicker(TickerDelegate);
}

void FHotReloadModule::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickerDelegate);
	ShutdownHotReloadWatcher();
}

bool FHotReloadModule::Exec( UWorld* Inworld, const TCHAR* Cmd, FOutputDevice& Ar )
{
#if !UE_BUILD_SHIPPING
	if ( FParse::Command( &Cmd, TEXT( "Module" ) ) )
	{
#if !IS_MONOLITHIC
		// Recompile <ModuleName>
		if( FParse::Command( &Cmd, TEXT( "Recompile" ) ) )
		{
			const FString ModuleNameStr = FParse::Token( Cmd, 0 );
			if( !ModuleNameStr.IsEmpty() )
			{
				const FName ModuleName( *ModuleNameStr );
				const bool bReloadAfterRecompile = true;
				RecompileModule( ModuleName, bReloadAfterRecompile, Ar);
			}

			return true;
		}
#endif // !IS_MONOLITHIC
	}
#endif // !UE_BUILD_SHIPPING
	return false;
}

void FHotReloadModule::Tick()
{
	// We never want to block on a pending compile when checking compilation status during Tick().  We're
	// just checking so that we can fire callbacks if and when compilation has finished.
	const bool bWaitForCompletion = false;

	// Ignored output variables
	bool bCompileStillInProgress = false;
	bool bCompileSucceeded = false;
	FOutputDeviceNull NullOutput;
	CheckForFinishedModuleDLLCompile( bWaitForCompletion, bCompileStillInProgress, bCompileSucceeded, NullOutput );
}
	
void FHotReloadModule::SaveConfig()
{
	// Find all the modules
	TArray<FModuleStatus> Modules;
	FModuleManager::Get().QueryModules(Modules);

	// Update the compile data for each one
	for( const FModuleStatus &Module : Modules )
	{
		UpdateModuleCompileData(*Module.Name);
	}
}

FString FHotReloadModule::GetModuleCompileMethod(FName InModuleName)
{
	if (!ModuleCompileData.Contains(InModuleName))
	{
		UpdateModuleCompileData(InModuleName);
	}

	switch(ModuleCompileData.FindChecked(InModuleName).Get().CompileMethod)
	{
	case EModuleCompileMethod::External:
		return HotReloadDefs::CompileMethodExternal;
	case EModuleCompileMethod::Runtime:
		return HotReloadDefs::CompileMethodRuntime;
	default:
		return HotReloadDefs::CompileMethodUnknown;
	}
}

bool FHotReloadModule::RecompileModule( const FName InModuleName, const bool bReloadAfterRecompile, FOutputDevice &Ar )
{
#if !IS_MONOLITHIC
	const bool bShowProgressDialog = true;
	const bool bShowCancelButton = false;

	FFormatNamedArguments Args;
	Args.Add( TEXT("CodeModuleName"), FText::FromName( InModuleName ) );
	const FText StatusUpdate = FText::Format( NSLOCTEXT("ModuleManager", "Recompile_SlowTaskName", "Compiling {CodeModuleName}..."), Args );

	GWarn->BeginSlowTask( StatusUpdate, bShowProgressDialog, bShowCancelButton );

	ModuleCompilerStartedEvent.Broadcast();

	// Update our set of known modules, in case we don't already know about this module
	FModuleManager::Get().AddModule( InModuleName );

	// Only use rolling module names if the module was already loaded into memory.  This allows us to try compiling
	// the module without actually having to unload it first.
	const bool bWasModuleLoaded = FModuleManager::Get().IsModuleLoaded( InModuleName );
	const bool bUseRollingModuleNames = bWasModuleLoaded;

	bool bWasSuccessful = true;
	FString NewModuleFileNameOnSuccess = FModuleManager::Get().GetModuleFilename(InModuleName);
	if( bUseRollingModuleNames )
	{
		// First, try to compile the module.  If the module is already loaded, we won't unload it quite yet.  Instead
		// make sure that it compiles successfully.

		// Find a unique file name for the module
		FString UniqueSuffix;
		FString UniqueModuleFileName;
		FModuleManager::Get().MakeUniqueModuleFilename( InModuleName, UniqueSuffix, UniqueModuleFileName );

		// If the recompile succeeds, we'll update our cached file name to use the new unique file name
		// that we setup for the module
		NewModuleFileNameOnSuccess = UniqueModuleFileName;

		TArray< FModuleToRecompile > ModulesToRecompile;
		FModuleToRecompile ModuleToRecompile;
		ModuleToRecompile.ModuleName = InModuleName.ToString();
		ModuleToRecompile.ModuleFileSuffix = UniqueSuffix;
		ModuleToRecompile.NewModuleFilename = UniqueModuleFileName;
		ModulesToRecompile.Add( ModuleToRecompile );
		bWasSuccessful = RecompileModuleDLLs( ModulesToRecompile, Ar );
	}

	if( bWasSuccessful )
	{
		// Shutdown the module if it's already running
		if( bWasModuleLoaded )
		{
			Ar.Logf( TEXT( "Unloading module before compile." ) );
			FModuleManager::Get().UnloadOrAbandonModuleWithCallback( InModuleName, Ar );
		}

		if( !bUseRollingModuleNames )
		{
			// Try to recompile the DLL
			TArray< FModuleToRecompile > ModulesToRecompile;
			FModuleToRecompile ModuleToRecompile;
			ModuleToRecompile.ModuleName = InModuleName.ToString();
			ModulesToRecompile.Add( ModuleToRecompile );
			bWasSuccessful = RecompileModuleDLLs( ModulesToRecompile, Ar );
		}

		// Reload the module if it was loaded before we recompiled
		if( bWasSuccessful && bWasModuleLoaded && bReloadAfterRecompile )
		{
			Ar.Logf( TEXT( "Reloading module after successful compile." ) );
			bWasSuccessful = FModuleManager::Get().LoadModuleWithCallback( InModuleName, Ar );
		}
	}

	GWarn->EndSlowTask();
	return bWasSuccessful;
#else
	return false;
#endif // !IS_MONOLITHIC
}

/** Type hash for a UObject Function Pointer, maybe not a great choice, but it should be sufficient for the needs here. **/
inline uint32 GetTypeHash(Native A)
{
	return *(uint32*)&A;
}

/** Map from old function pointer to new function pointer for hot reload. */
static TMap<Native, Native> HotReloadFunctionRemap;

/** Adds and entry for the UFunction native pointer remap table */
void FHotReloadModule::AddHotReloadFunctionRemap(Native NewFunctionPointer, Native OldFunctionPointer)
{
	Native OtherNewFunction = HotReloadFunctionRemap.FindRef(OldFunctionPointer);
	check(!OtherNewFunction || OtherNewFunction == NewFunctionPointer);
	check(NewFunctionPointer);
	check(OldFunctionPointer);
	HotReloadFunctionRemap.Add(OldFunctionPointer, NewFunctionPointer);
}

void FHotReloadModule::DoHotReloadFromEditor()
{
	TArray<FString> GameModuleNames;
	TArray<UPackage*> PackagesToRebind;
	TArray<FName> DependentModules;
	GetGameModules(GameModuleNames);
	ECompilationResult::Type Result = ECompilationResult::Unsupported;
	// Analytics
	double Duration = 0.0;

	if (GameModuleNames.Num() > 0)
	{
		FScopedDurationTimer Timer(Duration);


		GetPackagesToRebindAndDependentModules(GameModuleNames, PackagesToRebind, DependentModules);

		const bool bWaitForCompletion = false;	// Don't wait -- we want compiling to happen asynchronously
		Result = RebindPackagesInternal(PackagesToRebind, DependentModules, bWaitForCompletion, *GLog);
	}

	RecordAnalyticsEvent(TEXT("Editor"), Result, Duration, PackagesToRebind.Num(), DependentModules.Num());
}

void FHotReloadModule::DoHotReloadCallback(bool bRecompileFinished, bool bRecompileSucceeded, TArray<UPackage*> Packages, TArray< FName > InDependentModules, FOutputDevice &HotReloadAr)
{
	DoHotReloadInternal(bRecompileFinished, bRecompileSucceeded, Packages, InDependentModules, HotReloadAr);
}

ECompilationResult::Type FHotReloadModule::DoHotReloadInternal(bool bRecompileFinished, bool bRecompileSucceeded, TArray<UPackage*> Packages, TArray< FName > InDependentModules, FOutputDevice &HotReloadAr)
{
	ECompilationResult::Type Result = ECompilationResult::Unsupported;
#if !IS_MONOLITHIC
	if (bRecompileSucceeded)
	{
		FFeedbackContext& ErrorsFC = UClass::GetDefaultPropertiesFeedbackContext();
		ErrorsFC.Errors.Empty();
		ErrorsFC.Warnings.Empty();
		// Rebind the hot reload DLL 
		TGuardValue<bool> GuardIsHotReload(GIsHotReload, true);
		TGuardValue<bool> GuardIsInitialLoad(GIsInitialLoad, true);
		HotReloadFunctionRemap.Empty(); // redundant

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); // we create a new CDO in the transient package...this needs to go away before we try again.

		// Load the new modules up
		bool bReloadSucceeded = false;
		for (TArray<UPackage*>::TConstIterator CurPackageIt(Packages); CurPackageIt; ++CurPackageIt)
		{
			UPackage* Package = *CurPackageIt;
			FName ShortPackageName = FPackageName::GetShortFName(Package->GetFName());

			// Abandon the old module.  We can't unload it because various data structures may be living
			// that have vtables pointing to code that would become invalidated.
			FModuleManager::Get().AbandonModule(ShortPackageName);

			// Module should never be loaded at this point
			check(!FModuleManager::Get().IsModuleLoaded(ShortPackageName));

			// Load the newly-recompiled module up (it will actually have a different DLL file name at this point.)
			FModuleManager::Get().LoadModule(ShortPackageName);
			bReloadSucceeded = FModuleManager::Get().IsModuleLoaded(ShortPackageName);
			if (!bReloadSucceeded)
			{
				HotReloadAr.Logf(ELogVerbosity::Warning, TEXT("HotReload failed, reload failed %s."), *Package->GetName());
				Result = ECompilationResult::OtherCompilationError;
				break;
			}
		}
		// Load dependent modules.
		for (int32 Nx = 0; Nx < InDependentModules.Num(); ++Nx)
		{
			const FName ModuleName = InDependentModules[Nx];

			FModuleManager::Get().UnloadOrAbandonModuleWithCallback(ModuleName, HotReloadAr);
			const bool bLoaded = FModuleManager::Get().LoadModuleWithCallback(ModuleName, HotReloadAr);
			if (!bLoaded)
			{
				HotReloadAr.Logf(ELogVerbosity::Warning, TEXT("Unable to reload module %s"), *ModuleName.GetPlainNameString());
			}
		}

		if (ErrorsFC.Errors.Num() || ErrorsFC.Warnings.Num())
		{
			TArray<FString> All;
			All = ErrorsFC.Errors;
			All += ErrorsFC.Warnings;

			ErrorsFC.Errors.Empty();
			ErrorsFC.Warnings.Empty();

			FString AllInOne;
			for (int32 Index = 0; Index < All.Num(); Index++)
			{
				AllInOne += All[Index];
				AllInOne += TEXT("\n");
			}
			HotReloadAr.Logf(ELogVerbosity::Warning, TEXT("Some classes could not be reloaded:\n%s"), *AllInOne);
		}

		if (bReloadSucceeded)
		{
			int32 Count = 0;
			// Remap all native functions (and gather scriptstructs)
			TArray<UScriptStruct*> ScriptStructs;
			for (FRawObjectIterator It; It; ++It)
			{
				if (UFunction* Function = Cast<UFunction>(*It))
				{
					if (Native NewFunction = HotReloadFunctionRemap.FindRef(Function->GetNativeFunc()))
					{
						Count++;
						Function->SetNativeFunc(NewFunction);
					}
				}

				if (UScriptStruct* ScriptStruct = Cast<UScriptStruct>(*It))
				{
					if (Packages.ContainsByPredicate([=](UPackage* Package) { return ScriptStruct->IsIn(Package); }) && ScriptStruct->GetCppStructOps())
					{
						ScriptStructs.Add(ScriptStruct);
					}
				}
			}
			// now let's set up the script structs...this relies on super behavior, so null them all, then set them all up. Internally this sets them up hierarchically.
			for (int32 ScriptIndex = 0; ScriptIndex < ScriptStructs.Num(); ScriptIndex++)
			{
				ScriptStructs[ScriptIndex]->ClearCppStructOps();
			}
			for (int32 ScriptIndex = 0; ScriptIndex < ScriptStructs.Num(); ScriptIndex++)
			{
				ScriptStructs[ScriptIndex]->PrepareCppStructOps();
				check(ScriptStructs[ScriptIndex]->GetCppStructOps());
			}
			HotReloadAr.Logf(ELogVerbosity::Warning, TEXT("HotReload successful (%d functions remapped  %d scriptstructs remapped)"), Count, ScriptStructs.Num());

			HotReloadFunctionRemap.Empty();
			Result = ECompilationResult::Succeeded;
		}


		const bool bWasTriggeredAutomatically = !bIsHotReloadingFromEditor;
		BroadcastHotReload( bWasTriggeredAutomatically );
	}
	else if (bRecompileFinished)
	{
		HotReloadAr.Logf(ELogVerbosity::Warning, TEXT("HotReload failed, recompile failed"));
		Result = ECompilationResult::OtherCompilationError;
	}
#endif
	bIsHotReloadingFromEditor = false;
	return Result;
}

void FHotReloadModule::RebindPackages(TArray<UPackage*> InPackages, TArray<FName> DependentModules, const bool bWaitForCompletion, FOutputDevice &Ar)
{
	ECompilationResult::Type Result = ECompilationResult::Unknown;
	double Duration = 0.0;
	{
		FScopedDurationTimer RebindTimer(Duration);
		Result = RebindPackagesInternal(InPackages, DependentModules, bWaitForCompletion, Ar);
	}
	RecordAnalyticsEvent(TEXT("Rebind"), Result, Duration, InPackages.Num(), DependentModules.Num());
}

ECompilationResult::Type FHotReloadModule::RebindPackagesInternal(TArray<UPackage*> InPackages, TArray<FName> DependentModules, const bool bWaitForCompletion, FOutputDevice &Ar)
{
	ECompilationResult::Type Result = ECompilationResult::Unsupported;
#if !IS_MONOLITHIC
	bool bCanRebind = InPackages.Num() > 0;

	// Verify that we're going to be able to rebind the specified packages
	if (bCanRebind)
	{
		for (UPackage* Package : InPackages)
		{
			check(Package);

			if (Package->GetOuter() != NULL)
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("Could not rebind package for %s, package is either not bound yet or is not a DLL."), *Package->GetName());
				bCanRebind = false;
				break;
			}
		}
	}

	// We can only proceed if a compile isn't already in progress
	if (IsCurrentlyCompiling())
	{
		Ar.Logf(ELogVerbosity::Warning, TEXT("Could not rebind package because a module compile is already in progress."));
		bCanRebind = false;
	}

	if (bCanRebind)
	{
		bIsHotReloadingFromEditor = true;

		const double StartTime = FPlatformTime::Seconds();

		TArray< FName > ModuleNames;
		for (UPackage* Package : InPackages)
		{
			// Attempt to recompile this package's module
			FName ShortPackageName = FPackageName::GetShortFName(Package->GetFName());
			ModuleNames.Add(ShortPackageName);
		}

		// Add dependent modules.
		ModuleNames.Append(DependentModules);

		// Start compiling modules
		const bool bCompileStarted = RecompileModulesAsync(
			ModuleNames,
			FRecompileModulesCallback::CreateRaw< FHotReloadModule, TArray<UPackage*>, TArray<FName>, FOutputDevice& >(this, &FHotReloadModule::DoHotReloadCallback, InPackages, DependentModules, Ar),
			bWaitForCompletion,
			Ar);

		if (bCompileStarted)
		{
			if (bWaitForCompletion)
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("HotReload operation took %4.1fs."), float(FPlatformTime::Seconds() - StartTime));
				bIsHotReloadingFromEditor = false;
			}
			else
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("Starting HotReload took %4.1fs."), float(FPlatformTime::Seconds() - StartTime));
			}
			Result = ECompilationResult::Succeeded;
		}
		else
		{
			Ar.Logf(ELogVerbosity::Warning, TEXT("RebindPackages failed because the compiler could not be started."));
			Result = ECompilationResult::OtherCompilationError;
			bIsHotReloadingFromEditor = false;
		}
	}
	else
#endif
	{
		Ar.Logf(ELogVerbosity::Warning, TEXT("RebindPackages not possible for specified packages (or application was compiled in monolithic mode.)"));
	}
	return Result;
}

void FHotReloadModule::ReinstanceClass(UClass* OldClass, UClass* NewClass)
{
	UE_LOG(LogHotReload, Log, TEXT("Re-instancing %s after hot-reload."), *NewClass->GetName());
	FBlueprintCompileReinstancer ReinstanceHelper(NewClass, OldClass);
	ReinstanceHelper.ReinstanceObjects();
}

void FHotReloadModule::GetGameModules(TArray<FString>& OutGameModules)
{
	// Ask the module manager for a list of currently-loaded gameplay modules
	TArray< FModuleStatus > ModuleStatuses;
	FModuleManager::Get().QueryModules(ModuleStatuses);

	for (TArray< FModuleStatus >::TConstIterator ModuleStatusIt(ModuleStatuses); ModuleStatusIt; ++ModuleStatusIt)
	{
		const FModuleStatus& ModuleStatus = *ModuleStatusIt;

		// We only care about game modules that are currently loaded
		if (ModuleStatus.bIsLoaded && ModuleStatus.bIsGameModule)
		{
			OutGameModules.AddUnique(ModuleStatus.Name);
		}
	}
}

void FHotReloadModule::OnHotReloadBinariesChanged(const TArray<struct FFileChangeData>& FileChanges)
{
	if (bIsHotReloadingFromEditor)
	{
		// DO NOTHING, this case is handled by RebindPackages
		return;
	}

	TArray< FString > GameModuleNames;
	GetGameModules(GameModuleNames);

	if (GameModuleNames.Num() > 0)
	{
		// Check if any of the game DLLs has been added
		for (auto& Change : FileChanges)
		{
			if (Change.Action == FFileChangeData::FCA_Added)
			{
				const FString Filename = FPaths::GetCleanFilename(Change.Filename);
				if (Filename.EndsWith(FPlatformProcess::GetModuleExtension()))
				{
					for (auto& GameModule : GameModuleNames)
					{
						if (Filename.Contains(GameModule) && !NewModules.ContainsByPredicate([&](const FRecompiledModule& Module){ return Module.Name == GameModule; }))
						{
							// Add to queue. We do not hot-reload here as there may potentially be other modules being compiled.
							NewModules.Add(FRecompiledModule(GameModule, Change.Filename));
							UE_LOG(LogHotReload, Log, TEXT("New module detected: %s"), *Filename);
						}
					}
				}
			}
		}
	}
}

void FHotReloadModule::InitHotReloadWatcher()
{
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::Get().LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
	if (DirectoryWatcher)
	{
		// Watch the game binaries folder for new files
		FString BinariesPath = FPaths::ConvertRelativePathToFull(FPaths::GameDir() / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory());
		BinariesFolderChangedDelegate = IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FHotReloadModule::OnHotReloadBinariesChanged);
		DirectoryWatcher->RegisterDirectoryChangedCallback(BinariesPath, BinariesFolderChangedDelegate);
	}
}

void FHotReloadModule::ShutdownHotReloadWatcher()
{
	FDirectoryWatcherModule* DirectoryWatcherModule = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	if( DirectoryWatcherModule != nullptr )
	{
		IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule->Get();
		if (DirectoryWatcher)
		{
			FString BinariesPath = FPaths::ConvertRelativePathToFull(FPaths::GameDir() / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory());
			DirectoryWatcher->UnregisterDirectoryChangedCallback(BinariesPath, BinariesFolderChangedDelegate);
		}
	}
}

bool FHotReloadModule::Tick(float DeltaTime)
{
	if (NewModules.Num())
	{
		// We have new modules in the queue, but make sure UBT has finished compiling all of them
		if (!FPlatformProcess::IsApplicationRunning(TEXT("UnrealBuildTool")))
		{
			DoHotReloadFromIDE();
			NewModules.Empty();
		}
		else
		{
			UE_LOG(LogHotReload, Verbose, TEXT("Detected %d reloaded modules but UnrealBuildTool is still running"), NewModules.Num());
		}
	}
	return true;
}

void FHotReloadModule::GetPackagesToRebindAndDependentModules(const TArray<FString>& InGameModuleNames, TArray<UPackage*>& OutPackagesToRebind, TArray<FName>& OutDependentModules)
{
	for (auto& GameModuleName : InGameModuleNames)
	{
		FString PackagePath(FString(TEXT("/Script/")) + GameModuleName);
		UPackage* Package = FindPackage(NULL, *PackagePath);
		if (Package != NULL)
		{
			OutPackagesToRebind.Add(Package);
		}
		else
		{
			OutDependentModules.Add(*GameModuleName);
		}
	}
}

void FHotReloadModule::DoHotReloadFromIDE()
{
	TArray<FString> GameModuleNames;
	TArray<UPackage*> PackagesToRebind;
	TArray<FName> DependentModules;
	double Duration = 0.0;
	ECompilationResult::Type Result = ECompilationResult::Unsupported;

	GetGameModules(GameModuleNames);

	if (GameModuleNames.Num() > 0)
	{		
		FScopedDurationTimer Timer(Duration);

		UE_LOG(LogHotReload, Log, TEXT("Starting Hot-Reload from IDE"));

		GWarn->BeginSlowTask(LOCTEXT("CompilingGameCode", "Compiling Game Code"), true);

		// Update compile data before we start compiling
		for (auto& NewModule : NewModules)
		{
			UpdateModuleCompileData(*NewModule.Name);
			OnModuleCompileSucceeded(*NewModule.Name, NewModule.NewFilename);
		}


		GetPackagesToRebindAndDependentModules(GameModuleNames, PackagesToRebind, DependentModules);

		check(PackagesToRebind.Num() || DependentModules.Num())
		{
			const bool bRecompileFinished = true;
			const bool bRecompileSucceeded = true;
			Result = DoHotReloadInternal(bRecompileFinished, bRecompileSucceeded, PackagesToRebind, DependentModules, *GLog);
		}

		GWarn->EndSlowTask();
	}

	RecordAnalyticsEvent(TEXT("IDE"), Result, Duration, PackagesToRebind.Num(), DependentModules.Num());
}

void FHotReloadModule::RecordAnalyticsEvent(const TCHAR* ReloadFrom, ECompilationResult::Type Result, double Duration, int32 PackageCount, int32 DependentModulesCount)
{
	if (FEngineAnalytics::IsAvailable())
	{
		TArray< FAnalyticsEventAttribute > ReloadAttribs;
		ReloadAttribs.Add(FAnalyticsEventAttribute(TEXT("ReloadFrom"), ReloadFrom));
		ReloadAttribs.Add(FAnalyticsEventAttribute(TEXT("Result"), ECompilationResult::ToString(Result)));
		ReloadAttribs.Add(FAnalyticsEventAttribute(TEXT("Duration"), FString::Printf(TEXT("%.4lf"), Duration)));
		ReloadAttribs.Add(FAnalyticsEventAttribute(TEXT("Packages"), FString::Printf(TEXT("%d"), PackageCount)));
		ReloadAttribs.Add(FAnalyticsEventAttribute(TEXT("DependentModules"), FString::Printf(TEXT("%d"), DependentModulesCount)));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.HotReload"), ReloadAttribs);
	}
}

bool FHotReloadModule::RecompileModulesAsync( const TArray< FName > ModuleNames, const FRecompileModulesCallback& InRecompileModulesCallback, const bool bWaitForCompletion, FOutputDevice &Ar )
{
#if !IS_MONOLITHIC
	// NOTE: This method of recompiling always using a rolling file name scheme, since we never want to unload before
	// we start recompiling, and we need the output DLL to be unlocked before we invoke the compiler

	ModuleCompilerStartedEvent.Broadcast();

	TArray< FModuleToRecompile > ModulesToRecompile;

	for( TArray< FName >::TConstIterator CurModuleIt( ModuleNames ); CurModuleIt; ++CurModuleIt )
	{
		const FName CurModuleName = *CurModuleIt;

		// Update our set of known modules, in case we don't already know about this module
		FModuleManager::Get().AddModule( CurModuleName );

		FString NewModuleFileNameOnSuccess = FModuleManager::Get().GetModuleFilename(CurModuleName);

		// Find a unique file name for the module
		FString UniqueSuffix;
		FString UniqueModuleFileName;
		FModuleManager::Get().MakeUniqueModuleFilename( CurModuleName, UniqueSuffix, UniqueModuleFileName );

		// If the recompile succeeds, we'll update our cached file name to use the new unique file name
		// that we setup for the module
		NewModuleFileNameOnSuccess = UniqueModuleFileName;

		FModuleToRecompile ModuleToRecompile;
		ModuleToRecompile.ModuleName = CurModuleName.ToString();
		ModuleToRecompile.ModuleFileSuffix = UniqueSuffix;
		ModuleToRecompile.NewModuleFilename = UniqueModuleFileName;
		ModulesToRecompile.Add( ModuleToRecompile );
	}

	// Kick off compilation!
	const FString AdditionalArguments = MakeUBTArgumentsForModuleCompiling();
	const bool bFailIfGeneratedCodeChanges = false;
	bool bWasSuccessful = StartCompilingModuleDLLs( FApp::GetGameName(), ModulesToRecompile, InRecompileModulesCallback, Ar, bFailIfGeneratedCodeChanges, AdditionalArguments );
	if (bWasSuccessful)
	{
		// Go ahead and check for completion right away.  This is really just so that we can handle the case
		// where the user asked us to wait for the compile to finish before returning.
		bool bCompileStillInProgress = false;
		bool bCompileSucceeded = false;
		FOutputDeviceNull NullOutput;
		CheckForFinishedModuleDLLCompile( bWaitForCompletion, bCompileStillInProgress, bCompileSucceeded, NullOutput );
		if( !bCompileStillInProgress && !bCompileSucceeded )
		{
			bWasSuccessful = false;
		}
	}

	return bWasSuccessful;
#else
	return false;
#endif // !IS_MONOLITHIC
}

void FHotReloadModule::OnModuleCompileSucceeded(FName ModuleName, const FString& NewModuleFilename)
{
	// If the compile succeeded, update the module info entry with the new file name for this module
	FModuleManager::Get().SetModuleFilename(ModuleName, NewModuleFilename);

#if !IS_MONOLITHIC && WITH_EDITOR
	// UpdateModuleCompileData() should have been run before compiling so the
	// data in ModuleInfo should be correct for the pre-compile dll file.
	FModuleCompilationData& CompileData = ModuleCompileData.FindChecked(ModuleName).Get();

	FDateTime FileTimeStamp;
	bool bGotFileTimeStamp = GetModuleFileTimeStamp(ModuleName, FileTimeStamp);

	CompileData.bHasFileTimeStamp = bGotFileTimeStamp;
	CompileData.FileTimeStamp = FileTimeStamp;

	if (CompileData.bHasFileTimeStamp)
	{
		CompileData.CompileMethod = EModuleCompileMethod::Runtime;
	}
	else
	{
		CompileData.CompileMethod = EModuleCompileMethod::Unknown;
	}
	WriteModuleCompilationInfoToConfig(ModuleName, CompileData);
#endif
}

bool FHotReloadModule::RecompileModuleDLLs( const TArray< FModuleToRecompile >& ModuleNames, FOutputDevice& Ar )
{
	bool bCompileSucceeded = false;
#if !IS_MONOLITHIC
	const FString AdditionalArguments = MakeUBTArgumentsForModuleCompiling();
	if( StartCompilingModuleDLLs( FApp::GetGameName(), ModuleNames, FRecompileModulesCallback(), Ar, true, AdditionalArguments ) )
	{
		const bool bWaitForCompletion = true;	// Always wait
		bool bCompileStillInProgress = false;
		CheckForFinishedModuleDLLCompile( bWaitForCompletion, bCompileStillInProgress, bCompileSucceeded, Ar );
	}
#endif
	return bCompileSucceeded;
}

FString FHotReloadModule::MakeUBTArgumentsForModuleCompiling()
{
	FString AdditionalArguments;
	if ( FPaths::IsProjectFilePathSet() )
	{
		// We have to pass FULL paths to UBT
		FString FullProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

		// @todo projectdirs: Currently non-Rocket projects that exist under the UE4 root are compiled by UBT with no .uproject file
		//     name passed in (see bIsProjectTarget in VCProject.cs), which causes intermediate libraries to be saved to the Engine
		//     intermediate folder instead of the project's intermediate folder.  We're emulating this behavior here for module
		//     recompiling, so that compiled modules will be able to find their import libraries in the original folder they were compiled.
		if( FRocketSupport::IsRocket() || !FullProjectPath.StartsWith( FPaths::ConvertRelativePathToFull( FPaths::RootDir() ) ) )
		{
			const FString ProjectFilenameWithQuotes = FString::Printf(TEXT("\"%s\""), *FullProjectPath);
			AdditionalArguments += FString::Printf(TEXT("%s "), *ProjectFilenameWithQuotes);
		}

		if (FRocketSupport::IsRocket())
		{
			AdditionalArguments += TEXT("-rocket ");
		}
	}

	return AdditionalArguments;
}

bool FHotReloadModule::StartCompilingModuleDLLs(const FString& GameName, const TArray< FModuleToRecompile >& ModuleNames, 
	const FRecompileModulesCallback& InRecompileModulesCallback, FOutputDevice& Ar, bool bInFailIfGeneratedCodeChanges, 
	const FString& InAdditionalCmdLineArgs )
{
#if PLATFORM_DESKTOP && !IS_MONOLITHIC
	// Keep track of what we're compiling
	ModulesBeingCompiled = ModuleNames;
	ModulesThatWereBeingRecompiled = ModulesBeingCompiled;

	const TCHAR* BuildPlatformName = FPlatformMisc::GetUBTPlatform();
	const TCHAR* BuildConfigurationName = FModuleManager::GetUBTConfiguration();

	RecompileModulesCallback = InRecompileModulesCallback;

	// Pass a module file suffix to UBT if we have one
	FString ModuleArg;
	for( int32 CurModuleIndex = 0; CurModuleIndex < ModuleNames.Num(); ++CurModuleIndex )
	{
		if( !ModuleNames[ CurModuleIndex ].ModuleFileSuffix.IsEmpty() )
		{
			ModuleArg += FString::Printf( TEXT( " -ModuleWithSuffix %s %s" ), *ModuleNames[ CurModuleIndex ].ModuleName, *ModuleNames[ CurModuleIndex ].ModuleFileSuffix );
		}
		else
		{
			ModuleArg += FString::Printf( TEXT( " -Module %s" ), *ModuleNames[ CurModuleIndex ].ModuleName );
		}
		Ar.Logf( TEXT( "Recompiling %s..." ), *ModuleNames[ CurModuleIndex ].ModuleName );

		// prepare the compile info in the FModuleInfo so that it can be compared after compiling
		FName ModuleFName(*ModuleNames[ CurModuleIndex ].ModuleName);
		UpdateModuleCompileData(ModuleFName);
	}

	FString ExtraArg;
#if UE_EDITOR
	// NOTE: When recompiling from the editor, we're passed the game target name, not the editor target name, but we'll
	//       pass "-editorrecompile" to UBT which tells UBT to figure out the editor target to use for this game, since
	//       we can't possibly know what the target is called from within the engine code.
	ExtraArg = TEXT( "-editorrecompile " );
#endif

	if (bInFailIfGeneratedCodeChanges)
	{
		// Additional argument to let UHT know that we can only compile the module if the generated code didn't change
		ExtraArg += TEXT( "-FailIfGeneratedCodeChanges " );
	}

	// Shared PCH does no work with hot-reloading modules as we don't scan all modules for them.
	ExtraArg += TEXT("-nosharedpch ");

	// If the're no game modules loaded, then it's not a code-based project and the target
	// for UBT should be the editor.
	FString TargetName;
	if (IsAnyGameModuleLoaded())
	{
		TargetName = GameName;
	}
	else
	{
		TargetName = TEXT("UE4Editor");
	}
	FString CmdLineParams = FString::Printf( TEXT( "%s%s %s %s %s%s" ), 
		*TargetName, *ModuleArg, 
		BuildPlatformName, BuildConfigurationName, 
		*ExtraArg, *InAdditionalCmdLineArgs );

	const bool bInvocationSuccessful = InvokeUnrealBuildToolForCompile(CmdLineParams, Ar);
	if ( !bInvocationSuccessful )
	{
		// No longer compiling modules
		ModulesBeingCompiled.Empty();

		ModuleCompilerFinishedEvent.Broadcast(FString(), ECompilationResult::OtherCompilationError, false);

		// Fire task completion delegate 
		
		RecompileModulesCallback.ExecuteIfBound( false, false );
		RecompileModulesCallback.Unbind();
	}

	return bInvocationSuccessful;
#else
	return false;
#endif
}

bool FHotReloadModule::InvokeUnrealBuildToolForCompile(const FString& InCmdLineParams, FOutputDevice &Ar)
{
#if PLATFORM_DESKTOP && !IS_MONOLITHIC

	// Make sure we're not already compiling something!
	check(!IsCurrentlyCompiling());

	// Setup output redirection pipes, so that we can harvest compiler output and display it ourselves
#if PLATFORM_LINUX
	int pipefd[2];
	pipe(pipefd);
	void* PipeRead = &pipefd[0];
	void* PipeWrite = &pipefd[1];
#else
	void* PipeRead = NULL;
	void* PipeWrite = NULL;
#endif

	verify(FPlatformProcess::CreatePipe(PipeRead, PipeWrite));
	ModuleCompileReadPipeText = TEXT("");

	FProcHandle ProcHandle = FDesktopPlatformModule::Get()->InvokeUnrealBuildToolAsync(InCmdLineParams, Ar, PipeRead, PipeWrite);

	// We no longer need the Write pipe so close it.
	// We DO need the Read pipe however...
#if PLATFORM_LINUX
	close(*(int*)PipeWrite);
#else
	FPlatformProcess::ClosePipe(0, PipeWrite);
#endif

	if (!ProcHandle.IsValid())
	{
		// We're done with the process handle now
		ModuleCompileProcessHandle.Reset();
		ModuleCompileReadPipe = NULL;
	}
	else
	{
		ModuleCompileProcessHandle = ProcHandle;
		ModuleCompileReadPipe = PipeRead;
	}

	return ProcHandle.IsValid();
#else
	return false;
#endif // PLATFORM_DESKTOP && !IS_MONOLITHIC
}

void FHotReloadModule::CheckForFinishedModuleDLLCompile(const bool bWaitForCompletion, bool& bCompileStillInProgress, bool& bCompileSucceeded, FOutputDevice& Ar, const FText& SlowTaskOverrideText, bool bFireEvents)
{
#if PLATFORM_DESKTOP && !IS_MONOLITHIC
	bCompileStillInProgress = false;
	ECompilationResult::Type CompilationResult = ECompilationResult::OtherCompilationError;

	// Is there a compilation in progress?
	if( IsCurrentlyCompiling() )
	{
		bCompileStillInProgress = true;

		// Ensure slow task messages are seen.
		GWarn->PushStatus();

		// Update the slow task dialog if we were summoned from a synchronous recompile path
		if (GIsSlowTask)
		{
			if ( !SlowTaskOverrideText.IsEmpty() )
			{
				GWarn->StatusUpdate(-1, -1, SlowTaskOverrideText);
			}
			else
			{
				FText StatusUpdate;
				if ( ModulesBeingCompiled.Num() > 0 )
				{
					FFormatNamedArguments Args;
					Args.Add( TEXT("CodeModuleName"), FText::FromString( ModulesBeingCompiled[0].ModuleName ) );
					StatusUpdate = FText::Format( NSLOCTEXT("FModuleManager", "CompileSpecificModuleStatusMessage", "{CodeModuleName}: Compiling modules..."), Args );
				}
				else
				{
					StatusUpdate = NSLOCTEXT("FModuleManager", "CompileStatusMessage", "Compiling modules...");
				}

				GWarn->StatusUpdate(-1, -1, StatusUpdate);
			}
		}

		// Check to see if the compile has finished yet
		int32 ReturnCode = -1;
		while (bCompileStillInProgress)
		{
			if( FPlatformProcess::GetProcReturnCode( ModuleCompileProcessHandle, &ReturnCode ) )
			{
				bCompileStillInProgress = false;
			}
			
			if (bRequestCancelCompilation)
			{
				FPlatformProcess::TerminateProc(ModuleCompileProcessHandle);
				bCompileStillInProgress = bRequestCancelCompilation = false;
			}

			if( bCompileStillInProgress )
			{
				ModuleCompileReadPipeText += FPlatformProcess::ReadPipe(ModuleCompileReadPipe);

				if( !bWaitForCompletion )
				{
					// We haven't finished compiling, but we were asked to return immediately

					break;
				}

				// Give up a small timeslice if we haven't finished recompiling yet
				FPlatformProcess::Sleep( 0.01f );
			}
		}
		
		bRequestCancelCompilation = false;

		// Restore any status from before the loop - see PushStatus() above.
		GWarn->PopStatus();

		if( !bCompileStillInProgress )		
		{
			// Compilation finished, now we need to grab all of the text from the output pipe
			ModuleCompileReadPipeText += FPlatformProcess::ReadPipe(ModuleCompileReadPipe);

			// The ReturnCode is -1 only if compilation was cancelled.
			CompilationResult = ReturnCode != -1 ? (ECompilationResult::Type)ReturnCode : ECompilationResult::OtherCompilationError;

			// If compilation succeeded for all modules, go back to the modules and update their module file names
			// in case we recompiled the modules to a new unique file name.  This is needed so that when the module
			// is reloaded after the recompile, we load the new DLL file name, not the old one
			if(CompilationResult == ECompilationResult::Succeeded)
			{
				for( int32 CurModuleIndex = 0; CurModuleIndex < ModulesThatWereBeingRecompiled.Num(); ++CurModuleIndex )
				{
					const FModuleToRecompile& CurModule = ModulesThatWereBeingRecompiled[ CurModuleIndex ];

					// Were we asked to assign a new file name for this module?
					if( !CurModule.NewModuleFilename.IsEmpty() )
					{
						// If the compile succeeded, update the module info entry with the new file name for this module
						OnModuleCompileSucceeded(FName(*CurModule.ModuleName), CurModule.NewModuleFilename);
					}
				}

				ModulesThatWereBeingRecompiled.Empty();
			}

			// We're done with the process handle now
			ModuleCompileProcessHandle.Close();
			ModuleCompileProcessHandle.Reset();

#if PLATFORM_LINUX
			close(*(int *)ModuleCompileReadPipe);
#else
			FPlatformProcess::ClosePipe(ModuleCompileReadPipe, 0);
#endif

			Ar.Log(*ModuleCompileReadPipeText);
			const FString FinalOutput = ModuleCompileReadPipeText;
			ModuleCompileReadPipe = NULL;
			ModuleCompileReadPipeText = TEXT("");

			// No longer compiling modules
			ModulesBeingCompiled.Empty();

			bCompileSucceeded = CompilationResult == ECompilationResult::Succeeded;

			if ( bFireEvents )
			{
				const bool bShowLogOnSuccess = false;
				ModuleCompilerFinishedEvent.Broadcast(FinalOutput, CompilationResult, !bCompileSucceeded || bShowLogOnSuccess);

				// Fire task completion delegate 
				RecompileModulesCallback.ExecuteIfBound( true, bCompileSucceeded );
				RecompileModulesCallback.Unbind();
			}
		}
		else
		{
			Ar.Logf(TEXT("Error: CheckForFinishedModuleDLLCompile: Compilation is still in progress"));
		}
	}
	else
	{
		Ar.Logf(TEXT("Error: CheckForFinishedModuleDLLCompile: There is no compilation in progress right now"));
	}
#endif // PLATFORM_DESKTOP && !IS_MONOLITHIC
}

void FHotReloadModule::UpdateModuleCompileData(FName ModuleName)
{
	// Find or create a compile data object for this module
	TSharedRef<FModuleCompilationData>* CompileDataPtr = ModuleCompileData.Find(ModuleName);
	if(CompileDataPtr == nullptr)
	{
		CompileDataPtr = &ModuleCompileData.Add(ModuleName, TSharedRef<FModuleCompilationData>(new FModuleCompilationData()));
	}

	// reset the compile data before updating it
	FModuleCompilationData& CompileData = CompileDataPtr->Get();
	CompileData.bHasFileTimeStamp = false;
	CompileData.FileTimeStamp = FDateTime(0);
	CompileData.CompileMethod = EModuleCompileMethod::Unknown;

#if !IS_MONOLITHIC && WITH_EDITOR
	ReadModuleCompilationInfoFromConfig(ModuleName, CompileData);

	FDateTime FileTimeStamp;
	bool bGotFileTimeStamp = GetModuleFileTimeStamp(ModuleName, FileTimeStamp);

	if (!bGotFileTimeStamp)
	{
		// File missing? Reset the cached timestamp and method to defaults and save them.
		CompileData.bHasFileTimeStamp = false;
		CompileData.FileTimeStamp = FDateTime(0);
		CompileData.CompileMethod = EModuleCompileMethod::Unknown;
		WriteModuleCompilationInfoToConfig(ModuleName, CompileData);
	}
	else
	{
		if (CompileData.bHasFileTimeStamp)
		{
			if (FileTimeStamp > CompileData.FileTimeStamp + HotReloadDefs::TimeStampEpsilon)
			{
				// The file is newer than the cached timestamp
				// The file must have been compiled externally
				CompileData.FileTimeStamp = FileTimeStamp;
				CompileData.CompileMethod = EModuleCompileMethod::External;
				WriteModuleCompilationInfoToConfig(ModuleName, CompileData);
			}
		}
		else
		{
			// The cached timestamp and method are default value so this file has no history yet
			// We can only set its timestamp and save
			CompileData.bHasFileTimeStamp = true;
			CompileData.FileTimeStamp = FileTimeStamp;
			WriteModuleCompilationInfoToConfig(ModuleName, CompileData);
		}
	}
#endif
}

void FHotReloadModule::ReadModuleCompilationInfoFromConfig(FName ModuleName, FModuleCompilationData& CompileData)
{
	FString DateTimeString;
	if (GConfig->GetString(*HotReloadDefs::CompilationInfoConfigSection, *FString::Printf(TEXT("%s.TimeStamp"), *ModuleName.ToString()), DateTimeString, GEditorUserSettingsIni))
	{
		FDateTime TimeStamp;
		if (!DateTimeString.IsEmpty() && FDateTime::Parse(DateTimeString, TimeStamp))
		{
			CompileData.bHasFileTimeStamp = true;
			CompileData.FileTimeStamp = TimeStamp;

			FString CompileMethodString;
			if (GConfig->GetString(*HotReloadDefs::CompilationInfoConfigSection, *FString::Printf(TEXT("%s.LastCompileMethod"), *ModuleName.ToString()), CompileMethodString, GEditorUserSettingsIni))
			{
				if (CompileMethodString.Equals(HotReloadDefs::CompileMethodRuntime, ESearchCase::IgnoreCase))
				{
					CompileData.CompileMethod = EModuleCompileMethod::Runtime;
				}
				else if (CompileMethodString.Equals(HotReloadDefs::CompileMethodExternal, ESearchCase::IgnoreCase))
				{
					CompileData.CompileMethod = EModuleCompileMethod::External;
				}
			}
		}
	}
}

void FHotReloadModule::WriteModuleCompilationInfoToConfig(FName ModuleName, const FModuleCompilationData& CompileData)
{
	FString DateTimeString;
	if (CompileData.bHasFileTimeStamp)
	{
		DateTimeString = CompileData.FileTimeStamp.ToString();
	}

	GConfig->SetString(*HotReloadDefs::CompilationInfoConfigSection, *FString::Printf(TEXT("%s.TimeStamp"), *ModuleName.ToString()), *DateTimeString, GEditorUserSettingsIni);

	FString CompileMethodString = HotReloadDefs::CompileMethodUnknown;
	if (CompileData.CompileMethod == EModuleCompileMethod::Runtime)
	{
		CompileMethodString = HotReloadDefs::CompileMethodRuntime;
	}
	else if (CompileData.CompileMethod == EModuleCompileMethod::External)
	{
		CompileMethodString = HotReloadDefs::CompileMethodExternal;
	}

	GConfig->SetString(*HotReloadDefs::CompilationInfoConfigSection, *FString::Printf(TEXT("%s.LastCompileMethod"), *ModuleName.ToString()), *CompileMethodString, GEditorUserSettingsIni);
}

bool FHotReloadModule::GetModuleFileTimeStamp(FName ModuleName, FDateTime& OutFileTimeStamp) const
{
	FString Filename = FModuleManager::Get().GetModuleFilename(ModuleName);
	if (IFileManager::Get().FileSize(*Filename) > 0)
	{
		OutFileTimeStamp = FDateTime(IFileManager::Get().GetTimeStamp(*Filename));
		return true;
	}
	return false;
}

bool FHotReloadModule::IsAnyGameModuleLoaded() const
{
	// Ask the module manager for a list of currently-loaded gameplay modules
	TArray< FModuleStatus > ModuleStatuses;
	FModuleManager::Get().QueryModules(ModuleStatuses);

	for (auto ModuleStatusIt = ModuleStatuses.CreateConstIterator(); ModuleStatusIt; ++ModuleStatusIt)
	{
		const FModuleStatus& ModuleStatus = *ModuleStatusIt;

		// We only care about game modules that are currently loaded
		if (ModuleStatus.bIsLoaded && ModuleStatus.bIsGameModule)
		{
			// There is at least one loaded game module.
			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE