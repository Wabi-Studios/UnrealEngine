// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "RemoteSession.h"
#include "Framework/Application/SlateApplication.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "RemoteSessionHost.h"
#include "RemoteSessionClient.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_EDITOR
	#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FRemoteSessionModule"


class FRemoteSessionModule : public IRemoteSessionModule, public FTickableGameObject
{
protected:

	TSharedPtr<FRemoteSessionHost>		Host;
	TSharedPtr<FRemoteSessionClient>	Client;

	int32								DefaultPort;
	int32								Quality;
	int32								Framerate;

	bool                                bAutoHostWithPIE;
	bool                                bAutoHostWithGame;
    
	FDelegateHandle PostPieDelegate;
	FDelegateHandle EndPieDelegate;
	FDelegateHandle GameStartDelegate;

public:

	void SetAutoStartWithPIE(bool bEnable)
	{
		bAutoHostWithPIE = bEnable;
	}

	void StartupModule()
	{
		// set defaults
		DefaultPort = IRemoteSessionModule::kDefaultPort;
		Quality = 85;
		Framerate = 30;
		bAutoHostWithPIE = true;
		bAutoHostWithGame = true;

		GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAutoHostWithGame"), bAutoHostWithGame, GEngineIni);
		GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAutoHostWithPIE"), bAutoHostWithPIE, GEngineIni);
		GConfig->GetInt(TEXT("RemoteSession"), TEXT("HostPort"), DefaultPort, GEngineIni);
		GConfig->GetInt(TEXT("RemoteSession"), TEXT("Quality"), Quality, GEngineIni);
		GConfig->GetInt(TEXT("RemoteSession"), TEXT("Framerate"), Framerate, GEngineIni);


		if (PLATFORM_DESKTOP 
			&& IsRunningDedicatedServer() == false 
			&& IsRunningCommandlet() == false)
		{
#if WITH_EDITOR
			PostPieDelegate = FEditorDelegates::PostPIEStarted.AddRaw(this, &FRemoteSessionModule::OnPIEStarted);
			EndPieDelegate = FEditorDelegates::EndPIE.AddRaw(this, &FRemoteSessionModule::OnPIEEnded);
#endif
			GameStartDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FRemoteSessionModule::OnGameStarted);
		}
	}

	void ShutdownModule()
	{
		// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
		// we call this function before unloading the module.
#if WITH_EDITOR
		if (PostPieDelegate.IsValid())
		{
			FEditorDelegates::PostPIEStarted.Remove(PostPieDelegate);
		}

		if (EndPieDelegate.IsValid())
		{
			FEditorDelegates::EndPIE.Remove(EndPieDelegate);
		}
#endif

		if (GameStartDelegate.IsValid())
		{
			FCoreDelegates::OnFEngineLoopInitComplete.Remove(GameStartDelegate);
		}
	}

	void OnGameStarted()
	{
		bool IsHostGame = PLATFORM_DESKTOP
			&& GIsEditor == false
			&& IsRunningDedicatedServer() == false
			&& IsRunningCommandlet() == false;

		if (IsHostGame && bAutoHostWithGame)
		{
			InitHost();
		}
	}

	void OnPIEStarted(bool bSimulating)
	{
		if (bAutoHostWithPIE)
		{
			InitHost();
		}
	}

	void OnPIEEnded(bool bSimulating)
	{
		// always stop, incase it was started via the console
		StopHost();
	}
	
	virtual TSharedPtr<IRemoteSessionRole>	CreateClient(const TCHAR* RemoteAddress) override
	{
		// todo - remove this and allow multiple clients (and hosts) to be created
		if (Client.IsValid())
		{
			StopClient(Client);
		}
		Client = MakeShareable(new FRemoteSessionClient(RemoteAddress));
		return Client;
	}

	virtual void StopClient(TSharedPtr<IRemoteSessionRole> InClient) override
	{
		if (InClient.IsValid())
		{
			TSharedPtr<FRemoteSessionClient> CastClient = StaticCastSharedPtr<FRemoteSessionClient>(InClient);
			CastClient->Close();
			
			if (CastClient == Client)
			{
				Client = nullptr;
			}
		}
	}


	virtual void InitHost(const int16 Port = 0) override
	{
		if (Host.IsValid())
		{
			Host = nullptr;
		}

#if UE_BUILD_SHIPPING
		bool bAllowInShipping = false;
		GConfig->GetBool(TEXT("RemoteSession"), TEXT("bAllowInShipping"), bAllowInShipping, GEngineIni);

		if (bAllowInShipping == false)
		{
			UE_LOG(LogRemoteSession, Log, TEXT("RemoteSession is disabled. Shipping=1"));
			return;
		}
#endif

		TSharedPtr<FRemoteSessionHost> NewHost = MakeShareable(new FRemoteSessionHost(Quality, Framerate));

		int16 SelectedPort = Port ? Port : (int16)DefaultPort;

		if (NewHost->StartListening(SelectedPort))
		{
			Host = NewHost;
			UE_LOG(LogRemoteSession, Log, TEXT("Started listening on port %d"), SelectedPort);
		}
		else
		{
			UE_LOG(LogRemoteSession, Error, TEXT("Failed to start host listening on port %d"), SelectedPort);
		}
	}

	virtual bool IsHostRunning() const override
	{
		return Host.IsValid();
	}

	virtual bool IsHostConnected() const override
	{
		return Host.IsValid() && Host->IsConnected();
	}

	virtual void StopHost() override
	{
		Host = nullptr;
	}

	virtual TSharedPtr<IRemoteSessionRole>	GetHost() const override
	{
		return Host;
	}

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FRemoteSession, STATGROUP_Tickables);
	}
	
	virtual bool IsTickable() const override
	{
		return true;
	}

	virtual void Tick(float DeltaTime) override
	{
		if (Client.IsValid())
		{
			Client->Tick(DeltaTime);
		}

		if (Host.IsValid())
		{
			Host->Tick(DeltaTime);
		}
	}	
};
	
IMPLEMENT_MODULE(FRemoteSessionModule, RemoteSession)

FAutoConsoleCommand GRemoteHostCommand(
	TEXT("remote.host"),
	TEXT("Starts a remote viewer host"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
		{
			Viewer->InitHost();
		}
	})
);

FAutoConsoleCommand GRemoteDisconnectCommand(
	TEXT("remote.disconnect"),
	TEXT("Disconnect remote viewer"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
		{
			//Viewer->StopClient();
			Viewer->StopHost();
		}
	})
);

FAutoConsoleCommand GRemoteAutoPIECommand(
	TEXT("remote.autopie"),
	TEXT("enables remote with pie"),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	if (FRemoteSessionModule* Viewer = FModuleManager::LoadModulePtr<FRemoteSessionModule>("RemoteSession"))
	{
		Viewer->SetAutoStartWithPIE(true);
	}
})
);

#undef LOCTEXT_NAMESPACE
