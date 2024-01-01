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

#include "FNA3D_Memory.h"

#include <SDL.h>

/*
 * Memory Allocator
 */

#define STARTING_ALLOCATION_SIZE 64000000 /* 64MB */
#define ALLOCATION_INCREMENT 16000000 /* 16MB */

typedef struct FNA3D_MemoryAllocation FNA3D_MemoryAllocation;

typedef struct FNA3D_MemoryFreeRegion
{
	FNA3D_MemoryAllocation *parent;

	size_t offset;
	size_t size;

	size_t allocationIndex;
	size_t sortedIndex;
} FNA3D_MemoryFreeRegion;

struct FNA3D_MemoryUsedRegion
{
	FNA3D_MemoryAllocation *parent;

	size_t offset;
	size_t size;
	size_t resourceOffset;	/* Differs from offset based on alignment */
	size_t resourceSize;	/* Differs from size based on alignment */
	size_t alignment;

	uint8_t isBuffer;
	void* defragResource;
};

typedef struct FNA3D_SubAllocator FNA3D_SubAllocator;

struct FNA3D_MemoryAllocation
{
	FNA3D_SubAllocator *parent;

	FNA3D_MemoryPlatformHandle handle;
	size_t size;

	FNA3D_MemoryUsedRegion **usedRegions;
	size_t usedRegionCount;
	size_t usedRegionCapacity;
	size_t usedSpace;

	FNA3D_MemoryFreeRegion **freeRegions;
	size_t freeRegionCount;
	size_t freeRegionCapacity;
	size_t freeSpace;

	uint8_t dedicated; /* For renderers with device-local memory */
	uint8_t available;

	uint8_t *mapPointer;
	SDL_mutex *mapLock;
};

struct FNA3D_SubAllocator
{
	size_t index; /* Index within FNA3D_MemoryAllocator */
	size_t nextAllocationSize;

	FNA3D_MemoryAllocation **allocations;
	size_t allocationCount;

	FNA3D_MemoryFreeRegion **sortedFreeRegions;
	size_t sortedFreeRegionCount;
	size_t sortedFreeRegionCapacity;
};

struct FNA3D_MemoryAllocator
{
	FNA3D_MemoryDriver driver;

	FNA3D_SubAllocator *subAllocators;
	size_t subAllocatorCount;

	FNA3D_MemoryUsedRegion **usedRegionsToDestroy;
	size_t usedRegionsToDestroyCount;
	size_t usedRegionsToDestroyCapacity;

	SDL_mutex *lock;
};

static void FNA3D_INTERNAL_RemoveMemoryFreeRegion(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_MemoryFreeRegion *freeRegion
) {
	size_t i;
	FNA3D_MemoryAllocation *allocation;
	FNA3D_SubAllocator *subAllocator;

	SDL_LockMutex(allocator->lock);

	allocation = freeRegion->parent;
	subAllocator = allocation->parent;

	if (allocation->available)
	{
		/* Close the gap in the sorted list */
		if (subAllocator->sortedFreeRegionCount > 1)
		{
			for (i = freeRegion->sortedIndex; i < subAllocator->sortedFreeRegionCount - 1; i += 1)
			{
				subAllocator->sortedFreeRegions[i] =
					subAllocator->sortedFreeRegions[i + 1];

				subAllocator->sortedFreeRegions[i]->sortedIndex = i;
			}
		}

		subAllocator->sortedFreeRegionCount -= 1;
	}

	/* Close the gap in the buffer list */
	if (allocation->freeRegionCount > 1 && freeRegion->allocationIndex != allocation->freeRegionCount - 1)
	{
		allocation->freeRegions[freeRegion->allocationIndex] =
			allocation->freeRegions[allocation->freeRegionCount - 1];

		allocation->freeRegions[freeRegion->allocationIndex]->allocationIndex =
			freeRegion->allocationIndex;
	}

	allocation->freeRegionCount -= 1;

	allocation->freeSpace -= freeRegion->size;

	SDL_free(freeRegion);

	SDL_UnlockMutex(allocator->lock);
}

