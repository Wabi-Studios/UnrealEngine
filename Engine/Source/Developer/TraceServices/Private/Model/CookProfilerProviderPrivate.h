// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/CookProfilerProvider.h"
#include "TraceServices/AnalysisService.h"

namespace TraceServices
{

class FCookProfilerProvider
	: public ICookProfilerProvider
{
public:
	virtual void BeginEdit() const override;
	virtual void EndEdit() const override;
	virtual void EditAccessCheck() const override;

	virtual void BeginRead() const override;
	virtual void EndRead() const override;
	virtual void ReadAccessCheck() const override;

	explicit FCookProfilerProvider(IAnalysisSession& Session);
	virtual ~FCookProfilerProvider() {}

	// Functions to add or edit data.
	void AddPackage(uint64 Id, TStringView<TCHAR> Name);
	FPackageData* EditPackage(uint64 Id);

	// Functions to query the data.
	virtual void EnumeratePackages(double StartTime, double EndTime, EnumeratePackagesCallback Callback) const override;
	virtual uint32 GetNumPackages() const;

private:
	IAnalysisSession& Session;
	TMap<uint64, uint32> PackageIdToIndexMap;
	TArray64<FPackageData> Packages;

	mutable FProviderLock Lock;
};

} // namespace TraceServices
