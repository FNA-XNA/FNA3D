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

#include "FNA3D_Memory.h"

void FNA3D_Memory_MakeMemoryUnavailable(
	FNA3D_Memory_MemoryAllocation *allocation
) {
	uint32_t i, j;
	FNA3D_Memory_MemoryFreeRegion *freeRegion;

	allocation->availableForAllocation = 0;

	for (i = 0; i < allocation->freeRegionCount; i += 1)
	{
		freeRegion = allocation->freeRegions[i];

		/* close the gap in the sorted list */
		if (allocation->allocator->sortedFreeRegionCount > 1)
		{
			for (j = freeRegion->sortedIndex; j < allocation->allocator->sortedFreeRegionCount - 1; j += 1)
			{
				allocation->allocator->sortedFreeRegions[j] =
					allocation->allocator->sortedFreeRegions[j + 1];

				allocation->allocator->sortedFreeRegions[j]->sortedIndex = j;
			}
		}

		allocation->allocator->sortedFreeRegionCount -= 1;
	}
}

void FNA3D_Memory_RemoveMemoryFreeRegion(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemoryFreeRegion *freeRegion
) {
	uint32_t i;

	SDL_LockMutex(context->allocatorLock);

	if (freeRegion->allocation->availableForAllocation)
	{
		/* close the gap in the sorted list */
		if (freeRegion->allocation->allocator->sortedFreeRegionCount > 1)
		{
			for (i = freeRegion->sortedIndex; i < freeRegion->allocation->allocator->sortedFreeRegionCount - 1; i += 1)
			{
				freeRegion->allocation->allocator->sortedFreeRegions[i] =
					freeRegion->allocation->allocator->sortedFreeRegions[i + 1];

				freeRegion->allocation->allocator->sortedFreeRegions[i]->sortedIndex = i;
			}
		}

		freeRegion->allocation->allocator->sortedFreeRegionCount -= 1;
	}

	/* close the gap in the buffer list */
	if (freeRegion->allocation->freeRegionCount > 1 && freeRegion->allocationIndex != freeRegion->allocation->freeRegionCount - 1)
	{
		freeRegion->allocation->freeRegions[freeRegion->allocationIndex] =
			freeRegion->allocation->freeRegions[freeRegion->allocation->freeRegionCount - 1];

		freeRegion->allocation->freeRegions[freeRegion->allocationIndex]->allocationIndex =
			freeRegion->allocationIndex;
	}

	freeRegion->allocation->freeRegionCount -= 1;

	freeRegion->allocation->freeSpace -= freeRegion->size;

	SDL_free(freeRegion);

	SDL_UnlockMutex(context->allocatorLock);
}

void FNA3D_Memory_NewMemoryFreeRegion(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemoryAllocation *allocation,
	FNA3D_Memory_MemorySize offset,
	FNA3D_Memory_MemorySize size
) {
	FNA3D_Memory_MemoryFreeRegion *newFreeRegion;
	FNA3D_Memory_MemorySize newOffset, newSize;
	int32_t insertionIndex = 0;
	int32_t i;

	SDL_LockMutex(context->allocatorLock);

	/* look for an adjacent region to merge */
	for (i = allocation->freeRegionCount - 1; i >= 0; i -= 1)
	{
		/* check left side */
		if (allocation->freeRegions[i]->offset + allocation->freeRegions[i]->size == offset)
		{
			newOffset = allocation->freeRegions[i]->offset;
			newSize = allocation->freeRegions[i]->size + size;

			FNA3D_Memory_RemoveMemoryFreeRegion(context, allocation->freeRegions[i]);
			FNA3D_Memory_NewMemoryFreeRegion(context, allocation, newOffset, newSize);

			SDL_UnlockMutex(context->allocatorLock);
			return;
		}

		/* check right side */
		if (allocation->freeRegions[i]->offset == offset + size)
		{
			newOffset = offset;
			newSize = allocation->freeRegions[i]->size + size;

			FNA3D_Memory_RemoveMemoryFreeRegion(context, allocation->freeRegions[i]);
			FNA3D_Memory_NewMemoryFreeRegion(context, allocation, newOffset, newSize);

			SDL_UnlockMutex(context->allocatorLock);
			return;
		}
	}

	/* region is not contiguous with another free region, make a new one */
	allocation->freeRegionCount += 1;
	if (allocation->freeRegionCount > allocation->freeRegionCapacity)
	{
		allocation->freeRegionCapacity *= 2;
		allocation->freeRegions = SDL_realloc(
			allocation->freeRegions,
			sizeof(FNA3D_Memory_MemoryFreeRegion*) * allocation->freeRegionCapacity
		);
	}

	newFreeRegion = SDL_malloc(sizeof(FNA3D_Memory_MemoryFreeRegion));
	newFreeRegion->offset = offset;
	newFreeRegion->size = size;
	newFreeRegion->allocation = allocation;

	allocation->freeSpace += size;

	allocation->freeRegions[allocation->freeRegionCount - 1] = newFreeRegion;
	newFreeRegion->allocationIndex = allocation->freeRegionCount - 1;

	if (allocation->availableForAllocation)
	{
		for (i = 0; i < allocation->allocator->sortedFreeRegionCount; i += 1)
		{
			if (allocation->allocator->sortedFreeRegions[i]->size < size)
			{
				/* this is where the new region should go */
				break;
			}

			insertionIndex += 1;
		}

		if (allocation->allocator->sortedFreeRegionCount + 1 > allocation->allocator->sortedFreeRegionCapacity)
		{
			allocation->allocator->sortedFreeRegionCapacity *= 2;
			allocation->allocator->sortedFreeRegions = SDL_realloc(
				allocation->allocator->sortedFreeRegions,
				sizeof(FNA3D_Memory_MemoryFreeRegion*) * allocation->allocator->sortedFreeRegionCapacity
			);
		}

		/* perform insertion sort */
		if (allocation->allocator->sortedFreeRegionCount > 0 && insertionIndex != allocation->allocator->sortedFreeRegionCount)
		{
			for (i = allocation->allocator->sortedFreeRegionCount; i > insertionIndex && i > 0; i -= 1)
			{
				allocation->allocator->sortedFreeRegions[i] = allocation->allocator->sortedFreeRegions[i - 1];
				allocation->allocator->sortedFreeRegions[i]->sortedIndex = i;
			}
		}

		allocation->allocator->sortedFreeRegionCount += 1;
		allocation->allocator->sortedFreeRegions[insertionIndex] = newFreeRegion;
		newFreeRegion->sortedIndex = insertionIndex;
	}

	SDL_UnlockMutex(context->allocatorLock);
}