static void FNA3D_INTERNAL_NewMemoryFreeRegion(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_MemoryAllocation *allocation,
	size_t offset,
	size_t size
) {
	FNA3D_SubAllocator *subAllocator;
	FNA3D_MemoryFreeRegion *newFreeRegion;
	size_t newOffset, newSize;
	size_t insertionIndex;
	int32_t i;

	SDL_LockMutex(allocator->lock);

	/* Look for an adjacent region to merge */
	for (i = allocation->freeRegionCount - 1; i >= 0; i -= 1)
	{
		/* Check left side */
		if (allocation->freeRegions[i]->offset + allocation->freeRegions[i]->size == offset)
		{
			newOffset = allocation->freeRegions[i]->offset;
			newSize = allocation->freeRegions[i]->size + size;

			FNA3D_INTERNAL_RemoveMemoryFreeRegion(
				allocator,
				allocation->freeRegions[i]
			);
			FNA3D_INTERNAL_NewMemoryFreeRegion(
				allocator,
				allocation,
				newOffset,
				newSize
			);

			SDL_UnlockMutex(allocator->lock);
			return;
		}

		/* Check right side */
		if (allocation->freeRegions[i]->offset == offset + size)
		{
			newOffset = offset;
			newSize = allocation->freeRegions[i]->size + size;

			FNA3D_INTERNAL_RemoveMemoryFreeRegion(
				allocator,
				allocation->freeRegions[i]
			);
			FNA3D_INTERNAL_NewMemoryFreeRegion(
				allocator,
				allocation,
				newOffset,
				newSize
			);

			SDL_UnlockMutex(allocator->lock);
			return;
		}
	}

	/* Region is not contiguous with another free region, make a new one */
	allocation->freeRegionCount += 1;
	if (allocation->freeRegionCount > allocation->freeRegionCapacity)
	{
		allocation->freeRegionCapacity *= 2;
		allocation->freeRegions = (FNA3D_MemoryFreeRegion**) SDL_realloc(
			allocation->freeRegions,
			sizeof(FNA3D_MemoryFreeRegion*) * allocation->freeRegionCapacity
		);
	}

	newFreeRegion = (FNA3D_MemoryFreeRegion*) SDL_malloc(sizeof(FNA3D_MemoryFreeRegion));
	newFreeRegion->offset = offset;
	newFreeRegion->size = size;
	newFreeRegion->parent = allocation;

	allocation->freeSpace += size;

	allocation->freeRegions[allocation->freeRegionCount - 1] = newFreeRegion;
	newFreeRegion->allocationIndex = allocation->freeRegionCount - 1;

	if (allocation->available)
	{
		subAllocator = allocation->parent;
		insertionIndex = 0;

		for (i = 0; i < subAllocator->sortedFreeRegionCount; i += 1)
		{
			if (subAllocator->sortedFreeRegions[i]->size < size)
			{
				/* This is where the new region should go */
				break;
			}

			insertionIndex += 1;
		}

		if (subAllocator->sortedFreeRegionCount + 1 > subAllocator->sortedFreeRegionCapacity)
		{
			subAllocator->sortedFreeRegionCapacity *= 2;
			subAllocator->sortedFreeRegions = SDL_realloc(
				subAllocator->sortedFreeRegions,
				sizeof(FNA3D_MemoryFreeRegion*) * subAllocator->sortedFreeRegionCapacity
			);
		}

		/* Perform insertion sort */
		if (	subAllocator->sortedFreeRegionCount > 0 &&
			insertionIndex != subAllocator->sortedFreeRegionCount	)
		{
			for (i = subAllocator->sortedFreeRegionCount; i > insertionIndex && i > 0; i -= 1)
			{
				subAllocator->sortedFreeRegions[i] = subAllocator->sortedFreeRegions[i - 1];
				subAllocator->sortedFreeRegions[i]->sortedIndex = i;
			}
		}

		subAllocator->sortedFreeRegionCount += 1;
		subAllocator->sortedFreeRegions[insertionIndex] = newFreeRegion;
		newFreeRegion->sortedIndex = insertionIndex;
	}

	SDL_UnlockMutex(allocator->lock);
}

