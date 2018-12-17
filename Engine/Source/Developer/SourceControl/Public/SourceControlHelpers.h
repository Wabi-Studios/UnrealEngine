// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "ISourceControlRevision.h"
#include "UObject/TextProperty.h"
#include "SourceControlHelpers.generated.h"


class ISourceControlProvider;

/**
 * Snapshot of source control state of for a file
 * @see	USourceControlHelpers::QueryFileState()
 */
USTRUCT(BlueprintType)
struct FSourceControlState
{
	GENERATED_BODY()

public:

	/** Get the local filename that this state represents */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	FString Filename;

	/** Indicates whether this source control state has valid information (true) or not (false) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsValid;

	/** Determine if we know anything about the source control state of this file */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsUnknown;

	/** Determine if this file can be checked in. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanCheckIn;

	/** Determine if this file can be checked out */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanCheckOut;

	/** Determine if this file is checked out */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsCheckedOut;

	/** Determine if this file is up-to-date with the version in source control */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsCurrent;

	/** Determine if this file is under source control */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsSourceControlled;

	/**
	 * Determine if this file is marked for add
	 * @note	if already checked in then not considered mid add
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsAdded;

	/** Determine if this file is marked for delete */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsDeleted;

	/** Determine if this file is ignored by source control */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsIgnored;

	/** Determine if source control allows this file to be edited */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanEdit;

	/** Determine if source control allows this file to be deleted. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanDelete;

	/** Determine if this file is modified compared to the version in source control. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsModified;

	/** 
	 * Determine if this file can be added to source control (i.e. is part of the directory 
	 * structure currently under source control) 
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanAdd;

	/** Determine if this file is in a conflicted state */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsConflicted;

	/** Determine if this file can be reverted, i.e. discard changes and the file will no longer be checked-out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bCanRevert;

	/** Determine if this file is checked out by someone else */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	bool bIsCheckedOutOther;

	/**
	 * Get name of other user who this file already checked out or "" if no other user has it checked out
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Editor Scripting | Editor Source Control Helpers")
	FString CheckedOutOther;

};  // FSourceControlState


/** 
 * Delegate used for performing operation on files that may need a checkout, but before they are added to source control 
 * @param	InDestFile			The filename that was potentially checked out
 * @param	InFileDescription	Description of the file to display to the user, e.g. "Text" or "Image"
 * @param	OutFailReason		Text describing why the operation failed
 * @return true if the operation was successful
 */
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnPostCheckOut, const FString& /*InDestFile*/, const FText& /*InFileDescription*/, FText& /*OutFailReason*/);

// For backwards compatibility
typedef class USourceControlHelpers SourceControlHelpers;

/**
 * Editor source control common functionality.
 *
 * @note Many of these source control methods use *smart* file strings which can be one of:
 *   - fully qualified path
 *   - relative path
 *   - long package name
 *   - asset path
 *   - export text path (often stored on clipboard)
 *
 *   For example:
 *	 - D:\Epic\Dev-Ent\Projects\Python3rdBP\Content\Mannequin\Animations\ThirdPersonIdle.uasset
 *	 - Content\Mannequin\Animations\ThirdPersonIdle.uasset
 *	 - /Game/Mannequin/Animations/ThirdPersonIdle
 *	 - /Game/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle
 *	 - AnimSequence'/Game/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle'
 */
UCLASS(Abstract, Transient, meta = (ScriptName = "SourceControl"))
class SOURCECONTROL_API USourceControlHelpers : public UObject
{	// Note - would use UBlueprintFunctionLibrary however it requires Engine module which breaks some
	// other modules that depend on the SourceControl module and expect to be Engine independent.

	GENERATED_BODY()

public:
	/**
	 * Determine the name of the current source control provider.
	 * @return	the name of the current source control provider. If one is not set then "None" is returned.
	 */
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | Editor Source Control Helpers")
	static FString CurrentProvider();

	/**
	 * Determine if there is a source control system enabled
	 * @return	true if enabled, false if not
	 */
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool IsEnabled();

	/**
	 * Quick check if currently set source control provider is enabled and available for use
	 * (server-based providers can use this to return whether the server is available or not)
	 *
	 * @return	true if source control is available, false if it is not
	 */
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool IsAvailable();

