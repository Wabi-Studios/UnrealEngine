// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "OutputLogPrivatePCH.h"
#include "SOutputLog.h"
#include "SScrollBorder.h"
#include "BaseTextLayoutMarshaller.h"
#include "GameFramework/GameMode.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameState.h"
/** Custom console editable text box whose only purpose is to prevent some keys from being typed */
class SConsoleEditableTextBox : public SEditableTextBox
{
public:
	SLATE_BEGIN_ARGS( SConsoleEditableTextBox ) {}
		
		/** Hint text that appears when there is no text in the text box */
		SLATE_ATTRIBUTE(FText, HintText)

		/** Called whenever the text is changed interactively by the user */
		SLATE_EVENT(FOnTextChanged, OnTextChanged)

		/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
		SLATE_EVENT(FOnTextCommitted, OnTextCommitted)

	SLATE_END_ARGS()


	void Construct( const FArguments& InArgs )
	{
		SetStyle(&FCoreStyle::Get().GetWidgetStyle< FEditableTextBoxStyle >("NormalEditableTextBox"));

		SBorder::Construct(SBorder::FArguments()
			.BorderImage(this, &SConsoleEditableTextBox::GetConsoleBorder)
			.BorderBackgroundColor(Style->BackgroundColor)
			.ForegroundColor(Style->ForegroundColor)
			.Padding(Style->Padding)
			[
				SAssignNew( EditableText, SConsoleEditableText )
				.HintText( InArgs._HintText )
				.OnTextChanged( InArgs._OnTextChanged )
				.OnTextCommitted( InArgs._OnTextCommitted )
			] );
	}

private:
	class SConsoleEditableText : public SEditableText
	{
	public:
		SLATE_BEGIN_ARGS( SConsoleEditableText ) {}
			/** The text that appears when there is nothing typed into the search box */
			SLATE_ATTRIBUTE(FText, HintText)
			/** Called whenever the text is changed interactively by the user */
			SLATE_EVENT(FOnTextChanged, OnTextChanged)

			/** Called whenever the text is committed.  This happens when the user presses enter or the text box loses focus. */
			SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
		SLATE_END_ARGS()

		void Construct( const FArguments& InArgs )
		{
			SEditableText::Construct
			( 
				SEditableText::FArguments()
				.HintText( InArgs._HintText )
				.OnTextChanged( InArgs._OnTextChanged )
				.OnTextCommitted( InArgs._OnTextCommitted ) 
				.ClearKeyboardFocusOnCommit( false )
				.IsCaretMovedWhenGainFocus( false ) 
				.MinDesiredWidth( 400.0f )
			);
		}

		virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
		{
			// Special case handling.  Intercept the tilde key.  It is not suitable for typing in the console
			if( InKeyEvent.GetKey() == EKeys::Tilde )
			{
				return FReply::Unhandled();
			}
			else
			{
				return SEditableText::OnKeyDown( MyGeometry, InKeyEvent );
			}
		}

		virtual FReply OnKeyChar( const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent )
		{
			// Special case handling.  Intercept the tilde key.  It is not suitable for typing in the console
			if( InCharacterEvent.GetCharacter() != 0x60 )
			{
				return SEditableText::OnKeyChar( MyGeometry, InCharacterEvent );
			}
			else
			{
				return FReply::Unhandled();
			}
		}

	};

	/** @return Border image for the text box based on the hovered and focused state */
	const FSlateBrush* GetConsoleBorder() const
	{
		if (EditableText->HasKeyboardFocus())
		{
			return &Style->BackgroundImageFocused;
		}
		else
		{
			if (EditableText->IsHovered())
			{
				return &Style->BackgroundImageHovered;
			}
			else
			{
				return &Style->BackgroundImageNormal;
			}
		}
	}

};

