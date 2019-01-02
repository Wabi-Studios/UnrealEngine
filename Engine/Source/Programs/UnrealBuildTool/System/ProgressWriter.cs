// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Class to display an incrementing progress percentage. Handles progress markup and direct console output.
	/// </summary>
	public class ProgressWriter : IDisposable
	{
		/// <summary>
		/// Global setting controlling whether to output markup
		/// </summary>
		public static bool bWriteMarkup = false;

		/// <summary>
		/// The name to include with the status message
		/// </summary>
		string Message;

		/// <summary>
		/// The inner scope object
		/// </summary>
		LogStatusScope Status;

		/// <summary>
		/// The current progress message
		/// </summary>
		string CurrentProgressString;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InMessage">The message to display before the progress percentage</param>
		/// <param name="bInUpdateStatus">Whether to write messages to the console</param>
		public ProgressWriter(string InMessage, bool bInUpdateStatus)
		{
			Message = InMessage;
			if(bInUpdateStatus)
			{
				Status = new LogStatusScope(InMessage);
			}
			Write(0, 100);
		}

		/// <summary>
		/// Write the terminating newline
		/// </summary>
		public void Dispose()
		{
			if(Status != null)
			{
				Status.Dispose();
				Status = null;
			}
		}

		/// <summary>
		/// Writes the current progress
		/// </summary>
		/// <param name="Numerator">Numerator for the progress fraction</param>
		/// <param name="Denominator">Denominator for the progress fraction</param>
		public void Write(int Numerator, int Denominator)
		{
			float ProgressValue = Denominator > 0 ? ((float)Numerator / (float)Denominator) : 1.0f;
			string ProgressString = String.Format("{0}%", Math.Round(ProgressValue * 100.0f));
			if (ProgressString != CurrentProgressString)
			{
				CurrentProgressString = ProgressString;
				if (bWriteMarkup)
				{
					Log.WriteLine(LogEventType.Console, "@progress '{0}' {1}", Message, ProgressString);
				}
				if(Status != null)
				{
					Status.SetProgress(ProgressString);
				}
			}
		}
	}
}