	/**
	* Get status text set by SourceControl system if an error occurs regardless whether bSilent is set or not.
	* Only set if there was an error.
	*/
	UFUNCTION(BlueprintPure, Category = "Editor Scripting | Editor Source Control Helpers")
	static FText LastErrorMsg();

	/**
	 * Use currently set source control provider to check out a file.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile		The file to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CheckOutFile(const FString& InFile, bool bSilent = false);

	/**
	 * Use currently set source control provider to check out specified files.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFiles		Files to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CheckOutFiles(const TArray<FString>& InFiles, bool bSilent = false);

	/**
	 * Use currently set source control provider to check out file or mark it for add.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile		The file to check out/add - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CheckOutOrAddFile(const FString& InFile, bool bSilent = false);

	/**
	 * Helper function perform an operation on files in our 'source controlled' directories, handling checkout/add etc.
	 * @note	Blocks until action is complete. Older C++ only version of CheckOutOrAddFile().
	 *
	 * @param	InDestFile			The path to the destination file
	 * @param	InFileDescription	Description of the file to display to the user, e.g. "Text" or "Image"
	 * @param	OnPostCheckOut		Delegate used for performing operation on files that may need a checkout, but before they are added to source control 
	 * @param	OutFailReason		Text describing why the operation failed
	 * @return	Success or failure of the operation
	 * @see     CheckOutOrAddFile()
	 */
	static bool CheckoutOrMarkForAdd(const FString& InDestFile, const FText& InFileDescription, const FOnPostCheckOut& OnPostCheckOut, FText& OutFailReason);

	/**
	 * Use currently set source control provider to mark a file for add. Does nothing (and returns true) if the file is already under SC
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile		The file to add - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool MarkFileForAdd(const FString& InFile, bool bSilent = false);

	/**
	 * Use currently set source control provider to mark files for add. Does nothing (and returns true) for any file that is already under SC
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFiles		Files to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool MarkFilesForAdd(const TArray<FString>& InFiles, bool bSilent = false);

	/**
	 * Use currently set source control provider to remove file from source control and
	 * delete the file.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile		The file to delete - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent		if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool MarkFileForDelete(const FString& InFile, bool bSilent = false);

	/**
	 * Use currently set source control provider to revert a file regardless whether any changes will be lost or not.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile	The file to revert - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent	if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool RevertFile(const FString& InFile, bool bSilent = false);

	/**
	 * Use currently set source control provider to revert files regardless whether any changes will be lost or not.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFiles	Files to revert - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent	if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool RevertFiles(const TArray<FString>& InFiles, bool bSilent = false);

	/**
	 * Use currently set source control provider to revert a file provided no changes have been made.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile	File to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent	if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool RevertUnchangedFile(const FString& InFile, bool bSilent = false);

	/**
	 * Use currently set source control provider to revert files provided no changes have been made.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFiles	Files to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent	if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool RevertUnchangedFiles(const TArray<FString>& InFiles, bool bSilent = false);

	/**
	 * Helper function to get a list of files that are unchanged & revert them. This runs synchronous commands.
	 * @note	Older C++ only version of RevertFiles(Files, false).
	 * @see		RevertFiles()
	 *
	 * @param	InProvider	The provider to use
	 * @param	InFiles		The files to operate on
	 */
	static void RevertUnchangedFiles(ISourceControlProvider& InProvider, const TArray<FString>& InFiles);

	/**
	 * Use currently set source control provider to check in a file.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile			The file to check in - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	InDescription	Description for check in
	 * @param	bSilent			if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CheckInFile(const FString& InFile, const FString& InDescription, bool bSilent = false);

	/**
	 * Use currently set source control provider to check in specified files.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFiles			Files to check out - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	InDescription	Description for check in
	 * @param	bSilent			if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CheckInFiles(const TArray<FString>& InFiles, const FString& InDescription, bool bSilent = false);

	/**
	 * Use currently set source control provider to copy a file.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InSourceFile	Source file string to copy from - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	InDestFile		Source file string to copy to - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard). If package, then uses same extension as source file.
	 * @param	bSilent			if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	true if succeeded, false if failed and can call LastErrorMsg() for more info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static bool CopyFile(const FString& InSourceFile, const FString& InDestFile, bool bSilent = false);

	/**
	 * Helper function to copy a file into our 'source controlled' directories, handling checkout/add etc.
	 * @note	Blocks until action is complete. Older C++ only version of CopyFile().
	 *
	 * @param	InDestFile			The path to the destination file
	 * @param	InSourceFile		The path to the source file
	 * @param	InFileDescription	Description of the file to display to the user, e.g. "Text" or "Image"
	 * @param	OutFailReason		Text describing why the operation failed
	 * @return	Success or failure of the operation
	 */
	static bool CopyFileUnderSourceControl(const FString& InDestFile, const FString& InSourceFile, const FText& InFileDescription, FText& OutFailReason);