SConsoleInputBox::SConsoleInputBox()
	: SelectedSuggestion(-1)
	, bIgnoreUIUpdate(false)
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SConsoleInputBox::Construct( const FArguments& InArgs )
{
	OnConsoleCommandExecuted = InArgs._OnConsoleCommandExecuted;

	ChildSlot
	[
		SAssignNew( SuggestionBox, SMenuAnchor )
			.Placement( InArgs._SuggestionListPlacement )
			[
				SAssignNew(InputText, SConsoleEditableTextBox)
					.OnTextCommitted(this, &SConsoleInputBox::OnTextCommitted)
					.HintText( NSLOCTEXT( "ConsoleInputBox", "TypeInConsoleHint", "Enter console command" ) )
					.OnTextChanged(this, &SConsoleInputBox::OnTextChanged)
			]
			.MenuContent
			(
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				.Padding( FMargin(2) )
				[
					SNew(SBox)
					.HeightOverride(250) // avoids flickering, ideally this would be adaptive to the content without flickering
					[
						SAssignNew(SuggestionListView, SListView< TSharedPtr<FString> >)
							.ListItemsSource(&Suggestions)
							.SelectionMode( ESelectionMode::Single )							// Ideally the mouse over would not highlight while keyboard controls the UI
							.OnGenerateRow(this, &SConsoleInputBox::MakeSuggestionListItemWidget)
							.OnSelectionChanged(this, &SConsoleInputBox::SuggestionSelectionChanged)
							.ItemHeight(18)
					]
				]
			)
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SConsoleInputBox::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!GIntraFrameDebuggingGameThread && !IsEnabled())
	{
		SetEnabled(true);
	}
	else if (GIntraFrameDebuggingGameThread && IsEnabled())
	{
		SetEnabled(false);
	}
}


void SConsoleInputBox::SuggestionSelectionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if(bIgnoreUIUpdate)
	{
		return;
	}

	for(int32 i = 0; i < Suggestions.Num(); ++i)
	{
		if(NewValue == Suggestions[i])
		{
			SelectedSuggestion = i;
			MarkActiveSuggestion();

			// If the user selected this suggestion by clicking on it, then go ahead and close the suggestion
			// box as they've chosen the suggestion they're interested in.
			if( SelectInfo == ESelectInfo::OnMouseClick )
			{
				SuggestionBox->SetIsOpen( false );
			}

			// Ideally this would set the focus back to the edit control
//			FWidgetPath WidgetToFocusPath;
//			FSlateApplication::Get().GeneratePathToWidgetUnchecked( InputText.ToSharedRef(), WidgetToFocusPath );
//			FSlateApplication::Get().SetKeyboardFocus( WidgetToFocusPath, EFocusCause::SetDirectly );
			break;
		}
	}
}


TSharedRef<ITableRow> SConsoleInputBox::MakeSuggestionListItemWidget(TSharedPtr<FString> Text, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(Text.IsValid());

	FString Left, Right, Combined;

	if(Text->Split(TEXT("\t"), &Left, &Right))
	{
		Combined = Left + Right;
	}
	else
	{
		Combined = *Text;
	}

	FText HighlightText = FText::FromString(Left);

	return
		SNew(STableRow< TSharedPtr<FString> >, OwnerTable)
		[
			SNew(SBox)
			.WidthOverride(300)			// to enforce some minimum width, ideally we define the minimum, not a fixed width
			[
				SNew(STextBlock)
				.Text(Combined)
				.TextStyle( FEditorStyle::Get(), "Log.Normal")
				.HighlightText(HighlightText)
			]
		];
}

class FConsoleVariableAutoCompleteVisitor 
{
public:
	// @param Name must not be 0
	// @param CVar must not be 0
	static void OnConsoleVariable(const TCHAR *Name, IConsoleObject* CVar,TArray<FString>& Sink)
	{
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if(CVar->TestFlags(ECVF_Cheat))
		{
			return;
		}
#endif // (UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if(CVar->TestFlags(ECVF_Unregistered))
		{
			return;
		}

		Sink.Add(Name);
	}
};

