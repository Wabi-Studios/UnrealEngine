// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class ISslManager;
class ISslCertificateManager;

/**
 * Module for SSL/TLS certificate management
 */
class FSslModule : 
	public IModuleInterface, public FSelfRegisteringExec
{

public:

	// FSelfRegisteringExec

	/**
	 * Handle exec commands starting with "SSL"
	 *
	 * @param InWorld	the world context
	 * @param Cmd		the exec command being executed
	 * @param Ar		the archive to log results to
	 *
	 * @return true if the handler consumed the input, false to continue searching handlers
	 */
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

	// FSslModule

	/** Constructor */
	FSslModule();

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	SSL_API static FSslModule& Get();

	/**
	 * Accessor for the SSL certificate manager
	 *
	 * @return SSL certificate manager manager
	 */
	inline ISslCertificateManager& GetCertificateManager()
	{
		check(CertificateManagerPtr != nullptr);
		return *CertificateManagerPtr;
	}

	inline ISslManager& GetSslManager()
	{
		check(SslManagerPtr != nullptr);
		return *SslManagerPtr;
	}

private:

	// IModuleInterface

	/**
	 * Called when Http module is loaded
	 * Initialize platform specific parts of Http handling
	 */
	virtual void StartupModule() override;
	
	/**
	 * Called when Http module is unloaded
	 * Shutdown platform specific parts of Http handling
	 */
	virtual void ShutdownModule() override;

	/** Keeps track of SSL certificates */
	ISslCertificateManager* CertificateManagerPtr;

	/** Manager of the SSL library */
	ISslManager* SslManagerPtr;

	/** singleton for the module while loaded and available */
	static FSslModule* Singleton;
};