uint8_t FNA3D_INTERNAL_RemoveMemoryUsedRegion(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_MemoryUsedRegion *usedRegion
) {
	uint32_t i;
	uint8_t needsDefrag;
	FNA3D_MemoryAllocation *allocation;

	SDL_LockMutex(allocator->lock);

	allocation = usedRegion->parent;

	for (i = 0; i < allocation->usedRegionCount; i += 1)
	{
		if (allocation->usedRegions[i] == usedRegion)
		{
			/* Plug the hole */
			if (i != allocation->usedRegionCount - 1)
			{
				allocation->usedRegions[i] =
					allocation->usedRegions[allocation->usedRegionCount - 1];
			}

			break;
		}
	}

	allocation->usedSpace -= usedRegion->size;
	allocation->usedRegionCount -= 1;

	FNA3D_INTERNAL_NewMemoryFreeRegion(
		allocator,
		usedRegion->parent,
		usedRegion->offset,
		usedRegion->size
	);

	needsDefrag = !allocation->dedicated;

	SDL_free(usedRegion);

	SDL_UnlockMutex(allocator->lock);

	return needsDefrag;
}

static FNA3D_MemoryUsedRegion* FNA3D_INTERNAL_NewMemoryUsedRegion(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_MemoryAllocation *allocation,
	size_t offset,
	size_t size,
	size_t resourceOffset,
	size_t resourceSize,
	size_t alignment
) {
	FNA3D_MemoryUsedRegion *memoryUsedRegion;

	SDL_LockMutex(allocator->lock);

	if (allocation->usedRegionCount == allocation->usedRegionCapacity)
	{
		allocation->usedRegionCapacity *= 2;
		allocation->usedRegions = (FNA3D_MemoryUsedRegion**) SDL_realloc(
			allocation->usedRegions,
			allocation->usedRegionCapacity * sizeof(FNA3D_MemoryUsedRegion*)
		);
	}

	memoryUsedRegion = (FNA3D_MemoryUsedRegion*) SDL_malloc(sizeof(FNA3D_MemoryUsedRegion));
	memoryUsedRegion->parent = allocation;
	memoryUsedRegion->offset = offset;
	memoryUsedRegion->size = size;
	memoryUsedRegion->resourceOffset = resourceOffset;
	memoryUsedRegion->resourceSize = resourceSize;
	memoryUsedRegion->alignment = alignment;

	allocation->usedSpace += size;

	allocation->usedRegions[allocation->usedRegionCount] = memoryUsedRegion;
	allocation->usedRegionCount += 1;

	SDL_UnlockMutex(allocator->lock);

	return memoryUsedRegion;
}