FNA3D_Memory_MemoryUsedRegion* FNA3D_Memory_NewMemoryUsedRegion(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemoryAllocation *allocation,
	FNA3D_Memory_MemorySize offset,
	FNA3D_Memory_MemorySize size,
	FNA3D_Memory_MemorySize resourceOffset,
	FNA3D_Memory_MemorySize resourceSize,
	FNA3D_Memory_MemorySize alignment
) {
	FNA3D_Memory_MemoryUsedRegion *memoryUsedRegion;

	SDL_LockMutex(context->allocatorLock);

	if (allocation->usedRegionCount == allocation->usedRegionCapacity)
	{
		allocation->usedRegionCapacity *= 2;
		allocation->usedRegions = SDL_realloc(
			allocation->usedRegions,
			allocation->usedRegionCapacity * sizeof(FNA3D_Memory_MemoryUsedRegion*)
		);
	}

	memoryUsedRegion = SDL_malloc(sizeof(FNA3D_Memory_MemoryUsedRegion));
	memoryUsedRegion->allocation = allocation;
	memoryUsedRegion->offset = offset;
	memoryUsedRegion->size = size;
	memoryUsedRegion->resourceOffset = resourceOffset;
	memoryUsedRegion->resourceSize = resourceSize;
	memoryUsedRegion->alignment = alignment;

	allocation->usedSpace += size;

	allocation->usedRegions[allocation->usedRegionCount] = memoryUsedRegion;
	allocation->usedRegionCount += 1;

	SDL_UnlockMutex(context->allocatorLock);

	return memoryUsedRegion;
}

void FNA3D_Memory_RemoveMemoryUsedRegion(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemoryUsedRegion *usedRegion
) {
	uint32_t i;

	SDL_LockMutex(context->allocatorLock);

	for (i = 0; i < usedRegion->allocation->usedRegionCount; i += 1)
	{
		if (usedRegion->allocation->usedRegions[i] == usedRegion)
		{
			/* plug the hole */
			if (i != usedRegion->allocation->usedRegionCount - 1)
			{
				usedRegion->allocation->usedRegions[i] = usedRegion->allocation->usedRegions[
					usedRegion->allocation->usedRegionCount - 1
				];
			}

			break;
		}
	}

	usedRegion->allocation->usedSpace -= usedRegion->size;

	usedRegion->allocation->usedRegionCount -= 1;

	FNA3D_Memory_NewMemoryFreeRegion(
		context,
		usedRegion->allocation,
		usedRegion->offset,
		usedRegion->size
	);

	if (!usedRegion->allocation->dedicated)
	{
		context->needDefrag = 1;
	}

	SDL_free(usedRegion);

	context->resourceFreed = 1;
	SDL_UnlockMutex(context->allocatorLock);
}

