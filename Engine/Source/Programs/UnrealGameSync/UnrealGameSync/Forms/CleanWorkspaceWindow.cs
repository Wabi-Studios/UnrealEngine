// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.VisualStyles;

namespace UnrealGameSync
{
	partial class CleanWorkspaceWindow : Form
	{
		enum TreeNodeAction
		{
			Sync,
			Delete,
		}

		class TreeNodeData
		{
			public TreeNodeAction Action;
			public FileInfo? File;
			public FolderToClean? Folder;
			public int NumFiles;
			public int NumSelectedFiles;
			public int NumEmptySelectedFiles;
			public int NumMissingSelectedFiles;
			public int NumDefaultSelectedFiles;
		}

		enum SelectionType
		{
			All,
			SafeToDelete,
			Missing,
			Empty,
			None,
		}

		static readonly CheckBoxState[] CheckBoxStates = 
		{ 
			CheckBoxState.UncheckedNormal, 
			CheckBoxState.MixedNormal, 
			CheckBoxState.CheckedNormal 
		};

		IPerforceSettings PerforceSettings;
		FolderToClean RootFolderToClean;

		static readonly string[] SafeToDeleteFolders =
		{
			"/binaries/",
			"/intermediate/",
			"/build/receipts/",
			"/.vs/",
			"/automationtool/saved/rules/",
			"/saved/logs/",
	
			"/bin/debug/",
			"/bin/development/",
			"/bin/release/",
			"/obj/debug/",
			"/obj/development/",
			"/obj/release/",

			"/bin/x86/debug/",
			"/bin/x86/development/",
			"/bin/x86/release/",
			"/obj/x86/debug/",
			"/obj/x86/development/",
			"/obj/x86/release/",

			"/bin/x64/debug/",
			"/bin/x64/development/",
			"/bin/x64/release/",
			"/obj/x64/debug/",
			"/obj/x64/development/",
			"/obj/x64/release/",
		};

		static readonly string[] SafeToDeleteExtensions =
		{
			".pdb",
			".obj",
			".sdf",
			".suo",
			".sln",
			".csproj.user",
			".csproj.references",
		};

		IReadOnlyList<string> ExtraSafeToDeleteFolders;
		IReadOnlyList<string> ExtraSafeToDeleteExtensions;
		ILogger Logger;

		[DllImport("Shell32.dll", EntryPoint = "ExtractIconExW", CharSet = CharSet.Unicode, ExactSpelling = true, CallingConvention = CallingConvention.StdCall)]
		private static extern int ExtractIconEx(string sFile, int iIndex, IntPtr piLargeVersion, out IntPtr piSmallVersion, int amountIcons);

		private CleanWorkspaceWindow(IPerforceSettings PerforceSettings, FolderToClean RootFolderToClean, string[] ExtraSafeToDeleteFolders, string[] ExtraSafeToDeleteExtensions, ILogger<CleanWorkspaceWindow> Logger)
		{
			this.PerforceSettings = PerforceSettings;
			this.RootFolderToClean = RootFolderToClean;
			this.ExtraSafeToDeleteFolders = ExtraSafeToDeleteFolders.Select(x => x.Trim().Replace('\\', '/').Trim('/')).Where(x => x.Length > 0).Select(x => String.Format("/{0}/", x.ToLowerInvariant())).ToArray();
			this.ExtraSafeToDeleteExtensions = ExtraSafeToDeleteExtensions.Select(x => x.Trim().ToLowerInvariant()).Where(x => x.Length > 0).ToArray();
			this.Logger = Logger;

			InitializeComponent();
		}

		public static void DoClean(IWin32Window Owner, IPerforceSettings PerforceSettings, DirectoryReference LocalRootPath, string ClientRootPath, IReadOnlyList<string> SyncPaths, string[] ExtraSafeToDeleteFolders, string[] ExtraSafeToDeleteExtensions, ILogger<CleanWorkspaceWindow> Logger)
		{
			// Figure out which folders to clean
			FolderToClean RootFolderToClean = new FolderToClean(LocalRootPath.ToDirectoryInfo());
			using(FindFoldersToCleanTask QueryWorkspace = new FindFoldersToCleanTask(PerforceSettings, RootFolderToClean, ClientRootPath, SyncPaths, Logger))
			{
				ModalTask? Result = ModalTask.Execute(Owner, "Clean Workspace", "Querying files in Perforce, please wait...", x => QueryWorkspace.RunAsync(x), ModalTaskFlags.None);
				if (Result == null || !Result.Succeeded)
				{
					return;
				}
			}

			// If there's nothing to delete, don't bother displaying the dialog at all
			if(RootFolderToClean.FilesToDelete.Count == 0 && RootFolderToClean.NameToSubFolder.Count == 0)
			{
				MessageBox.Show("You have no local files which are not in Perforce.", "Workspace Clean", MessageBoxButtons.OK);
				return;
			}

			// Populate the tree
			CleanWorkspaceWindow CleanWorkspace = new CleanWorkspaceWindow(PerforceSettings, RootFolderToClean, ExtraSafeToDeleteFolders, ExtraSafeToDeleteExtensions, Logger);
			CleanWorkspace.ShowDialog();
		}

