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

#if FNA3D_DRIVER_SDL

#include "FNA3D_Driver.h"

#include <SDL3/SDL_gpu.h>

static inline SDL_GpuSampleCount XNAToSDL_SampleCount(int32_t sampleCount)
{
	if (sampleCount <= 1)
	{
		return SDL_GPU_SAMPLECOUNT_1;
	}
	else if (sampleCount == 2)
	{
		return SDL_GPU_SAMPLECOUNT_2;
	}
	else if (sampleCount <= 4)
	{
		return SDL_GPU_SAMPLECOUNT_4;
	}
	else if (sampleCount <= 8)
	{
		return SDL_GPU_SAMPLECOUNT_8;
	}
	else
	{
		FNA3D_LogWarn("Unexpected sample count: %d", sampleCount);
		return SDL_GPU_SAMPLECOUNT_1;
	}
}

/* Indirection to cleanly handle Renderbuffers */
typedef struct SDLGPU_TextureHandle
{
    SDL_GpuTexture *texture;
    SDL_GpuTextureCreateInfo createInfo;
} SDLGPU_TextureHandle;

typedef struct SDLGPU_RenderBuffer
{
    SDL_GpuTexture *texture; /* NOTE: does not own the texture reference! */
} SDLGPU_RenderBuffer;

typedef struct SDLGPU_Renderer
{
    SDL_GpuDevice *device;
    SDL_GpuCommandBuffer *commandBuffer;

    SDL_GpuTexture *swapchainTexture;
    uint32_t swapchainTextureWidth;
    uint32_t swapchainTextureHeight;

    uint8_t renderPassInProgress;
    uint8_t needNewRenderPass;

    uint8_t shouldClearColorOnBeginPass;
	uint8_t shouldClearDepthOnBeginPass;
	uint8_t shouldClearStencilOnBeginPass;

    SDL_GpuVec4 clearColorValue;
	SDL_GpuDepthStencilValue clearDepthStencilValue;

	/* Defer render pass settings */
    SDL_GpuTexture *nextRenderPassColorAttachments[MAX_RENDERTARGET_BINDINGS];
    SDL_GpuCubeMapFace nextRenderPassColorAttachmentCubeFace[MAX_RENDERTARGET_BINDINGS];
	uint32_t nextRenderPassColorAttachmentCount;

	SDL_GpuTexture *nextRenderPassDepthStencilAttachment; /* may be NULL */

    SDL_GpuPrimitiveType currentPrimitiveType;
    uint8_t needNewGraphicsPipeline;
} SDLGPU_Renderer;

/* Statics */

static SDL_GpuBackend preferredBackends[2] = { SDL_GPU_BACKEND_VULKAN, SDL_GPU_BACKEND_D3D11 };
static FNA3D_PresentationParameters requestedPresentationParameters;

/* Destroy */

static void SDLGPU_DestroyDevice(FNA3D_Device *device)
{
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) device->driverData;
    SDL_GpuDestroyDevice(renderer->device);
    SDL_free(renderer);
    SDL_free(device);
}

/* Create */

static FNA3D_Renderbuffer* SDLGPU_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
    SDLGPU_RenderBuffer *colorBufferHandle;

    /* Recreate texture with appropriate settings */
    SDL_GpuQueueDestroyTexture(renderer->device, textureHandle->texture);

    textureHandle->createInfo.sampleCount = XNAToSDL_SampleCount(multiSampleCount);
    textureHandle->texture = SDL_GpuCreateTexture(
        renderer->device,
        &textureHandle->createInfo
    );

    if (textureHandle->texture == NULL)
    {
        FNA3D_LogError("Failed to recreate color buffer texture!");
        return NULL;
    }

    colorBufferHandle = SDL_malloc(sizeof(SDLGPU_RenderBuffer));
    colorBufferHandle->texture = textureHandle->texture;

    return (FNA3D_Renderbuffer*) colorBufferHandle;
}

/* Presentation */

