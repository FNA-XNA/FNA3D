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

#include "FNA3D_CommandBuffer.h"

#include <SDL.h>

#define STARTING_TRANSFER_BUFFER_SIZE 8000000 /* 8MB */
#define FAST_TRANSFER_SIZE 64000000 /* 64MB */

typedef struct FNA3D_TransferBufferPool
{
	FNA3D_TransferBuffer *fastTransferBuffer;
	uint8_t fastTransferBufferAvailable;

	FNA3D_TransferBuffer **availableSlowTransferBuffers;
	uint32_t availableSlowTransferBufferCount;
	uint32_t availableSlowTransferBufferCapacity;
} FNA3D_TransferBufferPool;

/* Command buffers have various resources associated with them
 * that can be freed after the command buffer is fully processed.
 */
typedef struct FNA3D_CommandBufferContainer
{
	FNA3D_CommandBuffer *handle;

	FNA3D_TransferBuffer **transferBuffers;
	uint32_t transferBufferCount;
	uint32_t transferBufferCapacity;

	FNA3D_BufferHandle **boundBuffers;
	uint32_t boundBufferCount;
	uint32_t boundBufferCapacity;

	FNA3D_Renderbuffer **renderbuffersToDestroy;
	uint32_t renderbuffersToDestroyCount;
	uint32_t renderbuffersToDestroyCapacity;

	FNA3D_BufferHandle **buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	FNA3D_Effect **effectsToDestroy;
	uint32_t effectsToDestroyCount;
	uint32_t effectsToDestroyCapacity;

	FNA3D_Texture **texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;
} FNA3D_CommandBufferContainer;

struct FNA3D_CommandBufferManager
{
	FNA3D_CommandBufferDriver driver;

	FNA3D_CommandBufferContainer **inactiveCommandBufferContainers;
	uint32_t inactiveCommandBufferContainerCount;
	uint32_t inactiveCommandBufferContainerCapacity;

	FNA3D_CommandBufferContainer **submittedCommandBufferContainers;
	uint32_t submittedCommandBufferContainerCount;
	uint32_t submittedCommandBufferContainerCapacity;

	uint32_t currentCommandCount;
	FNA3D_CommandBufferContainer *currentCommandBufferContainer;

	/* Special command buffer for performing defrag copies */
	FNA3D_CommandBufferContainer *defragCommandBufferContainer;

	FNA3D_TransferBufferPool transferBufferPool;

	SDL_mutex *commandLock;
	SDL_mutex *transferLock;
};

static FNA3D_CommandBufferContainer* FNA3D_INTERNAL_CreateCommandBufferContainer(
	FNA3D_CommandBufferManager *manager,
	uint8_t fenceSignaled
) {
	FNA3D_CommandBufferContainer *result = (FNA3D_CommandBufferContainer*) SDL_malloc(
		sizeof(FNA3D_CommandBufferContainer)
	);

	result->handle = manager->driver.AllocCommandBuffer(
		manager->driver.driverData,
		fenceSignaled
	);

	/* Transfer buffer tracking */

	result->transferBufferCapacity = 0;
	result->transferBufferCount = 0;
	result->transferBuffers = NULL;

	/* Bound buffer tracking */

	result->boundBufferCapacity = 4;
	result->boundBufferCount = 0;
	result->boundBuffers = (FNA3D_BufferHandle**) SDL_malloc(
		result->boundBufferCapacity * sizeof(FNA3D_BufferHandle*)
	);

	/* Destroyed resources tracking */

	result->renderbuffersToDestroyCapacity = 16;
	result->renderbuffersToDestroyCount = 0;

	result->renderbuffersToDestroy = (FNA3D_Renderbuffer**) SDL_malloc(
		sizeof(FNA3D_Renderbuffer*) *
		result->renderbuffersToDestroyCapacity
	);

	result->buffersToDestroyCapacity = 16;
	result->buffersToDestroyCount = 0;

	result->buffersToDestroy = (FNA3D_BufferHandle**) SDL_malloc(
		sizeof(FNA3D_BufferHandle*) *
		result->buffersToDestroyCapacity
	);

	result->effectsToDestroyCapacity = 16;
	result->effectsToDestroyCount = 0;

	result->effectsToDestroy = (FNA3D_Effect**) SDL_malloc(
		sizeof(FNA3D_Effect*) *
		result->effectsToDestroyCapacity
	);

	result->texturesToDestroyCapacity = 16;
	result->texturesToDestroyCount = 0;

	result->texturesToDestroy = (FNA3D_Texture**) SDL_malloc(
		sizeof(FNA3D_Texture*) *
		result->texturesToDestroyCapacity
	);

	return result;
}