		private void CleanWorkspaceWindow_Load(object sender, EventArgs e)
		{
			IntPtr FolderIconPtr;
			ExtractIconEx("imageres.dll", 3, IntPtr.Zero, out FolderIconPtr, 1);

			IntPtr FileIconPtr;
			ExtractIconEx("imageres.dll", 2, IntPtr.Zero, out FileIconPtr, 1);

			Icon[] Icons = new Icon[]{ Icon.FromHandle(FolderIconPtr), Icon.FromHandle(FileIconPtr) };

			Size LargestIconSize = Size.Empty;
			foreach(Icon Icon in Icons)
			{
				LargestIconSize = new Size(Math.Max(LargestIconSize.Width, Icon.Width), Math.Max(LargestIconSize.Height, Icon.Height));
			}

			Size LargestCheckBoxSize = Size.Empty;
			using(Graphics Graphics = Graphics.FromHwnd(IntPtr.Zero))
			{
				foreach(CheckBoxState State in CheckBoxStates)
				{
					Size CheckBoxSize = CheckBoxRenderer.GetGlyphSize(Graphics, State);
					LargestCheckBoxSize = new Size(Math.Max(LargestCheckBoxSize.Width, CheckBoxSize.Width), Math.Max(LargestCheckBoxSize.Height, CheckBoxSize.Height));
				}
			}

			Size ImageSize = new Size(LargestCheckBoxSize.Width + LargestIconSize.Width, Math.Max(LargestIconSize.Height, LargestCheckBoxSize.Height));

			Bitmap TypeImageListBitmap = new Bitmap(Icons.Length * 3 * ImageSize.Width, ImageSize.Height, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
			using(Graphics Graphics = Graphics.FromImage(TypeImageListBitmap))
			{
				int MinX = 0;
				for(int IconIdx = 0; IconIdx < Icons.Length; IconIdx++)
				{
					for(int StateIdx = 0; StateIdx < 3; StateIdx++)
					{
						Size CheckBoxSize = CheckBoxRenderer.GetGlyphSize(Graphics, CheckBoxStates[StateIdx]);
						CheckBoxRenderer.DrawCheckBox(Graphics, new Point(MinX + (LargestCheckBoxSize.Width - CheckBoxSize.Width) / 2, (LargestCheckBoxSize.Height - CheckBoxSize.Height) / 2), CheckBoxStates[StateIdx]);

						Size IconSize = Icons[IconIdx].Size;
						Graphics.DrawIcon(Icons[IconIdx], MinX + LargestCheckBoxSize.Width + (LargestIconSize.Width - IconSize.Width) / 2, (LargestIconSize.Height - IconSize.Height) / 2);

						MinX += ImageSize.Width;
					}
				}
			}

			ImageList TypeImageList = new ImageList();
			TypeImageList.ImageSize = ImageSize;
			TypeImageList.ColorDepth = ColorDepth.Depth32Bit;
			TypeImageList.Images.AddStrip(TypeImageListBitmap);
			TreeView.ImageList = TypeImageList;

			TreeNode Node = BuildTreeViewStructure(RootFolderToClean, "/", false, 0);
			Node.Text = RootFolderToClean.Directory.FullName;
			TreeView.Nodes.Add(Node);
		}

		private TreeNode BuildTreeViewStructure(FolderToClean Folder, string FolderPath, bool bParentFolderSelected, int Depth)
		{
			bool bSelectFolder = bParentFolderSelected || IsSafeToDeleteFolder(FolderPath) || Folder.bEmptyLeaf;

			TreeNodeData FolderNodeData = new TreeNodeData();
			FolderNodeData.Folder = Folder;

			TreeNode FolderNode = new TreeNode();
			FolderNode.Text = Folder.Name;
			FolderNode.Tag = FolderNodeData;

			foreach(FolderToClean SubFolder in Folder.NameToSubFolder.OrderBy(x => x.Key).Select(x => x.Value))
			{
				TreeNode ChildNode = BuildTreeViewStructure(SubFolder, FolderPath + SubFolder.Name.ToLowerInvariant() + "/", bSelectFolder, Depth + 1);
				FolderNode.Nodes.Add(ChildNode);

				TreeNodeData ChildNodeData = (TreeNodeData)ChildNode.Tag;
				FolderNodeData.NumFiles += ChildNodeData.NumFiles;
				FolderNodeData.NumSelectedFiles += ChildNodeData.NumSelectedFiles;
				FolderNodeData.NumEmptySelectedFiles += ChildNodeData.NumEmptySelectedFiles;
				FolderNodeData.NumMissingSelectedFiles += ChildNodeData.NumMissingSelectedFiles;
				FolderNodeData.NumDefaultSelectedFiles += ChildNodeData.NumDefaultSelectedFiles;
			}

			foreach(FileInfo File in Folder.FilesToSync.OrderBy(x => x.Name))
			{
				TreeNodeData FileNodeData = new TreeNodeData();
				FileNodeData.Action = TreeNodeAction.Sync;
				FileNodeData.File = File;
				FileNodeData.NumFiles = 1;
				FileNodeData.NumSelectedFiles = 1;
				FileNodeData.NumEmptySelectedFiles = 0;
				FileNodeData.NumMissingSelectedFiles = 1;
				FileNodeData.NumDefaultSelectedFiles = FileNodeData.NumSelectedFiles;

				TreeNode FileNode = new TreeNode();
				FileNode.Text = File.Name + " (sync)";
				FileNode.Tag = FileNodeData;
				FolderNode.Nodes.Add(FileNode);

				UpdateImage(FileNode);

				FolderNodeData.NumFiles++;
				FolderNodeData.NumSelectedFiles += FileNodeData.NumSelectedFiles;
				FolderNodeData.NumEmptySelectedFiles += FileNodeData.NumEmptySelectedFiles;
				FolderNodeData.NumMissingSelectedFiles += FileNodeData.NumMissingSelectedFiles;
				FolderNodeData.NumDefaultSelectedFiles += FileNodeData.NumDefaultSelectedFiles;
			}

			foreach(FileInfo File in Folder.FilesToDelete.OrderBy(x => x.Name))
			{
				string Name = File.Name.ToLowerInvariant();

				bool bSelectFile = bSelectFolder || IsSafeToDeleteFile(FolderPath, File.Name.ToLowerInvariant());

				TreeNodeData FileNodeData = new TreeNodeData();
				FileNodeData.Action = TreeNodeAction.Delete;
				FileNodeData.File = File;
				FileNodeData.NumFiles = 1;
				FileNodeData.NumSelectedFiles = bSelectFile? 1 : 0;
				FileNodeData.NumEmptySelectedFiles = 0;
				FileNodeData.NumMissingSelectedFiles = 0;
				FileNodeData.NumDefaultSelectedFiles = FileNodeData.NumSelectedFiles;

				TreeNode FileNode = new TreeNode();
				FileNode.Text = File.Name;
				FileNode.Tag = FileNodeData;
				FolderNode.Nodes.Add(FileNode);

				UpdateImage(FileNode);

				FolderNodeData.NumFiles++;
				FolderNodeData.NumSelectedFiles += FileNodeData.NumSelectedFiles;
				FolderNodeData.NumEmptySelectedFiles += FileNodeData.NumEmptySelectedFiles;
				FolderNodeData.NumMissingSelectedFiles += FileNodeData.NumMissingSelectedFiles;
				FolderNodeData.NumDefaultSelectedFiles += FileNodeData.NumDefaultSelectedFiles;
			}

			if(FolderNodeData.Folder.bEmptyLeaf)
			{
				FolderNodeData.NumFiles++;
				FolderNodeData.NumSelectedFiles++;
				FolderNodeData.NumEmptySelectedFiles++;
				FolderNodeData.NumDefaultSelectedFiles++;
			}

			if(FolderNodeData.NumSelectedFiles > 0 && !FolderNodeData.Folder.bEmptyAfterClean && Depth < 2)
			{
				FolderNode.Expand();
			}
			else
			{
				FolderNode.Collapse();
			}

			UpdateImage(FolderNode);
			return FolderNode;
		}

		private bool IsSafeToDeleteFolder(string FolderPath)
		{
			return SafeToDeleteFolders.Any(x => FolderPath.EndsWith(x)) || ExtraSafeToDeleteFolders.Any(x => FolderPath.EndsWith(x));
		}

		private bool IsSafeToDeleteFile(string FolderPath, string Name)
		{
			return SafeToDeleteExtensions.Any(x => Name.EndsWith(x)) || ExtraSafeToDeleteExtensions.Any(x => Name.EndsWith(x));
		}

		private void TreeView_DrawNode(object sender, DrawTreeNodeEventArgs e)
		{
			e.Graphics.DrawLine(Pens.Black, new Point(e.Bounds.Left, e.Bounds.Top), new Point(e.Bounds.Right, e.Bounds.Bottom));
		}

		private void TreeView_NodeMouseClick(object sender, TreeNodeMouseClickEventArgs e)
		{
			TreeNode Node = e.Node;
			if(e.Button == System.Windows.Forms.MouseButtons.Right)
			{
				TreeView.SelectedNode = e.Node;
				FolderContextMenu.Tag = e.Node;
				FolderContextMenu.Show(TreeView.PointToScreen(e.Location));
			}
			else if(e.X >= e.Node.Bounds.Left - 32 && e.X < e.Node.Bounds.Left - 16)
			{
				TreeNodeData NodeData = (TreeNodeData)Node.Tag;
				SetSelected(Node, (NodeData.NumSelectedFiles == 0)? SelectionType.All : SelectionType.None);
			}
		}

		private void SetSelected(TreeNode ParentNode, SelectionType Type)
		{
			TreeNodeData ParentNodeData = (TreeNodeData)ParentNode.Tag;

			int PrevNumSelectedFiles = ParentNodeData.NumSelectedFiles;
			SetSelectedOnChildren(ParentNode, Type);

			int DeltaNumSelectedFiles = ParentNodeData.NumSelectedFiles - PrevNumSelectedFiles;
			if(DeltaNumSelectedFiles != 0)
			{
				for(TreeNode NextParentNode = ParentNode.Parent; NextParentNode != null; NextParentNode = NextParentNode.Parent)
				{
					TreeNodeData NextParentNodeData = (TreeNodeData)NextParentNode.Tag;
					NextParentNodeData.NumSelectedFiles += DeltaNumSelectedFiles;
					UpdateImage(NextParentNode);
				}
			}
		}

		private void SetSelectedOnChildren(TreeNode ParentNode, SelectionType Type)
		{
			TreeNodeData ParentNodeData = (TreeNodeData)ParentNode.Tag;

			int NewNumSelectedFiles = 0;
			switch(Type)
			{
				case SelectionType.All:
					NewNumSelectedFiles = ParentNodeData.NumFiles;
					break;
				case SelectionType.Empty:
					NewNumSelectedFiles = ParentNodeData.NumEmptySelectedFiles;
					break;
				case SelectionType.Missing:
					NewNumSelectedFiles = ParentNodeData.NumMissingSelectedFiles;
					break;
				case SelectionType.SafeToDelete:
					NewNumSelectedFiles = ParentNodeData.NumDefaultSelectedFiles;
					break;
				case SelectionType.None:
					NewNumSelectedFiles = 0;
					break;
			}

			if(NewNumSelectedFiles != ParentNodeData.NumSelectedFiles)
			{
				foreach(TreeNode? ChildNode in ParentNode.Nodes)
				{
					if (ChildNode != null)
					{
						SetSelectedOnChildren(ChildNode, Type);
					}
				}
				ParentNodeData.NumSelectedFiles = NewNumSelectedFiles;
				UpdateImage(ParentNode);
			}
		}

		private void UpdateImage(TreeNode Node)
		{
			TreeNodeData NodeData = (TreeNodeData)Node.Tag;
			int ImageIndex = (NodeData.Folder != null)? 0 : 3;
			ImageIndex += (NodeData.NumSelectedFiles == 0)? 0 : (NodeData.NumSelectedFiles < NodeData.NumFiles || (NodeData.Folder != null && !NodeData.Folder.bEmptyAfterClean))? 1 : 2;
			Node.ImageIndex = ImageIndex;
			Node.SelectedImageIndex = ImageIndex;
		}

		private void CleanBtn_Click(object sender, EventArgs e)
		{
			List<FileInfo> FilesToSync = new List<FileInfo>();
			List<FileInfo> FilesToDelete = new List<FileInfo>();
			List<DirectoryInfo> DirectoriesToDelete = new List<DirectoryInfo>();
			foreach(TreeNode? RootNode in TreeView.Nodes)
			{
				if (RootNode != null)
				{
					FindSelection(RootNode, FilesToSync, FilesToDelete, DirectoriesToDelete);
				}
			}

			ModalTask? Result = ModalTask.Execute(this, "Clean Workspace", "Cleaning files, please wait...", x => DeleteFilesTask.RunAsync(PerforceSettings, FilesToSync, FilesToDelete, DirectoriesToDelete, Logger, x), ModalTaskFlags.Quiet);
			if(Result != null && Result.Failed)
			{
				FailedToDeleteWindow FailedToDelete = new FailedToDeleteWindow();
				FailedToDelete.FileList.Text = Result.Error;
				FailedToDelete.FileList.SelectionStart = 0;
				FailedToDelete.FileList.SelectionLength = 0;
				FailedToDelete.ShowDialog();
			}
		}

		private void FindSelection(TreeNode Node, List<FileInfo> FilesToSync, List<FileInfo> FilesToDelete, List<DirectoryInfo> DirectoriesToDelete)
		{
			TreeNodeData NodeData = (TreeNodeData)Node.Tag;
			if(NodeData.File != null)
			{
				if(NodeData.NumSelectedFiles > 0)
				{
					if(NodeData.Action == TreeNodeAction.Delete)
					{
						FilesToDelete.Add(NodeData.File);
					}
					else
					{
						FilesToSync.Add(NodeData.File);
					}
				}
			}
			else
			{
				foreach(TreeNode? ChildNode in Node.Nodes)
				{
					if (ChildNode != null)
					{
						FindSelection(ChildNode, FilesToSync, FilesToDelete, DirectoriesToDelete);
					}
				}
				if(NodeData.Folder != null && NodeData.Folder.bEmptyAfterClean && NodeData.NumSelectedFiles == NodeData.NumFiles)
				{
					DirectoriesToDelete.Add(NodeData.Folder.Directory);
				}
			}
		}

		private void SelectAllBtn_Click(object sender, EventArgs e)
		{
			foreach(TreeNode? Node in TreeView.Nodes)
			{
				if (Node != null)
				{
					SetSelected(Node, SelectionType.All);
				}
			}
		}

		private void SelectMissingBtn_Click(object sender, EventArgs e)
		{
			foreach(TreeNode? Node in TreeView.Nodes)
			{
				if (Node != null)
				{
					SetSelected(Node, SelectionType.Missing);
				}
			}
		}

		private void FolderContextMenu_SelectAll_Click(object sender, EventArgs e)
		{
			TreeNode Node = (TreeNode)FolderContextMenu.Tag;
			SetSelected(Node, SelectionType.All);
		}

		private void FolderContextMenu_SelectSafeToDelete_Click(object sender, EventArgs e)
		{
			TreeNode Node = (TreeNode)FolderContextMenu.Tag;
			SetSelected(Node, SelectionType.SafeToDelete);
		}

		private void FolderContextMenu_SelectEmptyFolder_Click(object sender, EventArgs e)
		{
			TreeNode Node = (TreeNode)FolderContextMenu.Tag;
			SetSelected(Node, SelectionType.Empty);
		}

		private void FolderContextMenu_SelectNone_Click(object sender, EventArgs e)
		{
			TreeNode Node = (TreeNode)FolderContextMenu.Tag;
			SetSelected(Node, SelectionType.None);
		}

		private void FolderContextMenu_OpenWithExplorer_Click(object sender, EventArgs e)
		{
			TreeNode Node = (TreeNode)FolderContextMenu.Tag;
			TreeNodeData NodeData = (TreeNodeData)Node.Tag;

			if (NodeData.Folder != null)
			{
				Process.Start("explorer.exe", String.Format("\"{0}\"", NodeData.Folder.Directory.FullName));
			}
			else if (NodeData.File != null)
			{
				Process.Start("explorer.exe", String.Format("\"{0}\"", NodeData.File.Directory!.FullName));
			}
		}
	}
}