	/**
	 * Use currently set source control provider to query a file's source control state.
	 * @note	Blocks until action is complete.
	 *
	 * @param	InFile			The file to query - can be either fully qualified path, relative path, long package name, asset path or export text path (often stored on clipboard)
	 * @param	bSilent			if false (default) then write out any error info to the Log. Any error text can be retrieved by LastErrorMsg() regardless.
	 * @return	Source control state - see USourceControlState. It will have bIsValid set to false if
	 *			it could not have its values set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting | Editor Source Control Helpers")
	static FSourceControlState QueryFileState(const FString& InFile, bool bSilent = false);


	/**
	 * Helper function to get a filename for a package name.
	 * @param	InPackageName	The package name to get the filename for
	 * @return the filename
	 */
	static FString PackageFilename(const FString& InPackageName);

	/**
	 * Helper function to get a filename for a package.
	 * @param	InPackage	The package to get the filename for
	 * @return the filename
	 */
	static FString PackageFilename(const UPackage* InPackage);

	/**
	 * Helper function to convert package name array into a filename array.
	 * @param	InPackageNames	The package name array
	 * @return an array of filenames
	 */
	static TArray<FString> PackageFilenames(const TArray<FString>& InPackageNames);

	/**
	 * Helper function to convert package array into filename array.
	 * @param	InPackages	The package array
	 * @return an array of filenames
	 */
	static TArray<FString> PackageFilenames(const TArray<UPackage*>& InPackages);

	/**
	 * Helper function to convert a filename array to absolute paths.
	 * @param	InFileNames	The filename array
	 * @return an array of filenames, transformed into absolute paths
	 */
	static TArray<FString> AbsoluteFilenames(const TArray<FString>& InFileNames);

	/**
	 * Helper function to annotate a file using a label
	 * @param	InProvider	The provider to use
	 * @param	InLabel		The label to use to retrieve the file
	 * @param	InFile		The file to annotate
	 * @param	OutLines	Output array of annotated lines
	 * @returns true if successful
	 */
	static bool AnnotateFile(ISourceControlProvider& InProvider, const FString& InLabel, const FString& InFile, TArray<FAnnotationLine>& OutLines);

	/**
	 * Helper function to annotate a file using a changelist/checkin identifier
	 * @param	InProvider				The provider to use
	 * @param	InCheckInIdentifier		The changelist/checkin identifier to use to retrieve the file
	 * @param	InFile					The file to annotate
	 * @param	OutLines				Output array of annotated lines
	 * @returns true if successful
	 */
	static bool AnnotateFile(ISourceControlProvider& InProvider, int32 InCheckInIdentifier, const FString& InFile, TArray<FAnnotationLine>& OutLines);

	/**
	 * Helper function to branch/integrate packages from one location to another
	 * @param	DestPackage			The destination package
	 * @param	SourcePackage		The source package
	 * @return true if the file packages were successfully branched.
	 */
	static bool BranchPackage(UPackage* DestPackage, UPackage* SourcePackage );

	/**
	 * Helper function to get the ini filename for storing source control settings
	 * @return the filename
	 */
	static const FString& GetSettingsIni();

	/**
	 * Helper function to get the ini filename for storing global source control settings
	 * @return the filename
	 */
	static const FString& GetGlobalSettingsIni();

};  // USourceControlHelpers


/** 
 * Helper class that ensures FSourceControl is properly initialized and shutdown by calling Init/Close in
 * its constructor/destructor respectively.
 */
class SOURCECONTROL_API FScopedSourceControl
{
public:
	/** Constructor; Initializes Source Control Provider */
	FScopedSourceControl();

	/** Destructor; Closes Source Control Provider */
	~FScopedSourceControl();

	/** Get the provider we are using */
	ISourceControlProvider& GetProvider();
};
