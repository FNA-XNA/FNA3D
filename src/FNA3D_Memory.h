/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2024 Ethan Lee
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#ifndef FNA3D_MEMORY_H
#define FNA3D_MEMORY_H

#include "FNA3D_Driver.h"

#include <stddef.h> /* for size_t */

/* Driver-defined memory structures  */

typedef uint64_t FNA3D_MemoryPlatformHandle;
typedef struct FNA3D_BufferHandle FNA3D_BufferHandle;

/* Renderers will contain a memory driver, so we can call back into the renderer
 * to do platform-specific things that don't have to do with the high-level
 * memory management
 */

typedef struct FNA3D_MemoryDriver
{
	uint8_t (*AllocDeviceMemory)(
		FNA3D_Renderer *driverData,
		size_t subAllocatorIndex,
		size_t memorySize,
		uint8_t deviceLocal,
		uint8_t hostVisible,
		FNA3D_MemoryPlatformHandle* driverMemory,
		uint8_t **mapPointer
	);
	void (*FreeDeviceMemory)(
		FNA3D_Renderer *driverData,
		FNA3D_MemoryPlatformHandle driverMemory,
		size_t memorySize,
		size_t subAllocatorIndex
	);
	uint8_t (*BindBufferMemory)(
		FNA3D_Renderer *driverData,
		FNA3D_MemoryPlatformHandle deviceMemory,
		size_t alignedOffset,
		FNA3D_MemoryPlatformHandle buffer
	);
	uint8_t (*BindImageMemory)(
		FNA3D_Renderer *driverData,
		FNA3D_MemoryPlatformHandle deviceMemory,
		size_t alignedOffset,
		FNA3D_MemoryPlatformHandle image
	);
	void (*BeginDefragCommands)(
		FNA3D_Renderer *driverData
	);
	void (*EndDefragCommands)(
		FNA3D_Renderer *driverData
	);
	uint8_t (*DefragBuffer)(
		FNA3D_Renderer *driverData,
		void* resource,
		size_t resourceSize
	);
	uint8_t (*DefragImage)(
		FNA3D_Renderer *driverData,
		void* resource,
		size_t resourceSize
	);

	FNA3D_BufferHandle *(*CreateBufferHandle)(
		FNA3D_Renderer *driverData,
		uint8_t isVertexData,
		size_t sizeInBytes
	);
	FNA3D_BufferHandle *(*CloneBufferHandle)(
		FNA3D_Renderer *driverData,
		FNA3D_BufferHandle *buffer
	);
	void (*MarkBufferHandlesForDestroy)(
		FNA3D_Renderer *driverData,
		FNA3D_BufferHandle **buffers,
		size_t bufferCount
	);
	uint8_t (*BufferHandleInUse)(
		FNA3D_Renderer *driverData,
		FNA3D_BufferHandle *buffer
	);

	FNA3D_Renderer *driverData;
} FNA3D_MemoryDriver;

#define ASSIGN_MEMORY_DRIVER_FUNC(func, name) \
	memoryDriver.func = name##_Memory_##func;
#define ASSIGN_MEMORY_DRIVER(name) \
	memoryDriver.driverData = (FNA3D_Renderer*) renderer; \
	ASSIGN_MEMORY_DRIVER_FUNC(AllocDeviceMemory, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(FreeDeviceMemory, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(BindBufferMemory, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(BindImageMemory, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(BeginDefragCommands, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(EndDefragCommands, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(DefragBuffer, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(DefragImage, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(CreateBufferHandle, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(CloneBufferHandle, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(MarkBufferHandlesForDestroy, name) \
	ASSIGN_MEMORY_DRIVER_FUNC(BufferHandleInUse, name)

/*
 * Memory Allocator Interface
 */

typedef struct FNA3D_MemoryAllocator FNA3D_MemoryAllocator;

typedef struct FNA3D_MemoryUsedRegion FNA3D_MemoryUsedRegion;

FNA3D_SHAREDINTERNAL FNA3D_MemoryAllocator* FNA3D_CreateMemoryAllocator(
	FNA3D_MemoryDriver *driver,
	size_t numSubAllocators
);

FNA3D_SHAREDINTERNAL void FNA3D_DestroyMemoryAllocator(FNA3D_MemoryAllocator *allocator);

FNA3D_SHAREDINTERNAL void FNA3D_Memory_LockAllocator(FNA3D_MemoryAllocator *allocator);
FNA3D_SHAREDINTERNAL void FNA3D_Memory_UnlockAllocator(FNA3D_MemoryAllocator *allocator);

FNA3D_SHAREDINTERNAL uint8_t FNA3D_Memory_BindResource(
	FNA3D_MemoryAllocator *allocator,
	size_t subAllocatorIndex,
	size_t requiredSize,
	size_t requiredAlignment,
	uint8_t isHostVisible,
	uint8_t isDeviceLocal,
	uint8_t shouldAllocDedicated,
	size_t resourceSize, /* may be different from requirements size! */
	uint8_t resourceIsImage,
	FNA3D_MemoryPlatformHandle resource,
	void* defragResourceHandle,
	FNA3D_MemoryUsedRegion** pMemoryUsedRegion
);

FNA3D_SHAREDINTERNAL uint8_t* FNA3D_Memory_GetHostPointer(
	FNA3D_MemoryUsedRegion *usedRegion,
	size_t offset
);

FNA3D_SHAREDINTERNAL void FNA3D_Memory_FreeEmptyAllocations(FNA3D_MemoryAllocator *allocator);

FNA3D_SHAREDINTERNAL uint8_t FNA3D_Memory_Defragment(FNA3D_MemoryAllocator *allocator);

FNA3D_SHAREDINTERNAL uint8_t FNA3D_Memory_DestroyDefragmentedRegions(FNA3D_MemoryAllocator *allocator);

FNA3D_SHAREDINTERNAL uint8_t FNA3D_INTERNAL_RemoveMemoryUsedRegion(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_MemoryUsedRegion *usedRegion
);

/*
 * Buffer Container Interface
 */

/* Buffer containers refer to locations in memory previously allocated
 *
 * To properly support SETDATAOPTIONS_DISCARD the renderer's "buffer handle" is
 * actually a container pointing to a buffer.
 *
 * This lets us change out the internal buffer without affecting
 * the client or requiring a stall.
 *
 * The "discarded" buffers are kept around to avoid memory fragmentation
 * being created by buffers that frequently discard.
 */
typedef struct FNA3D_BufferContainer FNA3D_BufferContainer;

FNA3D_SHAREDINTERNAL FNA3D_BufferContainer* FNA3D_Memory_CreateBufferContainer(
	FNA3D_MemoryAllocator *allocator,
	uint8_t isVertexData,
	size_t sizeInBytes
);

FNA3D_SHAREDINTERNAL void FNA3D_Memory_DestroyBufferContainer(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_BufferContainer *container
);

FNA3D_SHAREDINTERNAL FNA3D_BufferHandle* FNA3D_Memory_GetActiveBuffer(
	FNA3D_BufferContainer *container
);

FNA3D_SHAREDINTERNAL FNA3D_BufferHandle* FNA3D_Memory_DiscardActiveBuffer(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_BufferContainer *container
);

#endif /* FNA3D_MEMORY_H */