void SConsoleInputBox::OnTextChanged(const FText& InText)
{
	if(bIgnoreUIUpdate)
	{
		return;
	}

	const FString& InputTextStr = InputText->GetText().ToString();
	if(!InputTextStr.IsEmpty())
	{
		TArray<FString> AutoCompleteList;

		// console variables
		{
			IConsoleManager::Get().ForEachConsoleObject(
				FConsoleObjectVisitor::CreateStatic< TArray<FString>& >(
				&FConsoleVariableAutoCompleteVisitor::OnConsoleVariable,
				AutoCompleteList ), *InputTextStr);
		}

		AutoCompleteList.Sort();

		for(uint32 i = 0; i < (uint32)AutoCompleteList.Num(); ++i)
		{
			FString &ref = AutoCompleteList[i];

			ref = ref.Left(InputTextStr.Len()) + TEXT("\t") + ref.RightChop(InputTextStr.Len());
		}

		SetSuggestions(AutoCompleteList, false);
	}
	else
	{
		ClearSuggestions();
	}
}

void SConsoleInputBox::OnTextCommitted( const FText& InText, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		if (!InText.IsEmpty())
		{
			IConsoleManager::Get().AddConsoleHistoryEntry( *InText.ToString() );

			// Copy the exec text string out so we can clear the widget's contents.  If the exec command spawns
			// a new window it can cause the text box to lose focus, which will result in this function being
			// re-entered.  We want to make sure the text string is empty on re-entry, so we'll clear it out
			const FString ExecString = InText.ToString();

			// Clear the console input area
			bIgnoreUIUpdate = true;
			InputText->SetText(FText::GetEmpty());
			bIgnoreUIUpdate = false;
			
			// Exec!
			{
				bool bWasHandled = false;
				UWorld* World = NULL;
				UWorld* OldWorld = NULL;

				// The play world needs to handle these commands if it exists
				if( GIsEditor && GEditor->PlayWorld && !GIsPlayInEditorWorld )
				{
					World = GEditor->PlayWorld;
					OldWorld = SetPlayInEditorWorld( GEditor->PlayWorld );
				}
				
				ULocalPlayer* Player = GEngine->GetDebugLocalPlayer();
				if( Player )
				{
					UWorld* PlayerWorld = Player->GetWorld();
					if( !World )
					{
						World = PlayerWorld;
					}
					bWasHandled = Player->Exec( PlayerWorld, *ExecString, *GLog );
				}
					
				if( !World )
				{
					World = GEditor->GetEditorWorldContext().World();
				}
				if( World )
				{
					if( !bWasHandled )
					{					
						AGameMode* const GameMode = World->GetAuthGameMode();
						if( GameMode && GameMode->ProcessConsoleExec( *ExecString, *GLog, NULL ) )
						{
							bWasHandled = true;
						}
						else if (World->GameState && World->GameState->ProcessConsoleExec(*ExecString, *GLog, NULL))
						{
							bWasHandled = true;
						}
					}

					if( !bWasHandled && !Player)
					{
						if( GIsEditor )
						{
							bWasHandled = GEditor->Exec( World, *ExecString, *GLog );
						}
						else
						{
							bWasHandled = GEngine->Exec( World, *ExecString, *GLog );
						}	
					}
				}
				// Restore the old world of there was one
				if( OldWorld )
				{
					RestoreEditorWorld( OldWorld );
				}
			}
		}

		ClearSuggestions();

		OnConsoleCommandExecuted.ExecuteIfBound();
	}
}

