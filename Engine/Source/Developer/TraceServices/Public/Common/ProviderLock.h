// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"

#include "HAL/CriticalSection.h"

namespace TraceServices
{

struct TRACESERVICES_API FProviderEditScopeLock
{
	FProviderEditScopeLock(const IProvider& InProvider)
		: Provider(InProvider)
	{
		Provider.BeginEdit();
	}

	~FProviderEditScopeLock()
	{
		Provider.EndEdit();
	}

	const IProvider& Provider;
};

struct TRACESERVICES_API FProviderReadScopeLock
{
	FProviderReadScopeLock(const IProvider& InProvider)
		: Provider(InProvider)
	{
		Provider.BeginRead();
	}

	~FProviderReadScopeLock()
	{
		Provider.EndRead();
	}

	const IProvider& Provider;
};

class FProviderLock
{
public:
	void ReadAccessCheck(const FProviderLock* CurrentProviderLock, const int32& CurrentReadProviderLockCount, const int32& CurrentWriteAllocationsProviderLockCount) const;
	void WriteAccessCheck(const int32& CurrentWriteProviderLockCount) const;

	void BeginRead(FProviderLock*& CurrentProviderLock, int32& CurrentReadProviderLockCount, const int32& WriteAllocationsProviderLockCount);
	void EndRead(FProviderLock*& CurrentProviderLock, int32& CurrentReadProviderLockCount);

	void BeginWrite(FProviderLock*& CurrentProviderLock, const int32& CurrentReadProviderLockCount, int32& WriteAllocationsProviderLockCount);
	void EndWrite(FProviderLock*& CurrentProviderLock, int32& WriteAllocationsProviderLockCount);

private:
	FRWLock RWLock;
};

} // namespace TraceServices