static FNA3D_MemoryAllocation* FNA3D_INTERNAL_Allocate(
	FNA3D_MemoryAllocator *allocator,
	size_t subAllocatorIndex,
	size_t memorySize,
	uint8_t dedicated,
	uint8_t deviceLocal,
	uint8_t hostVisible
) {
	FNA3D_SubAllocator *subAllocator = &allocator->subAllocators[subAllocatorIndex];
	FNA3D_MemoryAllocation *allocation;
	uint8_t result;

	allocation = (FNA3D_MemoryAllocation*) SDL_malloc(sizeof(FNA3D_MemoryAllocation));
	allocation->size = memorySize;
	allocation->freeSpace = 0; /* added by FreeRegions */
	allocation->usedSpace = 0; /* added by UsedRegions */
	allocation->mapLock = SDL_CreateMutex();

	subAllocator->allocationCount += 1;
	subAllocator->allocations = (FNA3D_MemoryAllocation**) SDL_realloc(
		subAllocator->allocations,
		sizeof(FNA3D_MemoryAllocation*) * subAllocator->allocationCount
	);

	subAllocator->allocations[subAllocator->allocationCount - 1] = allocation;

	if (dedicated)
	{
		allocation->dedicated = 1;
		allocation->available = 0;
	}
	else
	{
		allocation->dedicated = 0;
		allocation->available = 1;
	}

	allocation->usedRegions = (FNA3D_MemoryUsedRegion**) SDL_malloc(sizeof(FNA3D_MemoryUsedRegion*));
	allocation->usedRegionCount = 0;
	allocation->usedRegionCapacity = 1;

	allocation->freeRegions = (FNA3D_MemoryFreeRegion**) SDL_malloc(sizeof(FNA3D_MemoryFreeRegion*));
	allocation->freeRegionCount = 0;
	allocation->freeRegionCapacity = 1;

	allocation->parent = subAllocator;

	result = allocator->driver.AllocDeviceMemory(
		allocator->driver.driverData,
		subAllocatorIndex,
		memorySize,
		deviceLocal,
		hostVisible,
		(FNA3D_MemoryPlatformHandle*) &allocation->handle,
		&allocation->mapPointer
	);

	if (!result)
	{
		/* Uh oh, we couldn't allocate, time to clean up */
		SDL_free(allocation->freeRegions);

		subAllocator->allocationCount -= 1;
		subAllocator->allocations = (FNA3D_MemoryAllocation**) SDL_realloc(
			subAllocator->allocations,
			sizeof(FNA3D_MemoryAllocation*) * subAllocator->allocationCount
		);

		SDL_free(allocation);
		return NULL;
	}

	FNA3D_INTERNAL_NewMemoryFreeRegion(
		allocator,
		allocation,
		0,
		allocation->size
	);

	return allocation;
}

static void FNA3D_INTERNAL_Deallocate(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_SubAllocator *subAllocator,
	size_t allocationIndex
) {
	size_t i;
	FNA3D_MemoryAllocation *allocation;

	SDL_LockMutex(allocator->lock);

	allocation = subAllocator->allocations[allocationIndex];

	for (i = 0; i < allocation->freeRegionCount; i += 1)
	{
		FNA3D_INTERNAL_RemoveMemoryFreeRegion(
			allocator,
			allocation->freeRegions[i]
		);
	}
	SDL_free(allocation->freeRegions);

	/* No need to iterate used regions because deallocate
	 * only happens when there are 0 used regions
	 */
	SDL_free(allocation->usedRegions);

	allocator->driver.FreeDeviceMemory(
		allocator->driver.driverData,
		allocation->handle,
		allocation->size,
		subAllocator->index
	);

	SDL_DestroyMutex(allocation->mapLock);
	SDL_free(allocation);

	if (allocationIndex != subAllocator->allocationCount - 1)
	{
		subAllocator->allocations[allocationIndex] = subAllocator->allocations[
			subAllocator->allocationCount - 1
		];
	}

	subAllocator->allocationCount -= 1;

	SDL_UnlockMutex(allocator->lock);
}

FNA3D_MemoryAllocator* FNA3D_CreateMemoryAllocator(
	FNA3D_MemoryDriver *driver,
	size_t numSubAllocators
) {
	size_t i;
	FNA3D_MemoryAllocator *result;
	FNA3D_SubAllocator *subAllocator;

	result = (FNA3D_MemoryAllocator*) SDL_malloc(
		sizeof(FNA3D_MemoryAllocator)
	);
	SDL_memcpy(&result->driver, driver, sizeof(FNA3D_MemoryDriver));
	result->subAllocators = (FNA3D_SubAllocator*) SDL_malloc(
		sizeof(FNA3D_SubAllocator) * numSubAllocators
	);
	result->subAllocatorCount = numSubAllocators;

	for (i = 0; i < result->subAllocatorCount; i += 1)
	{
		subAllocator = &result->subAllocators[i];

		subAllocator->index = i;
		subAllocator->nextAllocationSize = STARTING_ALLOCATION_SIZE;
		subAllocator->allocations = NULL;
		subAllocator->allocationCount = 0;

		subAllocator->sortedFreeRegions = (FNA3D_MemoryFreeRegion**) SDL_malloc(
			sizeof(FNA3D_MemoryFreeRegion*) * 4
		);
		subAllocator->sortedFreeRegionCount = 0;
		subAllocator->sortedFreeRegionCapacity = 4;
	}

	result->usedRegionsToDestroyCapacity = 16;
	result->usedRegionsToDestroyCount = 0;
	result->usedRegionsToDestroy = (FNA3D_MemoryUsedRegion**) SDL_malloc(
		sizeof(FNA3D_MemoryUsedRegion*) *
		result->usedRegionsToDestroyCapacity
	);

	result->lock = SDL_CreateMutex();

	return result;
}