FReply SConsoleInputBox::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& KeyEvent )
{
	if(SuggestionBox->IsOpen())
	{
		if(KeyEvent.GetKey() == EKeys::Up || KeyEvent.GetKey() == EKeys::Down)
		{
			if(KeyEvent.GetKey() == EKeys::Up)
			{
				if(SelectedSuggestion < 0)
				{
					// from edit control to end of list
					SelectedSuggestion = Suggestions.Num() - 1;
				}
				else
				{
					// got one up, possibly back to edit control
					--SelectedSuggestion;
				}
			}

			if(KeyEvent.GetKey() == EKeys::Down)
			{
				if(SelectedSuggestion < Suggestions.Num() - 1)
				{
					// go one down, possibly from edit control to top
					++SelectedSuggestion;
				}
				else
				{
					// back to edit control
					SelectedSuggestion = -1;
				}
			}

			MarkActiveSuggestion();

			return FReply::Handled();
		}
		else if (KeyEvent.GetKey() == EKeys::Tab)
		{
			if (Suggestions.Num())
			{
				if (SelectedSuggestion >= 0 && SelectedSuggestion < Suggestions.Num())
				{
					MarkActiveSuggestion();
					OnTextCommitted(InputText->GetText(), ETextCommit::OnEnter);
				}
				else
				{
					SelectedSuggestion = 0;
					MarkActiveSuggestion();
				}
			}

			return FReply::Handled();
		}
	}
	else
	{
		if(KeyEvent.GetKey() == EKeys::Up)
		{
			TArray<FString> History;

			IConsoleManager::Get().GetConsoleHistory(History);

			SetSuggestions(History, true);
			
			if(Suggestions.Num())
			{
				SelectedSuggestion = Suggestions.Num() - 1;
				MarkActiveSuggestion();
			}

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SConsoleInputBox::SetSuggestions(TArray<FString>& Elements, bool bInHistoryMode)
{
	FString SelectionText;
	if (SelectedSuggestion >= 0 && SelectedSuggestion < Suggestions.Num())
	{
		SelectionText = *Suggestions[SelectedSuggestion];
	}

	SelectedSuggestion = -1;
	Suggestions.Empty();
	SelectedSuggestion = -1;

	for(uint32 i = 0; i < (uint32)Elements.Num(); ++i)
	{
		Suggestions.Add(MakeShareable(new FString(Elements[i])));

		if (Elements[i] == SelectionText)
		{
			SelectedSuggestion = i;
		}
	}

	if(Suggestions.Num())
	{
		// Ideally if the selection box is open the output window is not changing it's window title (flickers)
		SuggestionBox->SetIsOpen(true, false);
		SuggestionListView->RequestScrollIntoView(Suggestions.Last());
	}
	else
	{
		SuggestionBox->SetIsOpen(false);
	}
}

void SConsoleInputBox::OnFocusLost( const FFocusEvent& InFocusEvent )
{
//	SuggestionBox->SetIsOpen(false);
}

void SConsoleInputBox::MarkActiveSuggestion()
{
	bIgnoreUIUpdate = true;
	if(SelectedSuggestion >= 0)
	{
		SuggestionListView->SetSelection(Suggestions[SelectedSuggestion]);
		SuggestionListView->RequestScrollIntoView(Suggestions[SelectedSuggestion]);	// Ideally this would only scroll if outside of the view

		InputText->SetText(FText::FromString(GetSelectionText()));
	}
	else
	{
		SuggestionListView->ClearSelection();
	}
	bIgnoreUIUpdate = false;
}

void SConsoleInputBox::ClearSuggestions()
{
	SelectedSuggestion = -1;
	SuggestionBox->SetIsOpen(false);
	Suggestions.Empty();
}

FString SConsoleInputBox::GetSelectionText() const
{
	FString ret = *Suggestions[SelectedSuggestion];
	
	ret = ret.Replace(TEXT("\t"), TEXT(""));

	return ret;
}

/** Output log text marshaller to convert an array of FLogMessages into styled lines to be consumed by an FTextLayout */
class FOutputLogTextLayoutMarshaller : public FBaseTextLayoutMarshaller
{
public:

	static TSharedRef< FOutputLogTextLayoutMarshaller > Create(TArray< TSharedPtr<FLogMessage> > InMessages);

	virtual ~FOutputLogTextLayoutMarshaller();
	
	// ITextLayoutMarshaller
	virtual void SetText(const FString& SourceString, FTextLayout& TargetTextLayout) override;
	virtual void GetText(FString& TargetString, const FTextLayout& SourceTextLayout) override;

	bool AppendMessage(const TCHAR* InText, const ELogVerbosity::Type InVerbosity, const FName& InCategory);
	void ClearMessages();
	int32 GetNumMessages() const;

protected:

	FOutputLogTextLayoutMarshaller(TArray< TSharedPtr<FLogMessage> > InMessages);

	void AppendMessageToTextLayout(const TSharedPtr<FLogMessage>& Message);

	/** All log messages to show in the text box */
	TArray< TSharedPtr<FLogMessage> > Messages;

	FTextLayout* TextLayout;
};

TSharedRef< FOutputLogTextLayoutMarshaller > FOutputLogTextLayoutMarshaller::Create(TArray< TSharedPtr<FLogMessage> > InMessages)
{
	return MakeShareable(new FOutputLogTextLayoutMarshaller(MoveTemp(InMessages)));
}

FOutputLogTextLayoutMarshaller::~FOutputLogTextLayoutMarshaller()
{
}

void FOutputLogTextLayoutMarshaller::SetText(const FString& SourceString, FTextLayout& TargetTextLayout)
{
	TextLayout = &TargetTextLayout;

	for(const auto& Message : Messages)
	{
		AppendMessageToTextLayout(Message);
	}
}

void FOutputLogTextLayoutMarshaller::GetText(FString& TargetString, const FTextLayout& SourceTextLayout)
{
	SourceTextLayout.GetAsText(TargetString);
}

bool FOutputLogTextLayoutMarshaller::AppendMessage(const TCHAR* InText, const ELogVerbosity::Type InVerbosity, const FName& InCategory)
{
	TArray< TSharedPtr<FLogMessage> > NewMessages;
	if(SOutputLog::CreateLogMessages(InText, InVerbosity, InCategory, NewMessages))
	{
		const bool bWasEmpty = Messages.Num() == 0;
		Messages.Append(NewMessages);

		if(TextLayout)
		{
			// If we were previously empty, then we'd have inserted a dummy empty line into the document
			// We need to remove this line now as it would cause the message indices to get out-of-sync with the line numbers, which would break auto-scrolling
			if(bWasEmpty)
			{
				TextLayout->ClearLines();
			}

			// If we've already been given a text layout, then append these new messages rather than force a refresh of the entire document
			for(const auto& Message : NewMessages)
			{
				AppendMessageToTextLayout(Message);
			}
		}
		else
		{
			MakeDirty();
		}

		return true;
	}

	return false;
}

void FOutputLogTextLayoutMarshaller::AppendMessageToTextLayout(const TSharedPtr<FLogMessage>& Message)
{
	const FTextBlockStyle& MessageTextStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>(Message->Style);

	TSharedRef<FString> LineText = Message->Message;

	TArray<TSharedRef<IRun>> Runs;
	Runs.Add(FSlateTextRun::Create(FRunInfo(), LineText, MessageTextStyle));

	TextLayout->AddLine(LineText, Runs);
}

void FOutputLogTextLayoutMarshaller::ClearMessages()
{
	Messages.Empty();
	MakeDirty();
}

int32 FOutputLogTextLayoutMarshaller::GetNumMessages() const
{
	return Messages.Num();
}

FOutputLogTextLayoutMarshaller::FOutputLogTextLayoutMarshaller(TArray< TSharedPtr<FLogMessage> > InMessages)
	: Messages(MoveTemp(InMessages))
	, TextLayout(nullptr)
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SOutputLog::Construct( const FArguments& InArgs )
{
	MessagesTextMarshaller = FOutputLogTextLayoutMarshaller::Create(MoveTemp(InArgs._Messages));

	MessagesTextBox = SNew(SMultiLineEditableTextBox)
		.Style(FEditorStyle::Get(), "Log.TextBox")
		.TextStyle(FEditorStyle::Get(), "Log.Normal")
		.Marshaller(MessagesTextMarshaller)
		.IsReadOnly(true)
		.AlwaysShowScrollbars(true)
		.OnVScrollBarUserScrolled(this, &SOutputLog::OnUserScrolled)
		.ContextMenuExtender(this, &SOutputLog::ExtendTextBoxMenu);

	ChildSlot
	[
		SNew(SVerticalBox)

			// Output log area
			+SVerticalBox::Slot()
			.FillHeight(1)
			[
				MessagesTextBox.ToSharedRef()
			]
			// The console input box
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
			[
				SNew( SConsoleInputBox )
				.OnConsoleCommandExecuted(this, &SOutputLog::OnConsoleCommandExecuted)

				// Always place suggestions above the input line for the output log widget
				.SuggestionListPlacement( MenuPlacement_AboveAnchor )
			]
	];
	
	GLog->AddOutputDevice(this);

	bIsUserScrolled = false;
	RequestForceScroll();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SOutputLog::~SOutputLog()
{
	if (GLog != NULL)
	{
		GLog->RemoveOutputDevice(this);
	}
}

bool SOutputLog::CreateLogMessages( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category, TArray< TSharedPtr<FLogMessage> >& OutMessages )
{
	if (Verbosity == ELogVerbosity::SetColor)
	{
		// Skip Color Events
		return false;
	}
	else
	{
		FName Style;
		if (Category == NAME_Cmd)
		{
			Style = FName(TEXT("Log.Command"));
		}
		else if (Verbosity == ELogVerbosity::Error)
		{
			Style = FName(TEXT("Log.Error"));
		}
		else if (Verbosity == ELogVerbosity::Warning)
		{
			Style = FName(TEXT("Log.Warning"));
		}
		else
		{
			Style = FName(TEXT("Log.Normal"));
		}

		// Determine how to format timestamps
		static ELogTimes::Type LogTimestampMode = ELogTimes::None;
		if (UObjectInitialized() && !GExitPurge)
		{
			// Logging can happen very late during shutdown, even after the UObject system has been torn down, hence the init check above
			LogTimestampMode = GetDefault<UEditorStyleSettings>()->LogTimestampMode;
		}

		const int32 OldNumMessages = OutMessages.Num();

		// handle multiline strings by breaking them apart by line
		TArray<FTextRange> LineRanges;
		FString CurrentLogDump = V;
		FTextRange::CalculateLineRangesFromString(CurrentLogDump, LineRanges);

		bool bIsFirstLineInMessage = true;
		for (const FTextRange& LineRange : LineRanges)
		{
			if (!LineRange.IsEmpty())
			{
				FString Line = CurrentLogDump.Mid(LineRange.BeginIndex, LineRange.Len());
				Line = Line.ConvertTabsToSpaces(4);

				OutMessages.Add(MakeShareable(new FLogMessage(MakeShareable(new FString((bIsFirstLineInMessage) ? FOutputDevice::FormatLogLine(Verbosity, Category, *Line, LogTimestampMode) : Line)), Style)));

				bIsFirstLineInMessage = false;
			}
		}

		return OldNumMessages != OutMessages.Num();
	}
}

void SOutputLog::Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category )
{
	if ( MessagesTextMarshaller->AppendMessage(V, Verbosity, Category) )
	{
		// Don't scroll to the bottom automatically when the user is scrolling the view or has scrolled it away from the bottom.
		if( !bIsUserScrolled )
		{
			MessagesTextBox->ScrollTo(FTextLocation(MessagesTextMarshaller->GetNumMessages() - 1));
		}
	}
}

void SOutputLog::ExtendTextBoxMenu(FMenuBuilder& Builder)
{
	FUIAction ClearOutputLogAction(
		FExecuteAction::CreateRaw( this, &SOutputLog::OnClearLog ),
		FCanExecuteAction::CreateSP( this, &SOutputLog::CanClearLog )
		);

	Builder.AddMenuEntry(
		NSLOCTEXT("OutputLog", "ClearLogLabel", "Clear Log"), 
		NSLOCTEXT("OutputLog", "ClearLogTooltip", "Clears all log messages"), 
		FSlateIcon(), 
		ClearOutputLogAction
		);
}

void SOutputLog::OnClearLog()
{
	// Make sure the cursor is back at the start of the log before we clear it
	MessagesTextBox->GoTo(FTextLocation(0));

	MessagesTextMarshaller->ClearMessages();
	MessagesTextBox->Refresh();
	bIsUserScrolled = false;
}

void SOutputLog::OnUserScrolled(float ScrollOffset)
{
	bIsUserScrolled = !FMath::IsNearlyEqual(ScrollOffset, 1.0f);
}

bool SOutputLog::CanClearLog() const
{
	return MessagesTextMarshaller->GetNumMessages() > 0;
}

void SOutputLog::OnConsoleCommandExecuted()
{
	RequestForceScroll();
}

void SOutputLog::RequestForceScroll()
{
	if(MessagesTextMarshaller->GetNumMessages() > 0)
	{
		MessagesTextBox->ScrollTo(FTextLocation(MessagesTextMarshaller->GetNumMessages() - 1));
		bIsUserScrolled = false;
	}
}
