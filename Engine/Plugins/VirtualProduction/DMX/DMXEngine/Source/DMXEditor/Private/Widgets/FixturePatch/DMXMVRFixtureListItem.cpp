// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXMVRFixtureListItem.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"

#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "DMXMVRFixtureListItem"

FDMXMVRFixtureListItem::FDMXMVRFixtureListItem(TWeakPtr<FDMXEditor> InDMXEditor, const FGuid& InMVRFixtureUUID)
	: MVRFixtureUUID(InMVRFixtureUUID)
	, WeakDMXEditor(InDMXEditor)
{
	const UDMXLibrary* const DMXLibrary = GetDMXLibrary();
	if (!DMXLibrary)
	{
		return;
	}

	const TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin();
	if (!DMXEditor)
	{
		return;
	}
	FixturePatchSharedData = DMXEditor->GetFixturePatchSharedData();

	const TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	UDMXEntityFixturePatch* const* FixturePatchPtr = FixturePatches.FindByPredicate([this](const UDMXEntityFixturePatch* FixturePatch)
		{
			return FixturePatch->GetMVRFixtureUUID() == MVRFixtureUUID;
		});

	if (ensureAlwaysMsgf(FixturePatchPtr, TEXT("Trying to create an MVR Fixture List Item, but there's no corresponding Fixture Patch for the MVR Fixture UUID.")))
	{
		WeakFixturePatch = *FixturePatchPtr;
	}
	
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();
	if (MVRFixturePtr)
	{
		CachedMVRFixture = *MVRFixturePtr;
	}
}

const FGuid& FDMXMVRFixtureListItem::GetMVRUUID() const
{
	return MVRFixtureUUID;
}

FLinearColor FDMXMVRFixtureListItem::GetBackgroundColor() const
{
	if (!ErrorStatusText.IsEmpty())
	{
		return FLinearColor::Red;
	}

	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->EditorColor;
	}

	return FLinearColor::Red;
}

FString FDMXMVRFixtureListItem::GetFixturePatchName() const
{
	if (const UDMXEntityFixturePatch* FixturePatch = GetFixturePatch())
	{
		return FixturePatch->Name;
	}

	return FString();
}

void FDMXMVRFixtureListItem::SetFixturePatchName(const FString& InDesiredName, FString& OutNewName)
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		if (FixturePatch->Name == InDesiredName)
		{
			return;
		}

		const FScopedTransaction SetFixturePatchNameTransaction(LOCTEXT("SetFixturePatchNameTransaction", "Set Fixture Patch Name"));
		FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDMXEntity, Name)));

		FixturePatch->SetName(InDesiredName);

		FixturePatch->PostEditChange();
	}
}

int32 FDMXMVRFixtureListItem::GetUnitNumber() const
{
	return CachedMVRFixture.UnitNumber;
}

void FDMXMVRFixtureListItem::SetUnitNumber(int32 InUnitNumber)
{
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();
	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	if (MVRFixturePtr && DMXLibrary)
	{
		const FScopedTransaction SetMVRFixtureUnitNumberTransaction(LOCTEXT("SetMVRFixtureUnitNumberTransaction", "Set MVR Fixture UnitNumber"));
		DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetGeneralSceneDescriptionPropertyName()));

		MVRFixturePtr->UnitNumber = InUnitNumber;

		DMXLibrary->PostEditChange();

		// Update Cache for Getter
		CachedMVRFixture = *MVRFixturePtr;
	}
}	

const FString& FDMXMVRFixtureListItem::GetMVRFixtureName() const
{
	return CachedMVRFixture.Name;
}

bool FDMXMVRFixtureListItem::SetMVRFixtureName(const FString& Name)
{
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();
	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	if (!Name.IsEmpty() && MVRFixturePtr && DMXLibrary)
	{
		const FScopedTransaction SetMVRFixtureNameTransaction(LOCTEXT("SetMVRFixtureNameTransaction", "Set MVR Fixture Name"));
		DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetGeneralSceneDescriptionPropertyName()));

		MVRFixturePtr->Name = Name;

		DMXLibrary->PostEditChange();

		// Update Cache for Getter
		CachedMVRFixture = *MVRFixturePtr;
	}

	return false;
}

