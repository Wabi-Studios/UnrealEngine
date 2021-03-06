// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Tables.h"
#include "Templates/Function.h"

namespace TraceServices
{

struct FLogCategoryInfo
{
	const TCHAR* Name = nullptr;
	ELogVerbosity::Type DefaultVerbosity;
};

struct FLogMessageInfo
{
	uint64 Index;
	double Time;
	const FLogCategoryInfo* Category = nullptr;
	const TCHAR* File = nullptr;
	const TCHAR* Message = nullptr;
	int32 Line;
	ELogVerbosity::Type Verbosity;
};

class ILogProvider
	: public IProvider
{
public:
	virtual ~ILogProvider() = default;
	virtual uint64 GetMessageCount() const = 0;
	virtual void EnumerateMessages(double IntervalStart, double IntervalEnd, TFunctionRef<void(const FLogMessageInfo&)> Callback) const = 0;
	virtual void EnumerateMessagesByIndex(uint64 Start, uint64 End, TFunctionRef<void(const FLogMessageInfo&)> Callback) const = 0;
	virtual bool ReadMessage(uint64 Index, TFunctionRef<void(const FLogMessageInfo&)> Callback) const = 0;
	virtual uint64 GetCategoryCount() const = 0;
	virtual void EnumerateCategories(TFunctionRef<void(const FLogCategoryInfo&)> Callback) const = 0;
	virtual const IUntypedTable& GetMessagesTable() const = 0;
};

TRACESERVICES_API const ILogProvider& ReadLogProvider(const IAnalysisSession& Session);
TRACESERVICES_API void FormatString(TCHAR* OutputString, uint32 OutputStringCount, const TCHAR* FormatString, const uint8* FormatArgs);

} // namespace TraceServices
