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

#ifndef FNA3D_COMMANDBUFFER_H
#define FNA3D_COMMANDBUFFER_H

#include "FNA3D_Driver.h"
#include "FNA3D_Memory.h"

/* Driver-defined command buffer structures */

typedef struct FNA3D_CommandBuffer FNA3D_CommandBuffer;

/* Renderers will contain a command buffer driver, so we can call back into the
 * renderer to do platform-specific things that don't have to do with the
 * high-level command buffer management
 */

typedef struct FNA3D_CommandBufferDriver
{
	FNA3D_CommandBuffer* (*AllocCommandBuffer)(
		FNA3D_Renderer *driverData,
		uint8_t fenceSignaled
	);
	void (*FreeCommandBuffer)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle
	);

	void (*BeginRecording)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle
	);
	void (*EndRecording)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle
	);
	void (*Reset)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle
	);
	uint8_t (*QueryFence)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle
	);
	void (*WaitForFences)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer **handles,
		size_t handleCount
	);

	FNA3D_BufferHandle* (*CreateTransferBuffer)(
		FNA3D_Renderer *driverData,
		size_t size,
		uint8_t preferDeviceLocal
	);

	void (*IncBufferRef)(
		FNA3D_Renderer *renderer,
		FNA3D_BufferHandle *handle
	);
	void (*DecBufferRef)(
		FNA3D_Renderer *renderer,
		FNA3D_BufferHandle *handle
	);
	size_t (*GetBufferSize)(
		FNA3D_Renderer *driverData,
		FNA3D_BufferHandle *handle
	);

	void (*DestroyTexture)(
		FNA3D_Renderer *driverData,
		FNA3D_Texture *texture
	);
	void (*DestroyBuffer)(
		FNA3D_Renderer *driverData,
		FNA3D_BufferHandle *buffer
	);
	void (*DestroyRenderbuffer)(
		FNA3D_Renderer *driverData,
		FNA3D_Renderbuffer *renderbuffer
	);
	void (*DestroyEffect)(
		FNA3D_Renderer *driverData,
		FNA3D_Effect *effect
	);

	FNA3D_Renderer *driverData;
} FNA3D_CommandBufferDriver;

#define ASSIGN_COMMANDBUFFER_DRIVER_FUNC(func, name) \
	commandBufferDriver.func = name##_CommandBuffer_##func;
#define ASSIGN_COMMANDBUFFER_DRIVER(name) \
	commandBufferDriver.driverData = (FNA3D_Renderer*) renderer; \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(AllocCommandBuffer, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(FreeCommandBuffer, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(BeginRecording, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(EndRecording, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(Reset, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(QueryFence, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(WaitForFences, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(CreateTransferBuffer, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(IncBufferRef, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(DecBufferRef, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(GetBufferSize, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(DestroyTexture, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(DestroyBuffer, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(DestroyRenderbuffer, name) \
	ASSIGN_COMMANDBUFFER_DRIVER_FUNC(DestroyEffect, name)

/*
 * Command Buffer Interface
 */

typedef struct FNA3D_CommandBufferManager FNA3D_CommandBufferManager;

/* Renderers will receive this struct, perform the transfer, then update the
 * offset value for future AcquireTransferBuffer calls
 */
typedef struct FNA3D_TransferBuffer
{
	FNA3D_BufferHandle *buffer;
	size_t offset;
} FNA3D_TransferBuffer;

FNA3D_SHAREDINTERNAL FNA3D_CommandBufferManager* FNA3D_CreateCommandBufferManager(
	FNA3D_CommandBufferDriver *driver
);
FNA3D_SHAREDINTERNAL void FNA3D_DestroyCommandBufferManager(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_BeginRecording(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_EndRecording(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_Finish(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_LockForRendering(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_UnlockFromRendering(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_LockForDefrag(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_UnlockFromDefrag(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_LockForTransfer(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_UnlockFromTransfer(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_LockForSubmit(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_UnlockFromSubmit(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_AddDisposeTexture(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Texture *texture
);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_AddDisposeRenderbuffer(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Renderbuffer *renderbuffer
);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_AddDisposeEffect(
	FNA3D_CommandBufferManager *manager,
	FNA3D_Effect *effect
);
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_AddDisposeBuffers(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle **handles,
	size_t handleCount
);

FNA3D_SHAREDINTERNAL uint8_t FNA3D_CommandBuffer_PerformCleanups(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_SubmitCurrent(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL FNA3D_CommandBuffer* FNA3D_CommandBuffer_GetCurrent(FNA3D_CommandBufferManager *manager);
FNA3D_SHAREDINTERNAL FNA3D_CommandBuffer* FNA3D_CommandBuffer_GetDefragBuffer(FNA3D_CommandBufferManager *manager);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_MarkBufferAsBound(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle *buffer
);
FNA3D_SHAREDINTERNAL FNA3D_TransferBuffer* FNA3D_CommandBuffer_AcquireTransferBuffer(
	FNA3D_CommandBufferManager *manager,
	size_t requiredSize,
	size_t alignment
);

FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_ClearDestroyedBuffer(
	FNA3D_CommandBufferManager *manager,
	FNA3D_BufferHandle *buffer
);

/* FIXME: This is kind of a kludge -flibit */
FNA3D_SHAREDINTERNAL void FNA3D_CommandBuffer_ForEachSubmittedBuffer(
	FNA3D_CommandBufferManager *manager,
	void (*callback)(
		FNA3D_Renderer *driverData,
		FNA3D_CommandBuffer *handle,
		void* callbackData
	),
	void* callbackData
);

#endif /* FNA3D_COMMANDBUFFER_H */