void FNA3D_DestroyMemoryAllocator(FNA3D_MemoryAllocator *allocator)
{
	size_t i;
	int32_t j, k;
	FNA3D_SubAllocator *subAllocator;

	for (i = 0; i < allocator->subAllocatorCount; i += 1)
	{
		subAllocator = &allocator->subAllocators[i];

		for (j = subAllocator->allocationCount - 1; j >= 0; j -= 1)
		{
			for (k = subAllocator->allocations[j]->usedRegionCount - 1; k >= 0; k -= 1)
			{
				FNA3D_INTERNAL_RemoveMemoryUsedRegion(
					allocator,
					subAllocator->allocations[j]->usedRegions[k]
				);
			}

			FNA3D_INTERNAL_Deallocate(
				allocator,
				subAllocator,
				j
			);
		}

		if (subAllocator->allocations != NULL)
		{
			SDL_free(subAllocator->allocations);
		}
		SDL_free(subAllocator->sortedFreeRegions);
	}

	SDL_free(allocator->usedRegionsToDestroy);

	SDL_DestroyMutex(allocator->lock);
	SDL_free(allocator->subAllocators);
	SDL_free(allocator);
}

void FNA3D_Memory_LockAllocator(FNA3D_MemoryAllocator *allocator)
{
	SDL_LockMutex(allocator->lock);
}

void FNA3D_Memory_UnlockAllocator(FNA3D_MemoryAllocator *allocator)
{
	SDL_UnlockMutex(allocator->lock);
}

static inline size_t FNA3D_INTERNAL_NextHighestAlignment(
	size_t n,
	size_t align
) {
	return align * ((n + align - 1) / align);
}