void FNA3D_Memory_DeallocateMemory(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemorySubAllocator *allocator,
	uint32_t allocationIndex
) {
	uint32_t i;

	FNA3D_Memory_MemoryAllocation *allocation = allocator->allocations[allocationIndex];

	SDL_LockMutex(context->allocatorLock);

	for (i = 0; i < allocation->freeRegionCount; i += 1)
	{
		FNA3D_Memory_RemoveMemoryFreeRegion(
			context,
			allocation->freeRegions[i]
		);
	}
	SDL_free(allocation->freeRegions);

	/* no need to iterate used regions because deallocate
	 * only happens when there are 0 used regions
	 */
	SDL_free(allocation->usedRegions);

	context->freeMemoryFunc(context, allocation->memory);

	SDL_DestroyMutex(allocation->mapLock);
	SDL_free(allocation);

	if (allocationIndex != allocator->allocationCount - 1)
	{
		allocator->allocations[allocationIndex] = allocator->allocations[allocator->allocationCount - 1];
	}

	allocator->allocationCount -= 1;

	SDL_UnlockMutex(context->allocatorLock);
}

uint8_t FNA3D_Memory_AllocateMemory(
	FNA3D_Memory_Context *context,
	void* userdata,
	uint32_t memoryTypeIndex,
	FNA3D_Memory_MemorySize allocationSize,
	uint8_t dedicated,
	uint8_t isHostVisible,
	FNA3D_Memory_MemoryAllocation **pMemoryAllocation
) {
	FNA3D_Memory_MemoryAllocation *allocation;
	FNA3D_Memory_MemorySubAllocator *allocator = &context->memoryAllocator->subAllocators[memoryTypeIndex];
	int64_t result;

	allocation = SDL_malloc(sizeof(FNA3D_Memory_MemoryAllocation));
	allocation->size = allocationSize;
	allocation->freeSpace = 0; /* added by FreeRegions */
	allocation->usedSpace = 0; /* added by UsedRegions */
	allocation->mapLock = SDL_CreateMutex();

	allocator->allocationCount += 1;
	allocator->allocations = SDL_realloc(
		allocator->allocations,
		sizeof(FNA3D_Memory_MemoryAllocation*) * allocator->allocationCount
	);

	allocator->allocations[
		allocator->allocationCount - 1
	] = allocation;

	if (dedicated)
	{
		allocation->dedicated = 1;
		allocation->availableForAllocation = 0;
	}
	else
	{
		allocation->dedicated = 0;
		allocation->availableForAllocation = 1;
	}

	allocation->usedRegions = SDL_malloc(sizeof(FNA3D_Memory_MemoryUsedRegion*));
	allocation->usedRegionCount = 0;
	allocation->usedRegionCapacity = 1;

	allocation->freeRegions = SDL_malloc(sizeof(FNA3D_Memory_MemoryFreeRegion*));
	allocation->freeRegionCount = 0;
	allocation->freeRegionCapacity = 1;

	allocation->allocator = allocator;

	result = context->allocateMemoryFunc(
		context,
		userdata,
		&allocation->memory
	);

	if (!result)
	{
		/* Uh oh, we couldn't allocate, time to clean up */
		SDL_free(allocation->freeRegions);

		allocator->allocationCount -= 1;
		allocator->allocations = SDL_realloc(
			allocator->allocations,
			sizeof(FNA3D_Memory_MemoryAllocation*) * allocator->allocationCount
		);

		SDL_free(allocation);

		return 0;
	}

	/* persistent mapping for host memory */
	if (isHostVisible)
	{
		result = context->mapMemoryFunc(
			context,
			allocation
		);

		if (!result)
		{
			/* FIXME: How should we clean up here? */
			return 0;
		}
	}
	else
	{
		allocation->mapPointer = NULL;
	}

	FNA3D_Memory_NewMemoryFreeRegion(
		context,
		allocation,
		0,
		allocation->size
	);

	*pMemoryAllocation = allocation;
	return 1;
}

uint8_t FNA3D_Memory_FindAllocationToDefragment(
	FNA3D_Memory_Context *context,
	FNA3D_Memory_MemorySubAllocator *allocator,
	uint32_t *allocationIndexToDefrag
) {
	uint32_t i, j;

	for (i = 0; i < context->memoryAllocator->numSubAllocators; i += 1)
	{
		*allocator = context->memoryAllocator->subAllocators[i];

		for (j = 0; j < allocator->allocationCount; j += 1)
		{
			if (allocator->allocations[j]->freeRegionCount > 1)
			{
				*allocationIndexToDefrag = j;
				return 1;
			}
		}
	}

	return 0;
}