UDMXEntityFixtureType* FDMXMVRFixtureListItem::GetFixtureType() const
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->GetFixtureType();
	}

	return nullptr;
}

void FDMXMVRFixtureListItem::SetFixtureType(UDMXEntityFixtureType* FixtureType)
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get();
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();

	if (DMXLibrary && FixturePatch && MVRFixturePtr)
	{
		if (FixturePatch->GetFixtureType() == FixtureType)
		{
			return;
		}

		const FScopedTransaction SetFixtureTypeTransaction(LOCTEXT("SetFixtureTypeTransaction", "Set Fixture Type of Patch"));
		FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetParentFixtureTypeTemplatePropertyNameChecked()));

		FixturePatch->SetFixtureType(FixtureType);

		FixturePatch->PostEditChange();
	}
}

int32 FDMXMVRFixtureListItem::GetModeIndex() const
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->GetActiveModeIndex();
	}

	return INDEX_NONE;
}

void FDMXMVRFixtureListItem::SetModeIndex(int32 ModeIndex)
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get();
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();

	if (DMXLibrary && FixturePatch && MVRFixturePtr)
	{
		if (ModeIndex == FixturePatch->GetActiveModeIndex())
		{
			return;
		}
		
		// If all should be changed, just change the patch
		const FScopedTransaction SetModeTransaction(LOCTEXT("SetModeTransaction", "Set Mode of Patch"));
		FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetParentFixtureTypeTemplatePropertyNameChecked()));

		FixturePatch->SetActiveModeIndex(ModeIndex);
			
		FixturePatch->PostEditChange();
	}
}

int32 FDMXMVRFixtureListItem::GetUniverse() const
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->GetUniverseID();
	}

	return -1;
}

int32 FDMXMVRFixtureListItem::GetAddress() const
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->GetStartingChannel();
	}

	return -1;
}

void FDMXMVRFixtureListItem::SetAddresses(int32 Universe, int32 Address)
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get();
	FDMXMVRFixture* MVRFixturePtr = FindMVRFixture();

	if (DMXLibrary && FixturePatch && MVRFixturePtr)
	{
		if (FixturePatch->GetUniverseID() == Universe &&
			FixturePatch->GetStartingChannel() == Address)
		{
			return;
		}

		// Only valid values
		const FDMXFixtureMode* ModePtr = FixturePatch->GetActiveMode();
		const int32 MaxAddress = ModePtr ? DMX_MAX_ADDRESS - ModePtr->ChannelSpan + 1 : DMX_MAX_ADDRESS;
		if (Universe < 0 || 
			Universe > DMX_MAX_UNIVERSE || 
			Address < 1 || 
			Address > MaxAddress)
		{
			return;
		}
		
		const FScopedTransaction SetAddressesTransaction(LOCTEXT("SetAddressesTransaction", "Set Addresses of Patch"));
		FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetParentFixtureTypeTemplatePropertyNameChecked()));

		FixturePatch->SetUniverseID(Universe);
		FixturePatch->SetStartingChannel(Address);

		FixturePatch->PostEditChange();


		// Select the universe in Fixture Patch Shared Data
		FixturePatchSharedData->SelectUniverse(Universe);
	}
}

int32 FDMXMVRFixtureListItem::GetNumChannels() const
{
	if (UDMXEntityFixturePatch* FixturePatch = WeakFixturePatch.Get())
	{
		return FixturePatch->GetChannelSpan();
	}

	return -1;
}

UDMXEntityFixturePatch* FDMXMVRFixtureListItem::GetFixturePatch() const
{
	return WeakFixturePatch.Get();
}

void FDMXMVRFixtureListItem::PasteItemsOntoItem(TWeakPtr<FDMXEditor> WeakDMXEditor, const TSharedPtr<FDMXMVRFixtureListItem>& PasteOntoItem, const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ItemsToPaste)
{
	TArray<UDMXEntityFixturePatch*> FixturePatchesToPaste;
	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ItemsToPaste)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Item->GetFixturePatch())
		{
			FixturePatchesToPaste.Add(FixturePatch);
		}
	}

	const FText PasteFixturePatchesTransactionText = FText::Format(LOCTEXT("DuplicateFixturePatchesTransaction", "Duplicate Fixture {0}|plural(one=Patch, other=Patches)"), ItemsToPaste.Num() > 1);
	DuplicateFixturePatchesInternal(WeakDMXEditor, FixturePatchesToPaste, PasteFixturePatchesTransactionText);
}