static void FNA3D_CommandBuffer_CleanContainer(
	FNA3D_CommandBufferManager *manager,
	FNA3D_CommandBufferContainer *container
) {
	FNA3D_TransferBuffer* transferBuffer;
	uint32_t i;

	/* Reset bound buffers */
	for (i = 0; i < container->boundBufferCount; i += 1)
	{
		if (container->boundBuffers[i] != NULL)
		{
			manager->driver.DecBufferRef(
				manager->driver.driverData,
				container->boundBuffers[i]
			);
		}
	}
	container->boundBufferCount = 0;

	/* Destroy resources marked for destruction */

	for (i = 0; i < container->renderbuffersToDestroyCount; i += 1)
	{
		manager->driver.DestroyRenderbuffer(
			manager->driver.driverData,
			container->renderbuffersToDestroy[i]
		);
	}
	container->renderbuffersToDestroyCount = 0;

	for (i = 0; i < container->buffersToDestroyCount; i += 1)
	{
		manager->driver.DestroyBuffer(
			manager->driver.driverData,
			container->buffersToDestroy[i]
		);
	}
	container->buffersToDestroyCount = 0;

	for (i = 0; i < container->effectsToDestroyCount; i += 1)
	{
		manager->driver.DestroyEffect(
			manager->driver.driverData,
			container->effectsToDestroy[i]
		);
	}
	container->effectsToDestroyCount = 0;

	for (i = 0; i < container->texturesToDestroyCount; i += 1)
	{
		manager->driver.DestroyTexture(
			manager->driver.driverData,
			container->texturesToDestroy[i]
		);
	}
	container->texturesToDestroyCount = 0;

	/* Return the transfer buffers to the pool */
	for (i = 0; i < container->transferBufferCount; i += 1)
	{
		transferBuffer = container->transferBuffers[i];
		transferBuffer->offset = 0;

		if (transferBuffer == manager->transferBufferPool.fastTransferBuffer)
		{
			manager->transferBufferPool.fastTransferBufferAvailable = 1;
		}
		else
		{
			if (manager->transferBufferPool.availableSlowTransferBufferCount == manager->transferBufferPool.availableSlowTransferBufferCapacity)
			{
				manager->transferBufferPool.availableSlowTransferBufferCapacity += 1;
				manager->transferBufferPool.availableSlowTransferBuffers = SDL_realloc(
					manager->transferBufferPool.availableSlowTransferBuffers,
					manager->transferBufferPool.availableSlowTransferBufferCapacity * sizeof(FNA3D_TransferBuffer*)
				);
			}

			manager->transferBufferPool.availableSlowTransferBuffers[manager->transferBufferPool.availableSlowTransferBufferCount] = transferBuffer;
			manager->transferBufferPool.availableSlowTransferBufferCount += 1;
		}
	}
	container->transferBufferCount = 0;

	SDL_LockMutex(manager->commandLock);

	manager->driver.Reset(
		manager->driver.driverData,
		container->handle
	);

	SDL_UnlockMutex(manager->commandLock);

	/* Remove this command buffer from the submitted list */
	for (i = 0; i < manager->submittedCommandBufferContainerCount; i += 1)
	{
		if (manager->submittedCommandBufferContainers[i] == container)
		{
			manager->submittedCommandBufferContainers[i] = manager->submittedCommandBufferContainers[manager->submittedCommandBufferContainerCount - 1];
			manager->submittedCommandBufferContainerCount -= 1;
			break;
		}
	}

	/* Add this command buffer to the inactive list */
	if (manager->inactiveCommandBufferContainerCount + 1 > manager->inactiveCommandBufferContainerCapacity)
	{
		manager->inactiveCommandBufferContainerCapacity = manager->inactiveCommandBufferContainerCount + 1;
		manager->inactiveCommandBufferContainers = SDL_realloc(
			manager->inactiveCommandBufferContainers,
			manager->inactiveCommandBufferContainerCapacity * sizeof(FNA3D_CommandBufferContainer*)
		);
	}

	manager->inactiveCommandBufferContainers[manager->inactiveCommandBufferContainerCount] = container;
	manager->inactiveCommandBufferContainerCount += 1;
}