static void SDLGPU_SwapBuffers(
    FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDL_GpuSubmit(renderer->device, renderer->commandBuffer);

    renderer->commandBuffer = SDL_GpuAcquireCommandBuffer(renderer->device);
    renderer->swapchainTexture = SDL_GpuAcquireSwapchainTexture(
        renderer->device,
        renderer->commandBuffer,
        overrideWindowHandle,
        &renderer->swapchainTextureWidth,
        &renderer->swapchainTextureHeight
    );
}

static void SDLGPU_INTERNAL_PrepareRenderPassClear(
	SDLGPU_Renderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
    if (!clearColor && !clearDepth && !clearStencil)
	{
		return;
	}

	renderer->shouldClearColorOnBeginPass |= clearColor;
	renderer->shouldClearDepthOnBeginPass |= clearDepth;
	renderer->shouldClearStencilOnBeginPass |= clearStencil;

	if (clearColor)
	{
		renderer->clearColorValue.x = color->x;
		renderer->clearColorValue.y = color->y;
		renderer->clearColorValue.z = color->z;
		renderer->clearColorValue.w = color->w;
	}

	if (clearDepth)
	{
		if (depth < 0.0f)
		{
			depth = 0.0f;
		}
		else if (depth > 1.0f)
		{
			depth = 1.0f;
		}

		renderer->clearDepthStencilValue.depth = depth;
	}

	if (clearStencil)
	{
		renderer->clearDepthStencilValue.stencil = stencil;
	}

	renderer->needNewRenderPass = 1;
}

static void SDLGPU_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	uint8_t clearColor = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;
	uint8_t clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) == FNA3D_CLEAROPTIONS_DEPTHBUFFER;
	uint8_t clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) == FNA3D_CLEAROPTIONS_STENCIL;

    SDLGPU_INTERNAL_PrepareRenderPassClear(
        renderer,
        color,
        depth,
        stencil,
        clearColor,
        clearDepth,
        clearStencil
    );
}

/* Drawing */

static void SDLGPU_INTERNAL_EndPass(
    SDLGPU_Renderer *renderer
) {
    if (renderer->renderPassInProgress)
    {
        SDL_GpuEndRenderPass(
            renderer->device,
            renderer->commandBuffer
        );
    }
}