uint8_t FNA3D_Memory_BindResource(
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
	void *defragResourceHandle,
	FNA3D_MemoryUsedRegion** pMemoryUsedRegion
) {
	FNA3D_MemoryAllocation *allocation;
	FNA3D_SubAllocator *subAllocator;
	FNA3D_MemoryFreeRegion *region;
	FNA3D_MemoryUsedRegion *usedRegion;

	size_t allocationSize;
	size_t alignedOffset;
	uint32_t newRegionSize, newRegionOffset;
	uint8_t bindResult;

	subAllocator = &allocator->subAllocators[subAllocatorIndex];

	SDL_assert(resource != 0);

	SDL_LockMutex(allocator->lock);

	/* find the largest free region and use it */
	if (!shouldAllocDedicated && subAllocator->sortedFreeRegionCount > 0)
	{
		region = subAllocator->sortedFreeRegions[0];
		allocation = region->parent;

		alignedOffset = FNA3D_INTERNAL_NextHighestAlignment(
			region->offset,
			requiredAlignment
		);

		if (alignedOffset + requiredSize <= region->offset + region->size)
		{
			usedRegion = FNA3D_INTERNAL_NewMemoryUsedRegion(
				allocator,
				allocation,
				region->offset,
				requiredSize + (alignedOffset - region->offset),
				alignedOffset,
				resourceSize,
				requiredAlignment
			);

			usedRegion->isBuffer = !resourceIsImage;
			usedRegion->defragResource = defragResourceHandle;

			newRegionSize = region->size - ((alignedOffset - region->offset) + requiredSize);
			newRegionOffset = alignedOffset + requiredSize;

			/* remove and add modified region to re-sort */
			FNA3D_INTERNAL_RemoveMemoryFreeRegion(
				allocator,
				region
			);

			/* if size is 0, no need to re-insert */
			if (newRegionSize != 0)
			{
				FNA3D_INTERNAL_NewMemoryFreeRegion(
					allocator,
					allocation,
					newRegionOffset,
					newRegionSize
				);
			}

			SDL_UnlockMutex(allocator->lock);

			SDL_LockMutex(usedRegion->parent->mapLock);
			if (!resourceIsImage)
			{
				bindResult = allocator->driver.BindBufferMemory(
					allocator->driver.driverData,
					usedRegion->parent->handle,
					alignedOffset,
					resource
				);
			}
			else
			{
				bindResult = allocator->driver.BindImageMemory(
					allocator->driver.driverData,
					usedRegion->parent->handle,
					alignedOffset,
					resource
				);
			}
			SDL_UnlockMutex(usedRegion->parent->mapLock);
			if (!bindResult)
			{
				FNA3D_INTERNAL_RemoveMemoryUsedRegion(
					allocator,
					usedRegion
				);

				return 0;
			}

			*pMemoryUsedRegion = usedRegion;
			return 1;
		}
	}

	/* No suitable free regions exist, allocate a new memory region */

	if (shouldAllocDedicated)
	{
		allocationSize = requiredSize;
	}
	else if (requiredSize > subAllocator->nextAllocationSize)
	{
		/* allocate a page of required size aligned to ALLOCATION_INCREMENT increments */
		allocationSize =
			FNA3D_INTERNAL_NextHighestAlignment(requiredSize, ALLOCATION_INCREMENT);
	}
	else
	{
		allocationSize = subAllocator->nextAllocationSize;
	}

	allocation = FNA3D_INTERNAL_Allocate(
		allocator,
		subAllocatorIndex,
		allocationSize,
		shouldAllocDedicated,
		isDeviceLocal,
		isHostVisible
	);

	/* Uh oh, we're out of memory */
	if (allocation == NULL)
	{
		SDL_UnlockMutex(allocator->lock);

		/* Responsibility of the caller to handle being out of memory */
		FNA3D_LogWarn("Failed to allocate memory!");
		return 2;
	}

	usedRegion = FNA3D_INTERNAL_NewMemoryUsedRegion(
		allocator,
		allocation,
		0,
		requiredSize,
		0,
		resourceSize,
		requiredAlignment
	);

	usedRegion->isBuffer = !resourceIsImage;
	usedRegion->defragResource = defragResourceHandle;

	region = allocation->freeRegions[0];

	newRegionOffset = region->offset + requiredSize;
	newRegionSize = region->size - requiredSize;

	FNA3D_INTERNAL_RemoveMemoryFreeRegion(
		allocator,
		region
	);

	if (newRegionSize != 0)
	{
		FNA3D_INTERNAL_NewMemoryFreeRegion(
			allocator,
			allocation,
			newRegionOffset,
			newRegionSize
		);
	}

	SDL_UnlockMutex(allocator->lock);

	SDL_LockMutex(usedRegion->parent->mapLock);
	if (!resourceIsImage)
	{
		bindResult = allocator->driver.BindBufferMemory(
			allocator->driver.driverData,
			usedRegion->parent->handle,
			0,
			resource
		);
	}
	else
	{
		bindResult = allocator->driver.BindImageMemory(
			allocator->driver.driverData,
			usedRegion->parent->handle,
			0,
			resource
		);
	}
	SDL_UnlockMutex(usedRegion->parent->mapLock);
	if (!bindResult)
	{
		FNA3D_INTERNAL_RemoveMemoryUsedRegion(
			allocator,
			usedRegion
		);

		return 0;
	}

	*pMemoryUsedRegion = usedRegion;
	return 1;
}

uint8_t* FNA3D_Memory_GetHostPointer(
	FNA3D_MemoryUsedRegion *usedRegion,
	size_t offset
) {
	return (
		usedRegion->parent->mapPointer +
		usedRegion->resourceOffset +
		offset
	);
}