/* Public Functions */

FNA3D_CommandBufferManager* FNA3D_CreateCommandBufferManager(
	FNA3D_CommandBufferDriver *driver
) {
	FNA3D_CommandBufferManager *manager = (FNA3D_CommandBufferManager*) SDL_malloc(
		sizeof(FNA3D_CommandBufferManager)
	);
	SDL_memcpy(&manager->driver, driver, sizeof(FNA3D_CommandBufferDriver));

	manager->commandLock = SDL_CreateMutex();
	manager->transferLock = SDL_CreateMutex();

	manager->inactiveCommandBufferContainerCapacity = 1;
	manager->inactiveCommandBufferContainers = (FNA3D_CommandBufferContainer**) SDL_malloc(
		sizeof(FNA3D_CommandBufferContainer*)
	);
	manager->inactiveCommandBufferContainerCount = 0;
	manager->currentCommandCount = 0;

	manager->submittedCommandBufferContainerCapacity = 1;
	manager->submittedCommandBufferContainers = (FNA3D_CommandBufferContainer**) SDL_malloc(
		sizeof(FNA3D_CommandBufferContainer*)
	);
	manager->submittedCommandBufferContainerCount = 0;

	manager->defragCommandBufferContainer = FNA3D_INTERNAL_CreateCommandBufferContainer(
		manager,
		1
	);

	/*
	 * Initialize buffer space
	 */

	manager->transferBufferPool.fastTransferBuffer = (FNA3D_TransferBuffer*) SDL_malloc(
		sizeof(FNA3D_TransferBuffer)
	);
	manager->transferBufferPool.fastTransferBuffer->offset = 0;
	manager->transferBufferPool.fastTransferBuffer->buffer = manager->driver.CreateTransferBuffer(
		manager->driver.driverData,
		FAST_TRANSFER_SIZE,
		1
	);
	manager->transferBufferPool.fastTransferBufferAvailable = 1;

	manager->transferBufferPool.availableSlowTransferBufferCapacity = 4;
	manager->transferBufferPool.availableSlowTransferBufferCount = 0;
	manager->transferBufferPool.availableSlowTransferBuffers = (FNA3D_TransferBuffer**) SDL_malloc(
		manager->transferBufferPool.availableSlowTransferBufferCapacity * sizeof(FNA3D_TransferBuffer*)
	);

	return manager;
}