static void SDLGPU_INTERNAL_BeginRenderPass(
    SDLGPU_Renderer *renderer
) {
    SDL_GpuColorAttachmentInfo colorAttachmentInfos[MAX_RENDERTARGET_BINDINGS];
    SDL_GpuDepthStencilAttachmentInfo depthStencilAttachmentInfo;
    uint32_t i;

    if (!renderer->needNewRenderPass)
    {
        return;
    }

    SDLGPU_INTERNAL_EndPass(renderer);

    for (i = 0; i < renderer->nextRenderPassColorAttachmentCount; i += 1)
    {
        colorAttachmentInfos[i].textureSlice.texture = renderer->nextRenderPassColorAttachments[i];
        colorAttachmentInfos[i].textureSlice.layer = renderer->nextRenderPassColorAttachmentCubeFace[i];
        colorAttachmentInfos[i].textureSlice.mipLevel = 0;

        colorAttachmentInfos[i].loadOp =
            renderer->shouldClearColorOnBeginPass ?
                SDL_GPU_LOADOP_CLEAR :
                SDL_GPU_LOADOP_LOAD;

        /* We always have to store just in case changing render state breaks the render pass. */
        /* FIXME: perhaps there is a way around this? */
        colorAttachmentInfos[i].storeOp = SDL_GPU_STOREOP_STORE;

        colorAttachmentInfos[i].writeOption =
            colorAttachmentInfos[i].loadOp == SDL_GPU_LOADOP_LOAD ?
                SDL_GPU_TEXTUREWRITEOPTIONS_SAFE :
                SDL_GPU_TEXTUREWRITEOPTIONS_CYCLE; /* cycle if we can, it's fast! */

        if (renderer->shouldClearColorOnBeginPass)
        {
            colorAttachmentInfos[i].clearColor = renderer->clearColorValue;
        }
        else
        {
            colorAttachmentInfos[i].clearColor.x = 0;
            colorAttachmentInfos[i].clearColor.y = 0;
            colorAttachmentInfos[i].clearColor.z = 0;
            colorAttachmentInfos[i].clearColor.w = 0;
        }
    }

    if (renderer->nextRenderPassDepthStencilAttachment != NULL)
    {
        depthStencilAttachmentInfo.textureSlice.texture = renderer->nextRenderPassDepthStencilAttachment;
        depthStencilAttachmentInfo.textureSlice.layer = 0;
        depthStencilAttachmentInfo.textureSlice.mipLevel = 0;

        depthStencilAttachmentInfo.loadOp =
            renderer->shouldClearDepthOnBeginPass ?
                SDL_GPU_LOADOP_CLEAR :
                SDL_GPU_LOADOP_DONT_CARE;

        if (renderer->shouldClearDepthOnBeginPass)
        {
            depthStencilAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
        }
        else
        {
            /* FIXME: is there a way to safely get rid of this load op? */
            depthStencilAttachmentInfo.loadOp = SDL_GPU_LOADOP_LOAD;
        }

        if (renderer->shouldClearStencilOnBeginPass)
        {
            depthStencilAttachmentInfo.stencilLoadOp = SDL_GPU_LOADOP_CLEAR;
        }
        else
        {
            /* FIXME: is there a way to safely get rid of this load op? */
            depthStencilAttachmentInfo.stencilLoadOp = SDL_GPU_LOADOP_LOAD;
        }

        /* We always have to store just in case changing render state breaks the render pass. */
        /* FIXME: perhaps there is a way around this? */
        depthStencilAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

        depthStencilAttachmentInfo.writeOption =
            depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD || depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD ?
                SDL_GPU_TEXTUREWRITEOPTIONS_SAFE :
                SDL_GPU_TEXTUREWRITEOPTIONS_CYCLE; /* Cycle if we can! */

        if (renderer->shouldClearDepthOnBeginPass || renderer->shouldClearStencilOnBeginPass)
        {
            depthStencilAttachmentInfo.depthStencilClearValue = renderer->clearDepthStencilValue;
        }
    }

    SDL_GpuBeginRenderPass(
        renderer->device,
        renderer->commandBuffer,
        colorAttachmentInfos,
        renderer->nextRenderPassColorAttachmentCount,
        renderer->nextRenderPassDepthStencilAttachment != NULL ? &depthStencilAttachmentInfo : NULL
    );

    renderer->needNewRenderPass = 0;

    renderer->shouldClearColorOnBeginPass = 0;
    renderer->shouldClearDepthOnBeginPass = 0;
    renderer->shouldClearStencilOnBeginPass = 0;

    renderer->needNewGraphicsPipeline = 1;
}

static void SDLGPU_SetRenderTargets(
    FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveTargetContents /* ignored */
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    uint32_t i;

    if (
        renderer->shouldClearColorOnBeginPass ||
        renderer->shouldClearDepthOnBeginPass ||
        renderer->shouldClearDepthOnBeginPass
    ) {
        SDLGPU_INTERNAL_BeginRenderPass(renderer);
    }

    for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
    {
        renderer->nextRenderPassColorAttachments[i] = NULL;
    }
    renderer->nextRenderPassDepthStencilAttachment = NULL;

    if (numRenderTargets <= 0)
    {
        if (renderer->swapchainTexture == NULL)
        {
            /* swapchain is invalid, this is a no-op */
            return;
        }

        renderer->nextRenderPassColorAttachments[0] = renderer->swapchainTexture;
        renderer->nextRenderPassColorAttachmentCubeFace[0] = 0;
        renderer->nextRenderPassColorAttachmentCount = 1;
    }
    else
    {
        for (i = 0; i < numRenderTargets; i += 1)
        {
            renderer->nextRenderPassColorAttachmentCubeFace[i] = (
                renderTargets[i].type == FNA3D_RENDERTARGET_TYPE_CUBE ?
                    (SDL_GpuCubeMapFace) renderTargets[i].cube.face :
                    0
            );

            if (renderTargets[i].colorBuffer != NULL)
            {
                renderer->nextRenderPassColorAttachments[i] = ((SDLGPU_RenderBuffer*) renderTargets[i].colorBuffer)->texture;
            }
        }

        renderer->nextRenderPassColorAttachmentCount = numRenderTargets;
    }

    if (depthStencilBuffer != NULL)
    {
        renderer->nextRenderPassDepthStencilAttachment = ((SDLGPU_RenderBuffer*) depthStencilBuffer)->texture;
    }

    renderer->needNewRenderPass = 1;
}

