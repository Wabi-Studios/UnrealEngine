// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "BuildPatchInstaller.h"

class FHttpServiceTracker;
class IAnalyticsProvider;

/**
 * Constant values and typedefs
 */
enum
{
	// Sizes
	FileBufferSize		= 1024*1024*4,		// When reading from files, how much to buffer
	StreamBufferSize	= FileBufferSize*4,	// When reading from build data stream, how much to buffer.
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
/**
 * Implements the BuildPatchServicesModule.
 */
class FBuildPatchServicesModule
	: public IBuildPatchServicesModule
{
public:

	// IModuleInterface interface begin.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// IModuleInterface interface end.

	// IBuildPatchServicesModule interface begin.
	virtual BuildPatchServices::IBuildStatisticsRef CreateBuildStatistics(const IBuildInstallerRef& Installer) const override;
	virtual IBuildManifestPtr LoadManifestFromFile(const FString& Filename) override;
	virtual IBuildManifestPtr MakeManifestFromData(const TArray<uint8>& ManifestData) override;
	virtual bool SaveManifestToFile(const FString& Filename, IBuildManifestRef Manifest) override;
	virtual TSet<FString> GetInstalledPrereqIds() const override;
	virtual IBuildInstallerPtr StartBuildInstall(IBuildManifestPtr CurrentManifest, IBuildManifestPtr InstallManifest, const FString& InstallDirectory, FBuildPatchBoolManifestDelegate OnCompleteDelegate, bool bIsRepair = false, TSet<FString> InstallTags = TSet<FString>()) override;
	virtual IBuildInstallerPtr StartBuildInstallStageOnly(IBuildManifestPtr CurrentManifest, IBuildManifestPtr InstallManifest, const FString& InstallDirectory, FBuildPatchBoolManifestDelegate OnCompleteDelegate, bool bIsRepair = false, TSet<FString> InstallTags = TSet<FString>()) override;
	virtual IBuildInstallerRef StartBuildInstall(BuildPatchServices::FInstallerConfiguration Configuration, FBuildPatchBoolManifestDelegate OnCompleteDelegate) override;
	virtual const TArray<IBuildInstallerRef>& GetInstallers() const override;
	virtual void SetStagingDirectory(const FString& StagingDir) override;
	virtual void SetCloudDirectory(FString CloudDir) override;
	virtual void SetCloudDirectories(TArray<FString> CloudDirs) override;
	virtual void SetBackupDirectory(const FString& BackupDir) override;
	virtual void SetAnalyticsProvider(TSharedPtr< IAnalyticsProvider > AnalyticsProvider) override;
	virtual void SetHttpTracker(TSharedPtr< FHttpServiceTracker > HttpTracker) override;
	virtual void RegisterAppInstallation(IBuildManifestRef AppManifest, const FString AppInstallDirectory) override;
	virtual void CancelAllInstallers(bool WaitForThreads) override;
	virtual bool GenerateChunksManifestFromDirectory(const BuildPatchServices::FGenerationConfiguration& Settings) override;
	virtual bool CompactifyCloudDirectory(const FString& CloudDirectory, float DataAgeThreshold, ECompactifyMode::Type Mode, const FString& DeletedChunkLogFile) override;
	virtual bool EnumeratePatchData(const FString& InputFile, const FString& OutputFile, bool bIncludeSizes) override;
	virtual bool VerifyChunkData(const FString& SearchPath, const FString& OutputFile) override;
	virtual bool PackageChunkData(const FString& ManifestFilePath, const FString& PrevManifestFilePath, const TArray<TSet<FString>>& TagSetArray, const FString& OutputFile, const FString& CloudDir, uint64 MaxOutputFileSize, const FString& ResultDataFilePath) override;
	virtual bool MergeManifests(const FString& ManifestFilePathA, const FString& ManifestFilePathB, const FString& ManifestFilePathC, const FString& NewVersionString, const FString& SelectionDetailFilePath) override;
	virtual bool DiffManifests(const FString& ManifestFilePathA, const TSet<FString>& TagSetA, const FString& ManifestFilePathB, const TSet<FString>& TagSetB, const TArray<TSet<FString>>& CompareTagSets, const FString& OutputFilePath) override;
	virtual IBuildManifestPtr MakeManifestFromJSON(const FString& ManifestJSON) override;
	// IBuildPatchServicesModule interface end.

	/**
	 * Gets the directory used for staging intermediate files.
	 * @return	The staging directory
	 */
	static const FString& GetStagingDirectory();

	/**
	 * Gets the cloud directory where chunks and manifests will be pulled from.
	 * @param CloudIdx    Optional override for which cloud directory to get. This value will wrap within the range of available cloud directories.
	 * @return	The cloud directory
	 */
	static FString GetCloudDirectory(int32 CloudIdx = 0);

	/**
	 * Gets the cloud directories where chunks and manifests will be pulled from.
	 * @return	The cloud directories
	 */
	static TArray<FString> GetCloudDirectories();

	/**
	 * Gets the backup directory for saving files clobbered by repair/patch.
	 * @return	The backup directory
	 */
	static const FString& GetBackupDirectory();

private:
	/**
	 * Tick function to monitor installers for completion, so that we can call delegates on the main thread
	 * @param		Delta	Time since last tick
	 * @return	Whether to continue ticking
	 */
	bool Tick(float Delta);

	/**
	 * This will get called when core PreExits. Make sure any running installers are canceled out.
	 */
	void PreExit();

	/**
	 * Called during init to perform any fix up required to new configuration.
	 */
	void FixupLegacyConfig();

	/**
	 * Helper function to normalize the provided directory list.
	 */
	void NormalizeCloudPaths(TArray<FString>& InOutCloudPaths);

private:
	// The analytics provider interface
	static TSharedPtr<IAnalyticsProvider> Analytics;

	// The http tracker service interface
	static TSharedPtr<FHttpServiceTracker> HttpTracker;

	// Holds the cloud directories where chunks should belong
	static TArray<FString> CloudDirectories;

	// Holds the staging directory where we can perform any temporary work
	static FString StagingDirectory;

	// Holds the backup directory where we can move files that will be clobbered by repair or patch
	static FString BackupDirectory;

	// Holds the filename for local machine config. This is instead of shipped or user config, to track machine installation config.
	FString LocalMachineConfigFile;

	// A flag specifying whether prerequisites install should be skipped
	bool bForceSkipPrereqs;

	// Array of created installers
	TArray<FBuildPatchInstallerPtr> BuildPatchInstallers;

	// Array of created installers as exposable interface refs
	TArray<IBuildInstallerRef> BuildPatchInstallerInterfaces;

	// Holds available installations used for recycling install data
	TMap<FString, FBuildPatchAppManifestRef> AvailableInstallations;

	// Handle to the registered Tick delegate
	FDelegateHandle TickDelegateHandle;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS
