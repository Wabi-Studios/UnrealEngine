// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2016-2018 NVIDIA Corporation. All rights reserved.


#ifndef NVBLASTPXCALLBACKS_H
#define NVBLASTPXCALLBACKS_H

#include "NvBlastGlobals.h"
#include "PxErrorCallback.h"
#include "PxAllocatorCallback.h"

/**
This file contains helper functions to get PxShared compatible versions of global AllocatorCallback and ErrorCallback.
*/


NV_INLINE physx::PxErrorCallback& NvBlastGetPxErrorCallback()
{
	class PxErrorCallbackWrapper : public physx::PxErrorCallback
	{
		virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
		{
			NvBlastGlobalGetErrorCallback()->reportError((Nv::Blast::ErrorCode::Enum)code, message, file, line);
		}
	};
	static PxErrorCallbackWrapper wrapper;
	return wrapper;
}

NV_INLINE physx::PxAllocatorCallback& NvBlastGetPxAllocatorCallback()
{
	class PxAllocatorCallbackWrapper : public physx::PxAllocatorCallback
	{
		virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override
		{
			return NvBlastGlobalGetAllocatorCallback()->allocate(size, typeName, filename, line);
		}

		virtual void deallocate(void* ptr) override
		{
			NvBlastGlobalGetAllocatorCallback()->deallocate(ptr);
		}
	};
	static PxAllocatorCallbackWrapper wrapper;
	return wrapper;
}


#endif // #ifndef NVBLASTPXCALLBACKS_H
