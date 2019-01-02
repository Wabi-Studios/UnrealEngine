// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UniformBuffer.h: Uniform buffer declarations.
=============================================================================*/

#pragma once

#include "ShaderParameterMacros.h"
#include "RenderingThread.h"
#include "RenderResource.h"
#include "Templates/IsArrayOrRefOfType.h"

class FShaderUniformBufferParameter;
template<typename TBufferStruct> class TShaderUniformBufferParameter;

/** Creates a 
uniform buffer with the given value, and returns a structured reference to it. */
template<typename TBufferStruct>
TUniformBufferRef<TBufferStruct> CreateUniformBufferImmediate(const TBufferStruct& Value, EUniformBufferUsage Usage)
{
	return TUniformBufferRef<TBufferStruct>::CreateUniformBufferImmediate(Value, Usage);
}


/** A uniform buffer resource. */
template<typename TBufferStruct>
class TUniformBuffer : public FRenderResource
{
public:

	TUniformBuffer()
		: BufferUsage(UniformBuffer_MultiFrame)
		, Contents(nullptr)
	{
	}

	~TUniformBuffer()
	{
		if (Contents)
		{
			FMemory::Free(Contents);
		}
	}

	/** Sets the contents of the uniform buffer. */
	void SetContents(const TBufferStruct& NewContents)
	{
		SetContentsNoUpdate(NewContents);
		UpdateRHI();
	}
	/** Sets the contents of the uniform buffer to all zeros. */
	void SetContentsToZero()
	{
		if (!Contents)
		{
			Contents = (uint8*)FMemory::Malloc(sizeof(TBufferStruct), SHADER_PARAMETER_STRUCT_ALIGNMENT);
		}
		FMemory::Memzero(Contents, sizeof(TBufferStruct));
		UpdateRHI();
	}

	// FRenderResource interface.
	virtual void InitDynamicRHI() override
	{
		check(IsInRenderingThread());
		UniformBufferRHI.SafeRelease();
		if (Contents)
		{
			UniformBufferRHI = CreateUniformBufferImmediate<TBufferStruct>(*((const TBufferStruct*)Contents), BufferUsage);
		}
	}
	virtual void ReleaseDynamicRHI() override
	{
		UniformBufferRHI.SafeRelease();
	}

	// Accessors.
	FUniformBufferRHIParamRef GetUniformBufferRHI() const 
	{ 
		check(UniformBufferRHI.GetReference()); // you are trying to use a UB that was never filled with anything
		return UniformBufferRHI; 
	}

	const TUniformBufferRef<TBufferStruct>& GetUniformBufferRef() const
	{
		check(UniformBufferRHI.GetReference()); // you are trying to use a UB that was never filled with anything
		return UniformBufferRHI;
	}

	EUniformBufferUsage BufferUsage;

protected:

	/** Sets the contents of the uniform buffer. Used within calls to InitDynamicRHI */
	void SetContentsNoUpdate(const TBufferStruct& NewContents)
	{
		check(IsInRenderingThread());
		if (!Contents)
		{
			Contents = (uint8*)FMemory::Malloc(sizeof(TBufferStruct), SHADER_PARAMETER_STRUCT_ALIGNMENT);
		}
		FMemory::Memcpy(Contents,&NewContents,sizeof(TBufferStruct));
	}

private:

	TUniformBufferRef<TBufferStruct> UniformBufferRHI;
	uint8* Contents;
};

ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER_DECLARE_TEMPLATE(
	SetUniformBufferContents,TBufferStruct,
	TUniformBuffer<TBufferStruct>*,UniformBuffer,&UniformBuffer,
	TBufferStruct,Struct,Struct,
	{
		UniformBuffer->SetContents(Struct);
	});

/** Sends a message to the rendering thread to set the contents of a uniform buffer.  Called by the game thread. */
template<typename TBufferStruct>
void BeginSetUniformBufferContents(
	TUniformBuffer<TBufferStruct>& UniformBuffer,
	const TBufferStruct& Struct
	)
{
	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER_CREATE_TEMPLATE(
		SetUniformBufferContents,TBufferStruct,
		TUniformBuffer<TBufferStruct>*,&UniformBuffer,
		TBufferStruct,Struct);
}