void FNA3D_Memory_FreeEmptyAllocations(FNA3D_MemoryAllocator *allocator)
{
	FNA3D_SubAllocator *subAllocator;
	size_t i;
	int32_t j;

	SDL_LockMutex(allocator->lock);

	for (i = 0; i < allocator->subAllocatorCount; i += 1)
	{
		subAllocator = &allocator->subAllocators[i];

		for (j = subAllocator->allocationCount - 1; j >= 0; j -= 1)
		{
			if (subAllocator->allocations[j]->usedRegionCount == 0)
			{
				FNA3D_INTERNAL_Deallocate(
					allocator,
					subAllocator,
					j
				);
			}
		}
	}

	SDL_UnlockMutex(allocator->lock);
}

static uint8_t FNA3D_INTERNAL_FindAllocationToDefragment(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_SubAllocator **subAllocator,
	size_t *allocationIndexToDefrag
) {
	size_t i, j;
	FNA3D_SubAllocator *curAllocator;

	for (i = 0; i < allocator->subAllocatorCount; i += 1)
	{
		curAllocator = &allocator->subAllocators[i];

		for (j = 0; j < curAllocator->allocationCount; j += 1)
		{
			if (curAllocator->allocations[j]->freeRegionCount > 1)
			{
				*subAllocator = curAllocator;
				*allocationIndexToDefrag = j;
				return 1;
			}
		}
	}

	return 0;
}

uint8_t FNA3D_Memory_Defragment(FNA3D_MemoryAllocator *allocator)
{
	FNA3D_SubAllocator *subAllocator;
	FNA3D_MemoryAllocation *allocation;
	size_t allocationIndexToDefrag;
	FNA3D_MemoryFreeRegion *freeRegion;
	FNA3D_MemoryUsedRegion *currentRegion;
	uint8_t defragResult;
	size_t i, j;

	SDL_LockMutex(allocator->lock);

	allocator->driver.BeginDefragCommands(allocator->driver.driverData);

	if (FNA3D_INTERNAL_FindAllocationToDefragment(
		allocator,
		&subAllocator,
		&allocationIndexToDefrag
	)) {

		allocation = subAllocator->allocations[allocationIndexToDefrag];

		/* Make the allocation temporarily unavailable */
		allocation->available = 0;
		for (i = 0; i < allocation->freeRegionCount; i += 1)
		{
			freeRegion = allocation->freeRegions[i];

			/* close the gap in the sorted list */
			if (subAllocator->sortedFreeRegionCount > 1)
			{
				for (j = freeRegion->sortedIndex; j < subAllocator->sortedFreeRegionCount - 1; j += 1)
				{
					subAllocator->sortedFreeRegions[j] =
						subAllocator->sortedFreeRegions[j + 1];

					subAllocator->sortedFreeRegions[j]->sortedIndex = j;
				}
			}

			subAllocator->sortedFreeRegionCount -= 1;
		}

		for (i = 0; i < allocation->usedRegionCount; i += 1)
		{
			currentRegion = allocation->usedRegions[i];

			if (currentRegion->isBuffer)
			{
				defragResult = allocator->driver.DefragBuffer(
					allocator->driver.driverData,
					currentRegion->defragResource,
					currentRegion->resourceSize
				);
			}
			else
			{
				defragResult = allocator->driver.DefragImage(
					allocator->driver.driverData,
					currentRegion->defragResource,
					currentRegion->resourceSize
				);
			}

			if (!defragResult)
			{
				return 0;
			}

			if (allocator->usedRegionsToDestroyCount >= allocator->usedRegionsToDestroyCapacity)
			{
				allocator->usedRegionsToDestroyCapacity *= 2;
				allocator->usedRegionsToDestroy = (FNA3D_MemoryUsedRegion**) SDL_realloc(
					allocator->usedRegionsToDestroy,
					sizeof(FNA3D_MemoryUsedRegion*) * allocator->usedRegionsToDestroyCapacity
				);
			}

			allocator->usedRegionsToDestroy[
				allocator->usedRegionsToDestroyCount
			] = currentRegion;

			allocator->usedRegionsToDestroyCount += 1;
		}
	}

	allocator->driver.EndDefragCommands(allocator->driver.driverData);

	SDL_UnlockMutex(allocator->lock);

	return 1;
}

