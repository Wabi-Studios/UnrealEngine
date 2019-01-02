// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace BuildGraph.Tasks
{
	/// <summary>
	/// Parameters for a copy task
	/// </summary>
	public class CopyTaskParameters
	{
		/// <summary>
		/// Filter to be applied to the list of input files. Optional.
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.FileSpec)]
		public string Files;

		/// <summary>
		/// The pattern(s) to copy from (eg. Engine/*.txt)
		/// </summary>
		[TaskParameter(ValidationType = TaskParameterValidationType.FileSpec)]
		public string From;

		/// <summary>
		/// The directory or to copy to
		/// </summary>
		[TaskParameter(ValidationType = TaskParameterValidationType.FileSpec)]
		public string To;

		/// <summary>
		/// Whether or not to overwrite existing files
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Overwrite = true;

		/// <summary>
		/// Tag to be applied to build products of this task
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.TagList)]
		public string Tag;
	}

	/// <summary>
	/// Copies files from one directory to another.
	/// </summary>
	[TaskElement("Copy", typeof(CopyTaskParameters))]
	public class CopyTask : CustomTask
	{
		/// <summary>
		/// Parameters for this task
		/// </summary>
		CopyTaskParameters Parameters;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InParameters">Parameters for this task</param>
		public CopyTask(CopyTaskParameters InParameters)
		{
			Parameters = InParameters;
		}

		/// <summary>
		/// Execute the task.
		/// </summary>
		/// <param name="Job">Information about the current job</param>
		/// <param name="BuildProducts">Set of build products produced by this node.</param>
		/// <param name="TagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override void Execute(JobContext Job, HashSet<FileReference> BuildProducts, Dictionary<string, HashSet<FileReference>> TagNameToFileSet)
		{
			// Parse all the source patterns
			FilePattern SourcePattern = new FilePattern(CommandUtils.RootDirectory, Parameters.From);

			// Parse the target pattern
			FilePattern TargetPattern = new FilePattern(CommandUtils.RootDirectory, Parameters.To);

			// Apply the filter to the source files
			HashSet<FileReference> Files = null;
			if(!String.IsNullOrEmpty(Parameters.Files))
			{
				SourcePattern = SourcePattern.AsDirectoryPattern();
				Files = ResolveFilespec(SourcePattern.BaseDirectory, Parameters.Files, TagNameToFileSet);
			}

			// Build the file mapping
			Dictionary<FileReference, FileReference> TargetFileToSourceFile = FilePattern.CreateMapping(Files, ref SourcePattern, ref TargetPattern);

			//  If we're not overwriting, remove any files where the destination file already exists.
			if(!Parameters.Overwrite)
			{
				TargetFileToSourceFile = TargetFileToSourceFile.Where(File =>
				{
					if (FileReference.Exists(File.Key))
					{
						CommandUtils.LogInformation("Not copying existing file {0}", File.Key);
						return false;
					}
					return true;
				}).ToDictionary(Pair => Pair.Key, Pair => Pair.Value);
			}

			// Check we got some files
			if(TargetFileToSourceFile.Count == 0)
			{
				CommandUtils.LogInformation("No files found matching '{0}'", SourcePattern);
				return;
			}

			// If the target is on a network share, retry creating the first directory until it succeeds
			DirectoryReference FirstTargetDirectory = TargetFileToSourceFile.First().Key.Directory;
			if(!DirectoryReference.Exists(FirstTargetDirectory))
			{
				const int MaxNumRetries = 15;
				for(int NumRetries = 0;;NumRetries++)
				{
					try
					{
						DirectoryReference.CreateDirectory(FirstTargetDirectory);
						if(NumRetries == 1)
						{
							Log.TraceInformation("Created target directory {0} after 1 retry.", FirstTargetDirectory);
						}
						else if(NumRetries > 1)
						{
							Log.TraceInformation("Created target directory {0} after {1} retries.", FirstTargetDirectory, NumRetries);
						}
						break;
					}
					catch(Exception Ex)
					{
						if(NumRetries == 0)
						{
							Log.TraceInformation("Unable to create directory '{0}' on first attempt. Retrying {1} times...", FirstTargetDirectory, MaxNumRetries);
						}

						Log.TraceLog("  {0}", Ex);

						if(NumRetries >= 15)
						{
							throw new AutomationException(Ex, "Unable to create target directory '{0}' after {1} retries.", FirstTargetDirectory, NumRetries);
						}

						Thread.Sleep(2000);
					}
				}
			}

			// Copy them all
			KeyValuePair<FileReference, FileReference>[] FilePairs = TargetFileToSourceFile.ToArray();
			CommandUtils.LogInformation("Copying {0} file{1} from {2} to {3}...", FilePairs.Length, (FilePairs.Length == 1)? "" : "s", SourcePattern.BaseDirectory, TargetPattern.BaseDirectory);
			foreach(KeyValuePair<FileReference, FileReference> FilePair in FilePairs)
			{
				CommandUtils.LogLog("  {0} -> {1}", FilePair.Key, FilePair.Value);
			}
			CommandUtils.ThreadedCopyFiles(FilePairs.Select(x => x.Value.FullName).ToList(), FilePairs.Select(x => x.Key.FullName).ToList(), bQuiet: true);

			// Update the list of build products
			BuildProducts.UnionWith(TargetFileToSourceFile.Keys);

			// Apply the optional output tag to them
			foreach(string TagName in FindTagNamesFromList(Parameters.Tag))
			{
				FindOrAddTagSet(TagNameToFileSet, TagName).UnionWith(TargetFileToSourceFile.Keys);
			}
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter Writer)
		{
			Write(Writer, Parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			foreach(string TagName in FindTagNamesFromFilespec(Parameters.Files))
			{
				yield return TagName;
			}
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			return FindTagNamesFromList(Parameters.Tag);
		}
	}
}