void FNA3D_DestroyCommandBufferManager(
	FNA3D_CommandBufferManager *manager
) {
	FNA3D_CommandBufferContainer *commandBufferContainer;
	uint32_t i;

	if (manager->inactiveCommandBufferContainerCount + 1 > manager->inactiveCommandBufferContainerCapacity)
	{
		manager->inactiveCommandBufferContainerCapacity = manager->inactiveCommandBufferContainerCount + 1;
		manager->inactiveCommandBufferContainers = SDL_realloc(
			manager->inactiveCommandBufferContainers,
			manager->inactiveCommandBufferContainerCapacity * sizeof(FNA3D_CommandBufferContainer*)
		);
	}

	manager->inactiveCommandBufferContainers[manager->inactiveCommandBufferContainerCount] = manager->currentCommandBufferContainer;
	manager->inactiveCommandBufferContainerCount += 1;

	manager->inactiveCommandBufferContainerCapacity += 1;
	manager->inactiveCommandBufferContainers = SDL_realloc(
		manager->inactiveCommandBufferContainers,
		sizeof(FNA3D_CommandBufferContainer*) * manager->inactiveCommandBufferContainerCapacity
	);

	manager->inactiveCommandBufferContainers[manager->inactiveCommandBufferContainerCount] = manager->defragCommandBufferContainer;
	manager->inactiveCommandBufferContainerCount += 1;

	for (i = 0; i < manager->inactiveCommandBufferContainerCount; i += 1)
	{
		commandBufferContainer = manager->inactiveCommandBufferContainers[i];

		manager->driver.FreeCommandBuffer(
			manager->driver.driverData,
			commandBufferContainer->handle
		);

		SDL_free(commandBufferContainer->transferBuffers);
		SDL_free(commandBufferContainer->boundBuffers);

		SDL_free(commandBufferContainer->renderbuffersToDestroy);
		SDL_free(commandBufferContainer->buffersToDestroy);
		SDL_free(commandBufferContainer->effectsToDestroy);
		SDL_free(commandBufferContainer->texturesToDestroy);

		SDL_free(commandBufferContainer);
	}

	manager->driver.DestroyBuffer(
		manager->driver.driverData,
		manager->transferBufferPool.fastTransferBuffer->buffer
	);
	SDL_free(manager->transferBufferPool.fastTransferBuffer);

	for (i = 0; i < manager->transferBufferPool.availableSlowTransferBufferCount; i += 1)
	{
		manager->driver.DestroyBuffer(
			manager->driver.driverData,
			manager->transferBufferPool.availableSlowTransferBuffers[i]->buffer
		);

		SDL_free(manager->transferBufferPool.availableSlowTransferBuffers[i]);
	}
	SDL_free(manager->transferBufferPool.availableSlowTransferBuffers);

	SDL_DestroyMutex(manager->commandLock);
	SDL_DestroyMutex(manager->transferLock);

	SDL_free(manager->inactiveCommandBufferContainers);
	SDL_free(manager->submittedCommandBufferContainers);
	SDL_free(manager);
}

void FNA3D_CommandBuffer_BeginRecording(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->commandLock);

	/* If we are out of unused command buffers, allocate some more */
	if (manager->inactiveCommandBufferContainerCount == 0)
	{
		manager->inactiveCommandBufferContainers[manager->inactiveCommandBufferContainerCount] =
			FNA3D_INTERNAL_CreateCommandBufferContainer(manager, 0);

		manager->inactiveCommandBufferContainerCount += 1;
	}

	manager->currentCommandBufferContainer =
		manager->inactiveCommandBufferContainers[manager->inactiveCommandBufferContainerCount - 1];

	manager->inactiveCommandBufferContainerCount -= 1;

	manager->driver.BeginRecording(
		manager->driver.driverData,
		manager->currentCommandBufferContainer->handle
	);

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_EndRecording(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->commandLock);

	manager->driver.EndRecording(
		manager->driver.driverData,
		manager->currentCommandBufferContainer->handle
	);

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_Finish(FNA3D_CommandBufferManager *manager)
{
	int32_t i;
	FNA3D_CommandBuffer **commandBuffers = SDL_stack_alloc(
		FNA3D_CommandBuffer*,
		manager->submittedCommandBufferContainerCount
	);

	for (i = 0; i < manager->submittedCommandBufferContainerCount; i += 1)
	{
		commandBuffers[i] = manager->submittedCommandBufferContainers[i]->handle;
	}

	manager->driver.WaitForFences(
		manager->driver.driverData,
		commandBuffers,
		manager->submittedCommandBufferContainerCount
	);

	for (i = manager->submittedCommandBufferContainerCount - 1; i >= 0; i -= 1)
	{
		FNA3D_CommandBuffer_CleanContainer(
			manager,
			manager->submittedCommandBufferContainers[i]
		);
	}

	SDL_stack_free(commandBuffers);
}

void FNA3D_CommandBuffer_LockForRendering(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->commandLock);
	if (manager->currentCommandBufferContainer == NULL)
	{
		FNA3D_CommandBuffer_BeginRecording(manager);
	}
}

void FNA3D_CommandBuffer_UnlockFromRendering(FNA3D_CommandBufferManager *manager)
{
	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_LockForDefrag(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->commandLock);

	/* FIXME: This is a kludge! */
	manager->currentCommandBufferContainer = manager->defragCommandBufferContainer;
}

