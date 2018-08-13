// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "ToolModes/PackageChunksMode.h"
#include "Interfaces/IBuildPatchServicesModule.h"
#include "BuildPatchTool.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Algo/Find.h"

using namespace BuildPatchTool;

class FPackageChunksToolMode : public IToolMode
{
public:
	FPackageChunksToolMode(IBuildPatchServicesModule& InBpsInterface)
		: BpsInterface(InBpsInterface)
	{}

	virtual ~FPackageChunksToolMode()
	{}

	virtual EReturnCode Execute() override
	{
		// Parse commandline.
		if (ProcessCommandline() == false)
		{
			return EReturnCode::ArgumentProcessingError;
		}

		// Print help if requested.
		if (bHelp)
		{
			UE_LOG(LogBuildPatchTool, Log, TEXT("PACKAGE CHUNKS MODE"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("This tool mode supports packaging data required for an installation into larger files which can be used as local sources for build patch installers."));
			UE_LOG(LogBuildPatchTool, Log, TEXT(""));
			UE_LOG(LogBuildPatchTool, Log, TEXT("Required arguments:"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -mode=PackageChunks  Must be specified to launch the tool in package chunks mode."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -ManifestFile=\"\"     Specifies in quotes the file path to the manifest to enumerate chunks from."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -OutputFile=\"\"       Specifies in quotes the file path the output package. Extension of .chunkdb will be added if not present."));
			UE_LOG(LogBuildPatchTool, Log, TEXT(""));
			UE_LOG(LogBuildPatchTool, Log, TEXT("Optional arguments:"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -PrevManifestFile=\"\" Specifies in quotes the file path to a manifest for a previous build, this will be used to filter out chunks, such that the"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("                       produced chunkdb files will only contain chunks required to patch from this build to the one described by ManifestFile."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -CloudDir=\"\"         Specifies in quotes the cloud directory where chunks to be packaged can be found."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -MaxOutputFileSize=  When specified, the size of each output file (in bytes) will be limited to a maximum of the provided value."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -ResultDataFile=\"\"   Specifies in quotes the file path where the results will be exported as a JSON object."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("  -TagSets=\"t1,t2\"     Specifies in quotes a comma seperated list of tags for filtering of data saved. Multiple sets can be provided to split the chunkdb files by tagsets."));
			UE_LOG(LogBuildPatchTool, Log, TEXT(""));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: If CloudDir is not specified, the manifest file location will be used as the cloud directory."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: MaxOutputFileSize is recommended to be as large as possible. The minimum individual chunkdb filesize is equal to one chunk plus chunkdb"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    header, and thus will not result in efficient behavior."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: If MaxOutputFileSize is not specified, the one output file will be produced containing all required data."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: If MaxOutputFileSize is specified, the output files will be generated as Name.part01.chunkdb, Name.part02.chunkdb etc. The part number will"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    have the number of digits required for highest numbered part."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: If MaxOutputFileSize is specified, then each part can be equal to or less than the specified size, depending on the size of the last chunk"));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    that fits."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("NB: When providing multiple -TagSets= arguments, all data from the first -TagSets= arg will be saved first, followed by any extra data needed for the second -TagSets= arg, and so on in separated chunkdb files."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    Note that this means the chunkdb files produced for the second -TagSets= arg and later will not contain some required data for that tagset if the data already got saved out as part of a previous tagset."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    The chunkdb files are thus additive with no dupes."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    If it is desired that each tagset's chunkdb files contain the duplicate data, then PackageChunks should be executed once per -TagSets= arg rather than once will all -TagSets= args."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    An empty tag must be included in one of the -TagSets= args to include untagged file data in that tagset, e.g. -TagSets=\" , t1\"."));
			UE_LOG(LogBuildPatchTool, Log, TEXT("    Adding no -TagSets= args will include all data."));
			return EReturnCode::OK;
		}

		// Run the enumeration routine.
		bool bSuccess = BpsInterface.PackageChunkData(ManifestFile, PrevManifestFile, TagSetArray, OutputFile, CloudDir, MaxOutputFileSize, ResultDataFile);
		return bSuccess ? EReturnCode::OK : EReturnCode::ToolFailure;
	}

private:

	bool ProcessCommandline()
	{
#define HAS_SWITCH(SwitchVar) (Algo::FindByPredicate(Switches, [](const FString& Elem){ return Elem.StartsWith(TEXT(#SwitchVar "="));}) != nullptr)
#define PARSE_SWITCH(SwitchVar) ParseSwitch(TEXT(#SwitchVar "="), SwitchVar, Switches)
#define PARSE_SWITCHES(SwitchesVar) ParseSwitches(TEXT(#SwitchesVar "="), SwitchesVar, Switches)
		TArray<FString> Tokens, Switches;
		FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);

		bHelp = ParseOption(TEXT("help"), Switches);
		if (bHelp)
		{
			return true;
		}

		// Get all required parameters.
		if (!(PARSE_SWITCH(ManifestFile)
		   && PARSE_SWITCH(OutputFile)))
		{
			UE_LOG(LogBuildPatchTool, Error, TEXT("ManifestFile and OutputFile are required parameters"));
			return false;
		}
		FPaths::NormalizeFilename(ManifestFile);
		FPaths::NormalizeFilename(OutputFile);

		// Get optional parameters.
		PARSE_SWITCH(PrevManifestFile);
		PARSE_SWITCH(ResultDataFile);
		FPaths::NormalizeFilename(PrevManifestFile);
		FPaths::NormalizeFilename(ResultDataFile);
		PARSE_SWITCHES(TagSets);

		if (!PARSE_SWITCH(CloudDir))
		{
			// If not provided we use the location of the manifest file.
			CloudDir = FPaths::GetPath(ManifestFile);
		}
		FPaths::NormalizeDirectoryName(CloudDir);

		if (HAS_SWITCH(MaxOutputFileSize))
		{
			if (!PARSE_SWITCH(MaxOutputFileSize))
			{
				// Failing to parse a provided MaxOutputFileSize is an error.
				UE_LOG(LogBuildPatchTool, Error, TEXT("MaxOutputFileSize must be a valid uint64"));
				return false;
			}
		}
		else
		{
			// If not provided we don't limit the size, which is the equivalent of limiting to max uint64.
			MaxOutputFileSize = MAX_uint64;
		}

		// Process the tagsets that we parsed.
		if (TagSets.Num() > 0)
		{
			for (const FString& TagSet : TagSets)
			{
				TArray<FString> Tags;
				const bool bCullEmpty = false;
				TagSet.ParseIntoArray(Tags, TEXT(","), bCullEmpty);
				for (FString& Tag : Tags)
				{
					Tag.TrimStartAndEndInline();
				}
				// If we ended up with an empty array, the intention would have been to pass a tagset that contains just an empty string, so make that fixup.
				if (Tags.Num() == 0)
				{
					Tags.Add(TEXT(""));
				}
				TagSetArray.Emplace(Tags);
			}
		}

		return true;
#undef PARSE_SWITCHES
#undef PARSE_SWITCH
#undef HAS_SWITCH
	}

private:
	IBuildPatchServicesModule& BpsInterface;
	bool bHelp;
	FString ManifestFile;
	FString PrevManifestFile;
	FString OutputFile;
	FString ResultDataFile;
	FString CloudDir;
	uint64 MaxOutputFileSize;
	TArray<FString> TagSets;
	TArray<TSet<FString>> TagSetArray;
};

BuildPatchTool::IToolModeRef BuildPatchTool::FPackageChunksToolModeFactory::Create(IBuildPatchServicesModule& BpsInterface)
{
	return MakeShareable(new FPackageChunksToolMode(BpsInterface));
}