void FDMXMVRFixtureListItem::DuplicateItems(TWeakPtr<FDMXEditor> WeakDMXEditor, const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ItemsToDuplicate)
{
	TArray<UDMXEntityFixturePatch*> FixturePatchesToDuplicate;
	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ItemsToDuplicate)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Item->GetFixturePatch())
		{
			FixturePatchesToDuplicate.Add(FixturePatch);
		}
	}

	const FText DuplicateFixturePatchesTransactionText = FText::Format(LOCTEXT("DuplicateFixturePatchesTransaction", "Duplicate Fixture {0}|plural(one=Patch, other=Patches)"), ItemsToDuplicate.Num() > 1);
	DuplicateFixturePatchesInternal(WeakDMXEditor, FixturePatchesToDuplicate, DuplicateFixturePatchesTransactionText);
}

void FDMXMVRFixtureListItem::DeleteItems(const TArray<TSharedPtr<FDMXMVRFixtureListItem>>& ItemsToDelete)
{
	if (ItemsToDelete.Num() == 0)
	{
		return;
	}
	
	// Its safe to assume all patches are in the same Library - A Multi-Library Editor wouldn't make sense.
	UDMXLibrary* DMXLibrary = ItemsToDelete[0]->GetDMXLibrary();
	if (!DMXLibrary)
	{
		return;
	}
	
	const FText DeleteFixturePatchesTransactionText = FText::Format(LOCTEXT("DeleteFixturePatchesTransaction", "Delete Fixture {0}|plural(one=Patch, other=Patches)"), ItemsToDelete.Num() > 1);
	const FScopedTransaction DeleteFixturePatchTransaction(DeleteFixturePatchesTransactionText);
	DMXLibrary->PreEditChange(nullptr);

	for (const TSharedPtr<FDMXMVRFixtureListItem>& Item : ItemsToDelete)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Item->GetFixturePatch())
		{
			FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetMVRFixtureUUIDPropertyNameChecked()));
			UDMXEntityFixturePatch::RemoveFixturePatchFromLibrary(FixturePatch);
			FixturePatch->PostEditChange();
		}
	}

	DMXLibrary->PostEditChange();
}

UDMXLibrary* FDMXMVRFixtureListItem::GetDMXLibrary() const
{
	if (const TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin())
	{
		UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
		if (DMXLibrary)
		{
			return DMXLibrary;
		}
	}

	return nullptr;
}