void FNA3D_CommandBuffer_UnlockFromDefrag(FNA3D_CommandBufferManager *manager)
{
	/* FIXME: This is a kludge! */
	manager->currentCommandBufferContainer = NULL;

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_LockForTransfer(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->transferLock);
}

void FNA3D_CommandBuffer_UnlockFromTransfer(FNA3D_CommandBufferManager *manager)
{
	SDL_UnlockMutex(manager->transferLock);
}

void FNA3D_CommandBuffer_LockForSubmit(FNA3D_CommandBufferManager *manager)
{
	SDL_LockMutex(manager->commandLock);
	SDL_LockMutex(manager->transferLock);
}

void FNA3D_CommandBuffer_UnlockFromSubmit(FNA3D_CommandBufferManager *manager)
{
	SDL_UnlockMutex(manager->commandLock);
	SDL_UnlockMutex(manager->transferLock);
}

void FNA3D_CommandBuffer_AddDisposeTexture(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Texture *texture
) {
	FNA3D_CommandBufferContainer *container;

	SDL_LockMutex(manager->commandLock);

	container = manager->currentCommandBufferContainer;

	if (container->texturesToDestroyCount + 1 >= container->texturesToDestroyCapacity)
	{
		container->texturesToDestroyCapacity *= 2;

		container->texturesToDestroy = SDL_realloc(
			container->texturesToDestroy,
			sizeof(FNA3D_Texture*) * container->texturesToDestroyCapacity
		);
	}
	container->texturesToDestroy[container->texturesToDestroyCount] = texture;
	container->texturesToDestroyCount += 1;

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_AddDisposeRenderbuffer(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Renderbuffer *renderbuffer
) {
	FNA3D_CommandBufferContainer *container;

	SDL_LockMutex(manager->commandLock);

	container = manager->currentCommandBufferContainer;

	if (container->renderbuffersToDestroyCount + 1 >= container->renderbuffersToDestroyCapacity)
	{
		container->renderbuffersToDestroyCapacity *= 2;

		container->renderbuffersToDestroy = SDL_realloc(
			container->renderbuffersToDestroy,
			sizeof(FNA3D_Renderbuffer*) * container->renderbuffersToDestroyCapacity
		);
	}
	container->renderbuffersToDestroy[container->renderbuffersToDestroyCount] = renderbuffer;
	container->renderbuffersToDestroyCount += 1;

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_AddDisposeEffect(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Effect *effect
) {
	FNA3D_CommandBufferContainer *container;

	SDL_LockMutex(manager->commandLock);

	container = manager->currentCommandBufferContainer;

	if (container->effectsToDestroyCount + 1 >= container->effectsToDestroyCapacity)
	{
		container->effectsToDestroyCapacity *= 2;

		container->effectsToDestroy = SDL_realloc(
			container->effectsToDestroy,
			sizeof(FNA3D_Effect*) * container->effectsToDestroyCapacity
		);
	}
	container->effectsToDestroy[container->effectsToDestroyCount] = effect;
	container->effectsToDestroyCount += 1;

	SDL_UnlockMutex(manager->commandLock);
}

void FNA3D_CommandBuffer_AddDisposeBuffers(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle **handles,
	size_t handleCount
) {
	FNA3D_CommandBufferContainer *container;
	size_t i;

	/* Queue buffer for destruction */
	SDL_LockMutex(manager->commandLock);
	container = manager->currentCommandBufferContainer;

	/* FIXME: This is a lazy way of adding each buffer, do a memcpy instead */
	for (i = 0; i < handleCount; i += 1)
	{
		if (container->buffersToDestroyCount + 1 >= container->buffersToDestroyCapacity)
		{
			container->buffersToDestroyCapacity *= 2;

			container->buffersToDestroy = SDL_realloc(
				container->buffersToDestroy,
				sizeof(FNA3D_BufferHandle*) * container->buffersToDestroyCapacity
			);
		}

		container->buffersToDestroy[
			container->buffersToDestroyCount
		] = handles[i];
		container->buffersToDestroyCount += 1;
	}
	SDL_UnlockMutex(manager->commandLock);
}

uint8_t FNA3D_CommandBuffer_PerformCleanups(FNA3D_CommandBufferManager *manager)
{
	uint8_t result = 0;
	int32_t i;
	for (i = manager->submittedCommandBufferContainerCount - 1; i >= 0; i -= 1)
	{
		if (manager->driver.QueryFence(
			manager->driver.driverData,
			manager->submittedCommandBufferContainers[i]->handle
		)) {
			FNA3D_CommandBuffer_CleanContainer(
				manager,
				manager->submittedCommandBufferContainers[i]
			);

			result = 1;
		}
	}

	return result;
}

void FNA3D_CommandBuffer_SubmitCurrent(FNA3D_CommandBufferManager *manager)
{
	if (manager->submittedCommandBufferContainerCount >= manager->submittedCommandBufferContainerCapacity)
	{
		manager->submittedCommandBufferContainerCapacity *= 2;
		manager->submittedCommandBufferContainers = SDL_realloc(
			manager->submittedCommandBufferContainers,
			manager->submittedCommandBufferContainerCapacity * sizeof(FNA3D_CommandBufferContainer*)
		);
	}

	manager->submittedCommandBufferContainers[manager->submittedCommandBufferContainerCount] =
		manager->currentCommandBufferContainer;
	manager->submittedCommandBufferContainerCount += 1;
	manager->currentCommandBufferContainer = NULL;
}

FNA3D_CommandBuffer* FNA3D_CommandBuffer_GetCurrent(FNA3D_CommandBufferManager *manager)
{
	return manager->currentCommandBufferContainer->handle;
}

FNA3D_CommandBuffer* FNA3D_CommandBuffer_GetDefragBuffer(FNA3D_CommandBufferManager *manager)
{
	return manager->defragCommandBufferContainer->handle;
}

void FNA3D_CommandBuffer_MarkBufferAsBound(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle *buffer
) {
	uint32_t i;

	for (i = 0; i < manager->currentCommandBufferContainer->boundBufferCount; i += 1)
	{
		if (buffer == manager->currentCommandBufferContainer->boundBuffers[i])
		{
			/* Buffer is already referenced, nothing to do */
			return;
		}
	}

	manager->driver.IncBufferRef(
		manager->driver.driverData,
		buffer
	);

	if (manager->currentCommandBufferContainer->boundBufferCount >= manager->currentCommandBufferContainer->boundBufferCapacity)
	{
		manager->currentCommandBufferContainer->boundBufferCapacity *= 2;
		manager->currentCommandBufferContainer->boundBuffers = SDL_realloc(
			manager->currentCommandBufferContainer->boundBuffers,
			manager->currentCommandBufferContainer->boundBufferCapacity * sizeof(FNA3D_BufferHandle*)
		);
	}

	manager->currentCommandBufferContainer->boundBuffers[manager->currentCommandBufferContainer->boundBufferCount] = buffer;
	manager->currentCommandBufferContainer->boundBufferCount += 1;
}

FNA3D_TransferBuffer* FNA3D_CommandBuffer_AcquireTransferBuffer(
	FNA3D_CommandBufferManager *manager,
	size_t requiredSize,
	size_t alignment
) {
	FNA3D_TransferBuffer *transferBuffer;
	size_t parentBufferSize;
	uint32_t i;
	size_t size;
	size_t offset;
	FNA3D_CommandBufferContainer *commandBufferContainer =
		manager->currentCommandBufferContainer;

	/* Search the command buffer's current transfer buffers */

	for (i = 0; i < commandBufferContainer->transferBufferCount; i += 1)
	{
		transferBuffer = commandBufferContainer->transferBuffers[i];
		parentBufferSize = manager->driver.GetBufferSize(
			manager->driver.driverData,
			transferBuffer->buffer
		);
		offset = transferBuffer->offset + alignment - (transferBuffer->offset % alignment);

		if (offset + requiredSize <= parentBufferSize)
		{
			transferBuffer->offset = offset;
			return transferBuffer;
		}
	}

	/* Nothing fits so let's get a transfer buffer from the pool */

	if (commandBufferContainer->transferBufferCount == commandBufferContainer->transferBufferCapacity)
	{
		commandBufferContainer->transferBufferCapacity += 1;
		commandBufferContainer->transferBuffers = SDL_realloc(
			commandBufferContainer->transferBuffers,
			commandBufferContainer->transferBufferCapacity * sizeof(FNA3D_TransferBuffer*)
		);
	}

	/* Is the fast transfer buffer available? */
	if (	manager->transferBufferPool.fastTransferBufferAvailable &&
		requiredSize < FAST_TRANSFER_SIZE	)
	{
		transferBuffer = manager->transferBufferPool.fastTransferBuffer;
		manager->transferBufferPool.fastTransferBufferAvailable = 0;

		commandBufferContainer->transferBuffers[commandBufferContainer->transferBufferCount] = transferBuffer;
		commandBufferContainer->transferBufferCount += 1;

		/* If the fast transfer buffer is available, the offset is always zero */
		return transferBuffer;
	}

	/* Nope, let's get a slow buffer */
	for (i = 0; i < manager->transferBufferPool.availableSlowTransferBufferCount; i += 1)
	{
		transferBuffer = manager->transferBufferPool.availableSlowTransferBuffers[i];
		parentBufferSize = manager->driver.GetBufferSize(
			manager->driver.driverData,
			transferBuffer->buffer
		);
		offset = transferBuffer->offset + alignment - (transferBuffer->offset % alignment);

		if (offset + requiredSize <= parentBufferSize)
		{
			commandBufferContainer->transferBuffers[commandBufferContainer->transferBufferCount] = transferBuffer;
			commandBufferContainer->transferBufferCount += 1;

			manager->transferBufferPool.availableSlowTransferBuffers[i] = manager->transferBufferPool.availableSlowTransferBuffers[manager->transferBufferPool.availableSlowTransferBufferCount - 1];
			manager->transferBufferPool.availableSlowTransferBufferCount -= 1;

			transferBuffer->offset = offset;
			return transferBuffer;
		}
	}

	/* Nothing fits still, so let's create a new transfer buffer */

	size = STARTING_TRANSFER_BUFFER_SIZE;

	while (size < requiredSize)
	{
		size *= 2;
	}

	transferBuffer = SDL_malloc(sizeof(FNA3D_TransferBuffer));
	transferBuffer->offset = 0;
	transferBuffer->buffer = manager->driver.CreateTransferBuffer(
		manager->driver.driverData,
		size,
		0
	);

	if (transferBuffer->buffer == NULL)
	{
		FNA3D_LogError("Failed to allocate transfer buffer!");
		return NULL;
	}

	if (commandBufferContainer->transferBufferCount == commandBufferContainer->transferBufferCapacity)
	{
		commandBufferContainer->transferBufferCapacity += 1;
		commandBufferContainer->transferBuffers = SDL_realloc(
			commandBufferContainer->transferBuffers,
			commandBufferContainer->transferBufferCapacity * sizeof(FNA3D_TransferBuffer*)
		);
	}

	commandBufferContainer->transferBuffers[commandBufferContainer->transferBufferCount] = transferBuffer;
	commandBufferContainer->transferBufferCount += 1;

	return transferBuffer;
}

void FNA3D_CommandBuffer_ClearDestroyedBuffer(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle *buffer
) {
	uint32_t i, j;

	/* Don't unbind destroyed buffers! */
	for (i = 0; i < manager->submittedCommandBufferContainerCount; i += 1)
	{
		for (j = 0; j < manager->submittedCommandBufferContainers[i]->boundBufferCount; j += 1)
		{
			if (buffer == manager->submittedCommandBufferContainers[i]->boundBuffers[j])
			{
				manager->submittedCommandBufferContainers[i]->boundBuffers[j] = NULL;
			}
		}
	}
}

void FNA3D_CommandBuffer_ForEachSubmittedBuffer(
	FNA3D_CommandBufferManager *manager,
	void (*callback)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle,
		void* callbackData
	),
	void* callbackData
) {
	uint32_t i;
	for (i = 0; i < manager->submittedCommandBufferContainerCount; i += 1)
	{
		callback(
			manager->driver.driverData,
			manager->submittedCommandBufferContainers[i]->handle,
			callbackData
		);
	}
}
