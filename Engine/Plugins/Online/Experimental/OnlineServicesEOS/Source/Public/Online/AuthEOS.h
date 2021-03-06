// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/AuthEOSGS.h"

namespace UE::Online {

class FOnlineServicesEOS;

class ONLINESERVICESEOS_API FAuthEOS : public FAuthEOSGS
{
public:
	using Super = FAuthEOSGS;

	FAuthEOS(FOnlineServicesEOS& InOwningSubsystem);
	virtual ~FAuthEOS() = default;

	// Begin IAuth
	virtual void Initialize() override;
	virtual TOnlineAsyncOpHandle<FAuthLogin> Login(FAuthLogin::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FAuthLogout> Logout(FAuthLogout::Params&& Params) override;
	// End IAuth

	// Begin FAuthEOSGS
	virtual TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_ProductUserId ProductUserId) override;
	virtual TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_ProductUserId>& ProductUserIds) override;
	virtual TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_ProductUserId& ProductUserId)> ResolveProductIdFn() override;
	virtual TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_ProductUserId>& ProductUserIds)> ResolveProductIdsFn() override;
	// End FAuthEOSGS

	TFuture<FOnlineAccountIdHandle> ResolveAccountId(const FOnlineAccountIdHandle& LocalUserId, const EOS_EpicAccountId EpicAccountId);
	TFuture<TArray<FOnlineAccountIdHandle>> ResolveAccountIds(const FOnlineAccountIdHandle& LocalUserId, const TArray<EOS_EpicAccountId>& EpicAccountIds);
	TFunction<TFuture<FOnlineAccountIdHandle>(FOnlineAsyncOp& InAsyncOp, const EOS_EpicAccountId& EpicAccountId)> ResolveEpicIdFn();
	TFunction<TFuture<TArray<FOnlineAccountIdHandle>>(FOnlineAsyncOp& InAsyncOp, const TArray<EOS_EpicAccountId>& EpicAccountIds)> ResolveEpicIdsFn();

protected:
	TOnlineChainableAsyncOp<FAuthLogin, TSharedPtr<FEOSConnectLoginCredentials>> LoginEAS(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);
	void ProcessSuccessfulLogin(TOnlineAsyncOp<FAuthLogin>& InAsyncOp);
	void OnEASLoginStatusChanged(FOnlineAccountIdHandle LocalUserId, ELoginStatus PreviousStatus, ELoginStatus CurrentStatus);

	static FOnlineAccountIdHandle CreateAccountId(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId);

	EOS_NotificationId NotifyEASLoginStatusChangedNotificationId = EOS_INVALID_NOTIFICATIONID;
};

/* UE::Online */ }
