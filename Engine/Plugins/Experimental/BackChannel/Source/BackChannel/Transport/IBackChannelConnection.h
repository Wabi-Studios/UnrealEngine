// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSocket;

// todo (agrant 2017/12/29): concept of 'connection' should be a base class with persistent connection subclass?

/**
 *	Base class that describes a back-channel connection. The underlying behavior will depend on the type
 *	of connection that was requested from the factory
 */
class IBackChannelConnection
{
public:
	
	// todo (agrant 2017/12/29): Should remove 'Connect' and instead return a connected (or null..) socket
	// from the factory

	/* Start connecting to the specified port for incoming connections. Use WaitForConnection to check status. */
	virtual bool Connect(const TCHAR* InEndPoint) = 0;

	/* Start listening on the specified port for incoming connections. Use WaitForConnection to accept one. */
	virtual bool Listen(const int16 Port) = 0;

	/* Close the connection */
	virtual void Close() = 0;

	/* Waits for an icoming or outgoing connection to be made */
	virtual bool WaitForConnection(double InTimeout, TFunction<bool(TSharedRef<IBackChannelConnection>)> InDelegate) = 0;

	/* Returns true if this connection is currently listening for incoming connections */
	virtual bool IsListening() const = 0;

	/* Returns true if this connection is connected to another */
	virtual bool IsConnected() const = 0;

	/* Send data via our connection */
	virtual int32 SendData(const void* InData, const int32 InSize) = 0;
	
	/* Receive data from our connection. The returned value is the number of bytes read, to a max of BufferSize */
	virtual int32 ReceiveData(void* OutBuffer, const int32 BufferSize) = 0;

	/* Return the underlying socket (if any) for this connection */
	virtual FString GetDescription() const = 0;

	/* Return the underlying socket (if any) for this connection */
	virtual FSocket* GetSocket() = 0;

	/* Todo - Proper stats */
	virtual uint32	GetPacketsReceived() const = 0;

protected:

	IBackChannelConnection() {}
	virtual ~IBackChannelConnection() {}
};