uint8_t FNA3D_Memory_DestroyDefragmentedRegions(FNA3D_MemoryAllocator *allocator)
{
	uint8_t needsDefrag = 0;
	size_t i;
	for (i = 0; i < allocator->usedRegionsToDestroyCount; i += 1)
	{
		needsDefrag |= FNA3D_INTERNAL_RemoveMemoryUsedRegion(
			allocator,
			allocator->usedRegionsToDestroy[i]
		);
	}

	allocator->usedRegionsToDestroyCount = 0;
	return needsDefrag;
}

/*
 * Buffer Containers
 */

struct FNA3D_BufferContainer
{
	size_t sizeInBytes;
	FNA3D_BufferHandle *activeBuffer;

	/* These are all the buffers that have been used by this container.
	 * If a buffer is bound and then updated with Discard, a new buffer
	 * will be added to this list.
	 * These can be reused after they are submitted and command processing is complete.
	 */
	size_t bufferCapacity;
	size_t bufferCount;
	FNA3D_BufferHandle **referencedBuffers;
};

FNA3D_BufferContainer* FNA3D_Memory_CreateBufferContainer(
	FNA3D_MemoryAllocator *allocator,
	uint8_t isVertexData,
	size_t sizeInBytes
) {
	FNA3D_BufferContainer *result = (FNA3D_BufferContainer*) SDL_malloc(
		sizeof(FNA3D_BufferContainer)
	);

	result->sizeInBytes = sizeInBytes;
	result->bufferCapacity = 1;
	result->bufferCount = 1;
	result->activeBuffer = allocator->driver.CreateBufferHandle(
		allocator->driver.driverData,
		isVertexData,
		sizeInBytes
	);
	result->referencedBuffers = SDL_malloc(sizeof(FNA3D_BufferHandle*));
	result->referencedBuffers[0] = result->activeBuffer;

	return result;
}

void FNA3D_Memory_DestroyBufferContainer(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_BufferContainer *container
) {
	allocator->driver.MarkBufferHandlesForDestroy(
		allocator->driver.driverData,
		container->referencedBuffers,
		container->bufferCount
	);

	SDL_free(container->referencedBuffers);
	SDL_free(container);
}

FNA3D_BufferHandle* FNA3D_Memory_GetActiveBuffer(
	FNA3D_BufferContainer *container
) {
	return container->activeBuffer;
}

FNA3D_BufferHandle* FNA3D_Memory_DiscardActiveBuffer(
	FNA3D_MemoryAllocator *allocator,
	FNA3D_BufferContainer *container
) {
	size_t i;

	/* If a previously-discarded buffer is available, we can use that. */
	for (i = 0; i < container->bufferCount; i += 1)
	{
		if (!allocator->driver.BufferHandleInUse(
			allocator->driver.driverData,
			container->referencedBuffers[i]
		)) {
			container->activeBuffer = container->referencedBuffers[i];
			break;
		}
	}

	/* If no buffer is available, generate a new one. */
	if (i == container->bufferCount)
	{
		container->activeBuffer = allocator->driver.CloneBufferHandle(
			allocator->driver.driverData,
			container->activeBuffer
		);

		if (container->bufferCount >= container->bufferCapacity)
		{
			container->bufferCapacity += 1;
			container->referencedBuffers = SDL_realloc(
				container->referencedBuffers,
				container->bufferCapacity * sizeof(FNA3D_BufferHandle*)
			);
		}

		container->referencedBuffers[container->bufferCount] =
			container->activeBuffer;
		container->bufferCount += 1;
	}

	return container->activeBuffer;
}