void FDMXMVRFixtureListItem::DuplicateFixturePatchesInternal(TWeakPtr<FDMXEditor> WeakDMXEditor, const TArray<UDMXEntityFixturePatch*>& FixturePatchesToDuplicate, const FText& TransactionText)
{
	const TSharedPtr<FDMXEditor> DMXEditor = WeakDMXEditor.Pin();
	if (!DMXEditor.IsValid())
	{
		return;
	}
	const TSharedPtr<FDMXFixturePatchSharedData> FixturePatchSharedData = DMXEditor->GetFixturePatchSharedData();

	UDMXLibrary* DMXLibrary = DMXEditor->GetDMXLibrary();
	if (!DMXLibrary)
	{
		return;
	}

	// Find the first free Addresses
	TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	if (FixturePatches.Num() == 0)
	{
		return;
	}

	FixturePatches.Sort([](const UDMXEntityFixturePatch& FixturePatchA, const UDMXEntityFixturePatch& FixturePatchB)
		{
			const bool bUniverseIsSmaller = FixturePatchA.GetUniverseID() < FixturePatchB.GetUniverseID();
			const bool bUniverseIsEqual = FixturePatchA.GetUniverseID() == FixturePatchB.GetUniverseID();
			const bool bAddressIsSmallerOrEqual = FixturePatchA.GetStartingChannel() <= FixturePatchB.GetStartingChannel();

			return bUniverseIsSmaller || (bUniverseIsEqual && bAddressIsSmallerOrEqual);
		});

	int32 Address = FixturePatches.Last()->GetStartingChannel() + FixturePatches.Last()->GetChannelSpan();
	int32 Universe = -1;
	if (Address > DMX_MAX_ADDRESS)
	{
		Address = 1;
		Universe = FixturePatches.Last()->GetUniverseID() + 1;
	}
	else
	{
		Universe = FixturePatches.Last()->GetUniverseID();
	}

	// Duplicate
	const FScopedTransaction DuplicateFixturePatchTransaction(TransactionText);
	DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));

	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewFixturePatches;
	for (const UDMXEntityFixturePatch* FixturePatchToDuplicate : FixturePatchesToDuplicate)
	{
		// If this is duplicated from one library onto another (e.g. when pasting via duplicate), create a Copy of the Fixture Type in the duplicated to DMX Library.
		UDMXEntityFixtureType* FixtureTypeOfPatchToDuplicate = FixturePatchToDuplicate->GetFixtureType();
		if (FixtureTypeOfPatchToDuplicate && FixtureTypeOfPatchToDuplicate->GetParentLibrary() != DMXLibrary)
		{
			FDMXEntityFixtureTypeConstructionParams FixtureTypeConstructionParams;
			FixtureTypeConstructionParams.DMXCategory = FixtureTypeOfPatchToDuplicate->DMXCategory;
			FixtureTypeConstructionParams.Modes = FixtureTypeOfPatchToDuplicate->Modes;
			FixtureTypeConstructionParams.ParentDMXLibrary = DMXLibrary;

			UDMXEntityFixtureType::CreateFixtureTypeInLibrary(FixtureTypeConstructionParams, FixtureTypeOfPatchToDuplicate->Name);
		}

		// Duplicate the Fixture Patch
		const int32 ChannelSpan = FixturePatchToDuplicate->GetChannelSpan();
		if (Address + ChannelSpan - 1 > DMX_MAX_ADDRESS)
		{
			Address = 1;
			Universe++;
		}

		FDMXEntityFixturePatchConstructionParams ConstructionParams;
		ConstructionParams.FixtureTypeRef = FixturePatchToDuplicate->GetFixtureType();
		ConstructionParams.ActiveMode = FixturePatchToDuplicate->GetActiveModeIndex();
		ConstructionParams.UniverseID = Universe;
		ConstructionParams.StartingAddress = Address;

		UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(ConstructionParams, FixturePatchToDuplicate->Name);
		NewFixturePatches.Add(NewFixturePatch);

		// Duplicate the MVR Fixture if it exists
		UDMXMVRGeneralSceneDescription* GeneralSceneDescription = DMXLibrary->GetLazyGeneralSceneDescription();
		if (ensureAlwaysMsgf(GeneralSceneDescription, TEXT("Trying to duplicate MVR Fixture in the General Scene Description of '%s', but the General Scene Desription is not valid."), *DMXLibrary->GetName()))
		{
			FDMXMVRFixture* SourceMVRFixturePtr = GeneralSceneDescription->FindMVRFixture(FixturePatchToDuplicate->GetMVRFixtureUUID());
			if (SourceMVRFixturePtr)
			{
				FDMXMVRFixture MVRFixture = *SourceMVRFixturePtr;
				MVRFixture.UUID = NewFixturePatch->GetMVRFixtureUUID();
				DMXLibrary->GetLazyGeneralSceneDescription()->AddMVRFixture(MVRFixture);
			}
		}

		Address += ChannelSpan;
	}

	DMXLibrary->PostEditChange();

	if (NewFixturePatches.Num() > 0)
	{
		FixturePatchSharedData->SelectFixturePatches(NewFixturePatches);
	}
}

FDMXMVRFixture* FDMXMVRFixtureListItem::FindMVRFixture() const
{
	UDMXLibrary* DMXLibrary = GetDMXLibrary();

	if (!DMXLibrary)
	{
		return nullptr;
	}

	DMXLibrary->UpdateGeneralSceneDescription();
	UDMXMVRGeneralSceneDescription* GeneralSceneDescription = DMXLibrary->GetLazyGeneralSceneDescription();
	if (!ensureMsgf(GeneralSceneDescription, TEXT("Found Library without a General Scene Description. This should never occur.")))
	{
		return nullptr;
	}

	return GeneralSceneDescription->FindMVRFixture(MVRFixtureUUID);
}

#undef LOCTEXT_NAMESPACE
