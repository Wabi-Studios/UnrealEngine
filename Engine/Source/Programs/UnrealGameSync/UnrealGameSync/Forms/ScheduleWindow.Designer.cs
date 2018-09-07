// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{
	partial class ScheduleWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			this.TimePicker = new System.Windows.Forms.DateTimePicker();
			this.EnableCheckBox = new System.Windows.Forms.CheckBox();
			this.label1 = new System.Windows.Forms.Label();
			this.OkBtn = new System.Windows.Forms.Button();
			this.CancelBtn = new System.Windows.Forms.Button();
			this.ChangeComboBox = new System.Windows.Forms.ComboBox();
			this.ProjectListBox = new System.Windows.Forms.CheckedListBox();
			this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
			this.flowLayoutPanel1 = new System.Windows.Forms.FlowLayoutPanel();
			this.flowLayoutPanel2 = new System.Windows.Forms.FlowLayoutPanel();
			this.tableLayoutPanel1.SuspendLayout();
			this.flowLayoutPanel1.SuspendLayout();
			this.flowLayoutPanel2.SuspendLayout();
			this.SuspendLayout();
			// 
			// TimePicker
			// 
			this.TimePicker.Anchor = System.Windows.Forms.AnchorStyles.Left;
			this.TimePicker.CustomFormat = "h:mm tt";
			this.TimePicker.Format = System.Windows.Forms.DateTimePickerFormat.Custom;
			this.TimePicker.Location = new System.Drawing.Point(439, 3);
			this.TimePicker.Name = "TimePicker";
			this.TimePicker.ShowUpDown = true;
			this.TimePicker.Size = new System.Drawing.Size(118, 23);
			this.TimePicker.TabIndex = 3;
			// 
			// EnableCheckBox
			// 
			this.EnableCheckBox.Anchor = System.Windows.Forms.AnchorStyles.Left;
			this.EnableCheckBox.AutoSize = true;
			this.EnableCheckBox.Location = new System.Drawing.Point(0, 5);
			this.EnableCheckBox.Margin = new System.Windows.Forms.Padding(0, 3, 3, 3);
			this.EnableCheckBox.Name = "EnableCheckBox";
			this.EnableCheckBox.Size = new System.Drawing.Size(180, 19);
			this.EnableCheckBox.TabIndex = 1;
			this.EnableCheckBox.Text = "Automatically sync and build";
			this.EnableCheckBox.UseVisualStyleBackColor = true;
			this.EnableCheckBox.CheckedChanged += new System.EventHandler(this.EnableCheckBox_CheckedChanged);
			// 
			// label1
			// 
			this.label1.Anchor = System.Windows.Forms.AnchorStyles.Left;
			this.label1.AutoSize = true;
			this.label1.Location = new System.Drawing.Point(363, 7);
			this.label1.Name = "label1";
			this.label1.Size = new System.Drawing.Size(70, 15);
			this.label1.TabIndex = 3;
			this.label1.Text = "every day at";
			this.label1.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
			// 
			// OkBtn
			// 
			this.OkBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.OkBtn.DialogResult = System.Windows.Forms.DialogResult.OK;
			this.OkBtn.Location = new System.Drawing.Point(96, 3);
			this.OkBtn.Margin = new System.Windows.Forms.Padding(3, 3, 0, 3);
			this.OkBtn.Name = "OkBtn";
			this.OkBtn.Size = new System.Drawing.Size(87, 26);
			this.OkBtn.TabIndex = 5;
			this.OkBtn.Text = "Ok";
			this.OkBtn.UseVisualStyleBackColor = true;
			// 
			// CancelBtn
			// 
			this.CancelBtn.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
			this.CancelBtn.DialogResult = System.Windows.Forms.DialogResult.Cancel;
			this.CancelBtn.Location = new System.Drawing.Point(3, 3);
			this.CancelBtn.Name = "CancelBtn";
			this.CancelBtn.Size = new System.Drawing.Size(87, 26);
			this.CancelBtn.TabIndex = 4;
			this.CancelBtn.Text = "Cancel";
			this.CancelBtn.UseVisualStyleBackColor = true;
			// 
			// ChangeComboBox
			// 
			this.ChangeComboBox.Anchor = System.Windows.Forms.AnchorStyles.Left;
			this.ChangeComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
			this.ChangeComboBox.FormattingEnabled = true;
			this.ChangeComboBox.Items.AddRange(new object[] {
            "latest change",
            "latest good change",
            "latest starred change"});
			this.ChangeComboBox.Location = new System.Drawing.Point(186, 4);
			this.ChangeComboBox.Name = "ChangeComboBox";
			this.ChangeComboBox.Size = new System.Drawing.Size(171, 23);
			this.ChangeComboBox.TabIndex = 2;
			// 
			// ProjectListBox
			// 
			this.ProjectListBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
			this.ProjectListBox.CheckOnClick = true;
			this.ProjectListBox.FormattingEnabled = true;
			this.ProjectListBox.IntegralHeight = false;
			this.ProjectListBox.Location = new System.Drawing.Point(0, 35);
			this.ProjectListBox.Margin = new System.Windows.Forms.Padding(0, 6, 0, 6);
			this.ProjectListBox.Name = "ProjectListBox";
			this.ProjectListBox.Size = new System.Drawing.Size(680, 179);
			this.ProjectListBox.TabIndex = 8;
			// 
			// tableLayoutPanel1
			// 
			this.tableLayoutPanel1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
			this.tableLayoutPanel1.ColumnCount = 1;
			this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
			this.tableLayoutPanel1.Controls.Add(this.flowLayoutPanel1, 0, 0);
			this.tableLayoutPanel1.Controls.Add(this.ProjectListBox, 0, 1);
			this.tableLayoutPanel1.Controls.Add(this.flowLayoutPanel2, 0, 2);
			this.tableLayoutPanel1.Location = new System.Drawing.Point(12, 12);
			this.tableLayoutPanel1.Name = "tableLayoutPanel1";
			this.tableLayoutPanel1.RowCount = 3;
			this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
			this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
			this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
			this.tableLayoutPanel1.Size = new System.Drawing.Size(680, 252);
			this.tableLayoutPanel1.TabIndex = 9;
			// 
			// flowLayoutPanel1
			// 
			this.flowLayoutPanel1.AutoSize = true;
			this.flowLayoutPanel1.Controls.Add(this.EnableCheckBox);
			this.flowLayoutPanel1.Controls.Add(this.ChangeComboBox);
			this.flowLayoutPanel1.Controls.Add(this.label1);
			this.flowLayoutPanel1.Controls.Add(this.TimePicker);
			this.flowLayoutPanel1.Location = new System.Drawing.Point(0, 0);
			this.flowLayoutPanel1.Margin = new System.Windows.Forms.Padding(0);
			this.flowLayoutPanel1.Name = "flowLayoutPanel1";
			this.flowLayoutPanel1.Size = new System.Drawing.Size(560, 29);
			this.flowLayoutPanel1.TabIndex = 0;
			this.flowLayoutPanel1.WrapContents = false;
			// 
			// flowLayoutPanel2
			// 
			this.flowLayoutPanel2.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
			this.flowLayoutPanel2.AutoSize = true;
			this.flowLayoutPanel2.Controls.Add(this.CancelBtn);
			this.flowLayoutPanel2.Controls.Add(this.OkBtn);
			this.flowLayoutPanel2.Location = new System.Drawing.Point(497, 220);
			this.flowLayoutPanel2.Margin = new System.Windows.Forms.Padding(0);
			this.flowLayoutPanel2.Name = "flowLayoutPanel2";
			this.flowLayoutPanel2.Size = new System.Drawing.Size(183, 32);
			this.flowLayoutPanel2.TabIndex = 1;
			// 
			// ScheduleWindow
			// 
			this.AcceptButton = this.OkBtn;
			this.AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
			this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Dpi;
			this.CancelButton = this.CancelBtn;
			this.ClientSize = new System.Drawing.Size(704, 276);
			this.Controls.Add(this.tableLayoutPanel1);
			this.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
			this.Icon = global::UnrealGameSync.Properties.Resources.Icon;
			this.MinimumSize = new System.Drawing.Size(720, 315);
			this.Name = "ScheduleWindow";
			this.ShowInTaskbar = false;
			this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
			this.Text = "Schedule";
			this.tableLayoutPanel1.ResumeLayout(false);
			this.tableLayoutPanel1.PerformLayout();
			this.flowLayoutPanel1.ResumeLayout(false);
			this.flowLayoutPanel1.PerformLayout();
			this.flowLayoutPanel2.ResumeLayout(false);
			this.ResumeLayout(false);

		}

		#endregion

		private System.Windows.Forms.DateTimePicker TimePicker;
		private System.Windows.Forms.CheckBox EnableCheckBox;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.Button OkBtn;
		private System.Windows.Forms.Button CancelBtn;
		private System.Windows.Forms.ComboBox ChangeComboBox;
		private System.Windows.Forms.CheckedListBox ProjectListBox;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel1;
		private System.Windows.Forms.FlowLayoutPanel flowLayoutPanel2;
	}
}