/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2023 Ethan Lee
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

#include <SDL.h>
#include "FNA3D_Driver.h"

/* Definitions */

typedef uint64_t FNA3D_Memory_Size;

#define STARTING_ALLOCATION_SIZE 64000000 /* 64MB */
#define ALLOCATION_INCREMENT 16000000 /* 16MB */
#define MAX_ALLOCATION_SIZE 256000000 /* 256MB */
#define FAST_TRANSFER_SIZE 64000000 /* 64MB */
#define STARTING_TRANSFER_BUFFER_SIZE 8000000 /* 8MB */

/* Utilities */

static inline FNA3D_Memory_Size FNA3D_Memory_NextHighestAlignment(
	FNA3D_Memory_Size n,
	FNA3D_Memory_Size align
) {
	return align * ((n + align - 1) / align);
}

/* Structs */

typedef struct FNA3D_Memory_Allocation FNA3D_Memory_Allocation;

typedef struct FNA3D_Memory_FreeRegion
{
	FNA3D_Memory_Allocation *allocation;
	FNA3D_Memory_Size offset;
	FNA3D_Memory_Size size;
	uint32_t allocationIndex;
	uint32_t sortedIndex;
} FNA3D_Memory_FreeRegion;

typedef struct FNA3D_Memory_UsedRegion
{
	FNA3D_Memory_Allocation *allocation;
	FNA3D_Memory_Size offset;
	FNA3D_Memory_Size size;
	FNA3D_Memory_Size resourceOffset; /* differs from offset based on alignment */
	FNA3D_Memory_Size resourceSize; /* differs from size based on alignment */
	FNA3D_Memory_Size alignment;
	uint8_t isBuffer;
	/* used to copy resource */
	FNA3DNAMELESS union
	{
		FNA3D_Buffer *buffer;
		FNA3D_Texture *texture;
	};
} FNA3D_Memory_UsedRegion;

typedef struct FNA3D_Memory_SubAllocator
{
	uint32_t memoryTypeIndex;
	FNA3D_Memory_Size nextAllocationSize;
	FNA3D_Memory_Allocation **allocations;
	uint32_t allocationCount;
	FNA3D_Memory_FreeRegion **sortedFreeRegions;
	uint32_t sortedFreeRegionCount;
	uint32_t sortedFreeRegionCapacity;
} FNA3D_Memory_SubAllocator;

struct FNA3D_Memory_Allocation
{
	FNA3D_Memory_SubAllocator *allocator;
	void* memory;
	FNA3D_Memory_Size size;
	FNA3D_Memory_UsedRegion **usedRegions;
	uint32_t usedRegionCount;
	uint32_t usedRegionCapacity;
	FNA3D_Memory_FreeRegion **freeRegions;
	uint32_t freeRegionCount;
	uint32_t freeRegionCapacity;
	uint8_t dedicated;
	uint8_t availableForAllocation;
	FNA3D_Memory_Size freeSpace;
	FNA3D_Memory_Size usedSpace;
	uint8_t *mapPointer;
	SDL_mutex *mapLock;
};

typedef struct FNA3D_Memory_MemoryAllocator
{
	FNA3D_Memory_SubAllocator *subAllocators;
	uint8_t numSubAllocators;
} FNA3D_Memory_MemoryAllocator;

typedef struct FNA3D_Memory_Properties
{
	FNA3D_Memory_Size requiredSize;
	FNA3D_Memory_Size requiredAlignment;
	uint32_t memoryTypeIndex;
	uint8_t shouldAllocDedicated;
	uint8_t isHostVisible;
	uint8_t isDeviceLocal;
} FNA3D_Memory_Properties;

typedef struct FNA3D_Memory_Context FNA3D_Memory_Context;

typedef void (FNA3DCALL * FNA3D_Memory_FreeMemoryFunc)(FNA3D_Memory_Context *context, void* memory);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_AllocateMemoryFunc)(FNA3D_Memory_Context *context, void* userdata, void** pAllocation);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_MapMemoryFunc)(FNA3D_Memory_Context *context, FNA3D_Memory_Allocation *allocation);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_FindBufferMemoryRequirementsFunc)(FNA3D_Memory_Context *context, void* nativeBuffer, uint8_t preferDeviceLocal, FNA3D_Memory_Properties *pMemoryProperties);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_FindTextureMemoryRequirementsFunc)(FNA3D_Memory_Context *context, void* nativeTexture, uint8_t preferDeviceLocal, FNA3D_Memory_Properties *pMemoryProperties);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_BindBufferMemoryFunc)(FNA3D_Memory_Context *context, FNA3D_Memory_UsedRegion *usedRegion, FNA3D_Memory_Size alignedOffset, void* nativeBuffer);
typedef uint8_t (FNA3DCALL * FNA3D_Memory_BindTextureMemoryFunc)(FNA3D_Memory_Context *context, FNA3D_Memory_UsedRegion *usedRegion, FNA3D_Memory_Size alignedOffset, void* nativeTexture);

typedef struct FNA3D_Memory_Context
{
	FNA3D_Renderer *renderer;

	uint8_t bufferDefragInProgress;
	uint8_t needDefrag;
	uint32_t defragTimer;
	uint8_t resourceFreed;

	uint8_t unifiedMemoryWarning;
	FNA3D_Memory_Size maxDeviceLocalHeapUsage;
	FNA3D_Memory_Size deviceLocalHeapUsage;
	FNA3D_Memory_MemoryAllocator *memoryAllocator;
	SDL_mutex *allocatorLock;

	/* Driver-specific callbacks */
	FNA3D_Memory_FreeMemoryFunc freeMemory;
	FNA3D_Memory_AllocateMemoryFunc allocateMemory;
	FNA3D_Memory_MapMemoryFunc mapMemory;
	FNA3D_Memory_FindBufferMemoryRequirementsFunc findBufferMemoryRequirements;
	FNA3D_Memory_FindTextureMemoryRequirementsFunc findTextureMemoryRequirements;
	FNA3D_Memory_BindBufferMemoryFunc bindBufferMemory;
	FNA3D_Memory_BindTextureMemoryFunc bindTextureMemory;

} FNA3D_Memory_Context;

/* Functions */

void FNA3D_Memory_MakeMemoryUnavailable(
	FNA3D_Memory_Allocation *allocation
);

void FNA3D_Memory_RemoveMemoryUsedRegion(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_UsedRegion *usedRegion
);

void FNA3D_Memory_DeallocateMemory(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_SubAllocator *allocator,
	uint32_t allocationIndex
);

uint8_t FNA3D_Memory_AllocateMemory(
	FNA3D_Memory_Context *context,
	void* userdata,
	uint32_t memoryTypeIndex,
	FNA3D_Memory_Size allocationSize,
	uint8_t dedicated,
	uint8_t isHostVisible,
	FNA3D_Memory_Allocation **pMemoryAllocation
);

uint8_t FNA3D_Memory_FindAllocationToDefragment(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_SubAllocator *allocator,
	uint32_t *allocationIndexToDefrag
);

uint8_t FNA3D_Memory_BindMemoryForBuffer(
	FNA3D_Memory_Context *context,
	void* nativeBuffer,
	FNA3D_Memory_Size size,
	uint8_t preferDeviceLocal,
	uint8_t isTransferBuffer,
	FNA3D_Memory_UsedRegion **pMemoryUsedRegion
);

uint8_t FNA3D_Memory_BindMemoryForTexture(
	FNA3D_Memory_Context *context,
	void* nativeTexture,
	uint8_t isRenderTarget,
	FNA3D_Memory_UsedRegion** usedRegion
);

#endif /* FNA3D_MEMORY_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */

