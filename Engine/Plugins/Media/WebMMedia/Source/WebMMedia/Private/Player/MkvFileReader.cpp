// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MkvFileReader.h"

#if WITH_WEBM_LIBS

#include "HAL/PlatformFilemanager.h"
#include "GenericPlatform/GenericPlatformFile.h"

bool FMkvFileReader::Open(const TCHAR* Filename)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	m_file.Reset(PlatformFile.OpenRead(Filename));
	return m_file.IsValid();
}

int FMkvFileReader::Read(long long Position, long Lenght, unsigned char* Buffer)
{
	check(m_file.IsValid());

	m_file->Seek(Position);
	m_file->Read(Buffer, Lenght);
	return 0 /* success */;
}

int FMkvFileReader::Length(long long* Total, long long* Available)
{
	check(m_file.IsValid());

	int64 CurrentPosition = m_file->Tell();

	m_file->SeekFromEnd(0);
	int64 FileSize = m_file->Tell();

	m_file->Seek(CurrentPosition);

	// Don't know the difference between Total and Available, but below code is required to get MkvParser going

	if (Total)
	{
		*Total = FileSize;
	}

	if (Available)
	{
		*Available = FileSize;
	}

	return 0 /* success */;
}

#endif // WITH_WEBM_LIBS