static void SDLGPU_INTERNAL_BindGraphicsPipeline(
    SDLGPU_Renderer *renderer
) {
    /* TODO */
}

static void SDLGPU_DrawInstancedPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	int32_t instanceCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDL_GpuBufferBinding indexBinding;

	/* Note that minVertexIndex/numVertices are NOT used! */

    if (primitiveType != renderer->currentPrimitiveType)
    {
        renderer->currentPrimitiveType = primitiveType;
        renderer->needNewGraphicsPipeline = 1;
    }

    SDLGPU_INTERNAL_BeginRenderPass(renderer);
    SDLGPU_INTERNAL_BindGraphicsPipeline(renderer);

    /* TODO: bind vertex buffers */

    indexBinding.gpuBuffer = (SDL_GpuBuffer*) indices;
    indexBinding.offset = 0;

    SDL_GpuBindIndexBuffer(
        renderer->device,
        renderer->commandBuffer,
        &indexBinding,
        indexElementSize == FNA3D_INDEXELEMENTSIZE_16BIT ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT
    );

    SDL_GpuDrawInstancedPrimitives(
        renderer->device,
        renderer->commandBuffer,
        baseVertex,
        startIndex,
        primitiveCount,
        instanceCount
    );
}

static void SDLGPU_DrawIndexedPrimitives(
    FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
) {
    SDLGPU_DrawInstancedPrimitives(
        driverData,
        primitiveType,
        baseVertex,
        minVertexIndex,
        numVertices,
        startIndex,
        primitiveCount,
        1,
        indices,
        indexElementSize
    );
}

/* Initialization */

static uint8_t SDLGPU_PrepareWindowAttributes(uint32_t *flags)
{
    SDL_GpuBackend selectedBackend =
        SDL_GpuSelectBackend(
            preferredBackends,
            2,
            flags
        );

    if (selectedBackend == SDL_GPU_BACKEND_INVALID)
    {
        FNA3D_LogError("Failed to select backend!");
        return 0;
    }

    return 1;
}

static FNA3D_Device* SDLGPU_CreateDevice(
    FNA3D_PresentationParameters *presentationParameters,
    uint8_t debugMode
) {
    SDLGPU_Renderer *renderer;
    SDL_GpuDevice *device;
    FNA3D_Device *result;

    requestedPresentationParameters = *presentationParameters;
    device = SDL_GpuCreateDevice(debugMode);

    if (device == NULL)
    {
        FNA3D_LogError("Failed to create SDLGPU device!");
        return NULL;
    }

    result = SDL_malloc(sizeof(FNA3D_Device));
    ASSIGN_DRIVER(SDLGPU)

    renderer = SDL_malloc(sizeof(SDLGPU_Renderer));
    SDL_memset(renderer, '\0', sizeof(SDLGPU_Renderer));
    renderer->device = device;

    result->driverData = (FNA3D_Renderer*) renderer;

    return result;
}

FNA3D_Driver SDLGPUDriver = {
    "SDLGPU",
    SDLGPU_PrepareWindowAttributes,
    SDLGPU_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_SDL */
