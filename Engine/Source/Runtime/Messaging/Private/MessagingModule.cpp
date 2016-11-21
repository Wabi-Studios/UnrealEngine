// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MessagingPrivatePCH.h"
#include "IMessagingModule.h"
#include "MessageBridge.h"
#include "MessageBus.h"


#ifndef PLATFORM_SUPPORTS_MESSAGEBUS
	#define PLATFORM_SUPPORTS_MESSAGEBUS 1
#endif


/**
 * Implements the Messaging module.
 */
class FMessagingModule
	: public FSelfRegisteringExec
	, public IMessagingModule
{
public:

	//~ FSelfRegisteringExec interface

	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override
	{
		if (!FParse::Command(&Cmd, TEXT("MESSAGING")))
		{
			return false;
		}

		if (FParse::Command(&Cmd, TEXT("STATUS")))
		{
			if (DefaultBus.IsValid())
			{
				Ar.Log(TEXT("Default message bus has been initialized."));
			}
			else
			{
				Ar.Log(TEXT("Default message bus has NOT been initialized yet."));
			}
		}
		else
		{
			// show usage
			Ar.Log(TEXT("Usage: MESSAGING <Command>"));
			Ar.Log(TEXT(""));
			Ar.Log(TEXT("Command"));
			Ar.Log(TEXT("    STATUS = Displays the status of the default message bus"));
		}

		return true;
	}

public:

	//~ IMessagingModule interface

	virtual TSharedPtr<IMessageBridge, ESPMode::ThreadSafe> CreateBridge(const FMessageAddress& Address, const TSharedRef<IMessageBus, ESPMode::ThreadSafe>& Bus, const IMessageTransportRef& Transport) override
	{
		return MakeShareable(new FMessageBridge(Address, Bus, Transport));
	}

	virtual TSharedPtr<IMessageBus, ESPMode::ThreadSafe> CreateBus(const TSharedPtr<IAuthorizeMessageRecipients>& RecipientAuthorizer) override
	{
		return MakeShareable(new FMessageBus(RecipientAuthorizer));
	}

	virtual TSharedPtr<IMessageBus, ESPMode::ThreadSafe> GetDefaultBus() const override
	{
		return DefaultBus;
	}

public:

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
#if PLATFORM_SUPPORTS_MESSAGEBUS
		FCoreDelegates::OnPreExit.AddRaw(this, &FMessagingModule::HandleCorePreExit);
		DefaultBus = CreateBus(nullptr);
#endif	//PLATFORM_SUPPORTS_MESSAGEBUS
	}

	virtual void ShutdownModule() override
	{
		ShutdownDefaultBus();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

protected:

	void ShutdownDefaultBus()
	{
		if (!DefaultBus.IsValid())
		{
			return;
		}

		TWeakPtr<IMessageBus, ESPMode::ThreadSafe> DefaultBusPtr = DefaultBus;

		DefaultBus->Shutdown();
		DefaultBus.Reset();

		// wait for the bus to shut down
		int32 SleepCount = 0;

		while (DefaultBusPtr.IsValid())
		{
			check(SleepCount < 10);	// something is holding on to the message bus
			++SleepCount;

			FPlatformProcess::Sleep(0.1f);
		}
	}

private:

	/** Callback for Core shutdown. */
	void HandleCorePreExit()
	{
		ShutdownDefaultBus();
	}

private:

	/** Holds the message bus. */
	TSharedPtr<IMessageBus, ESPMode::ThreadSafe> DefaultBus;
};


IMPLEMENT_MODULE(FMessagingModule, Messaging);
