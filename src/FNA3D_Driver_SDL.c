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
#include "FNA3D_PipelineCache.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3_shader/SDL_shader.h>

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

typedef struct SDLGPU_Effect
{
    MOJOSHADER_effect *effect;
} SDLGPU_Effect;

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
	int32_t currentVertexBufferBindingsIndex;

    PackedVertexBufferBindingsArray vertexBufferBindingsCache;

    /* Vertex buffer bind settings*/
	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding vertexBindings[MAX_BOUND_VERTEX_BUFFERS];
	FNA3D_VertexElement vertexElements[MAX_BOUND_VERTEX_BUFFERS][MAX_VERTEX_ATTRIBUTES];
    SDL_GpuBufferBinding vertexBufferBindings[MAX_BOUND_VERTEX_BUFFERS];

    MOJOSHADER_sdlContext *mojoshaderContext;
} SDLGPU_Renderer;

/* Statics */

static SDL_GpuBackend preferredBackends[2] = { SDL_GPU_BACKEND_VULKAN, SDL_GPU_BACKEND_D3D11 };
static FNA3D_PresentationParameters requestedPresentationParameters;
static SDL_GpuBackend selectedBackend = SDL_GPU_BACKEND_INVALID;

/* Destroy */

static void SDLGPU_DestroyDevice(FNA3D_Device *device)
{
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) device->driverData;
    SDL_GpuDestroyDevice(renderer->device);
    SDL_free(renderer);
    SDL_free(device);
}

/* Create */

static void SDLGPU_CreateEffect(
    FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    MOJOSHADER_effectShaderContext shaderBackend;
    SDLGPU_Effect *result;
    int32_t i;

	shaderBackend.shaderContext = renderer->mojoshaderContext;
	shaderBackend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_sdlCompileShader;
	shaderBackend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_sdlShaderAddRef;
	shaderBackend.deleteShader = (MOJOSHADER_deleteShaderFunc) MOJOSHADER_sdlDeleteShader;
	shaderBackend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_sdlGetShaderParseData;
	shaderBackend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_sdlBindShaders;
	shaderBackend.getBoundShaders = (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_sdlGetBoundShaders;
	shaderBackend.mapUniformBufferMemory = (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_sdlMapUniformBufferMemory;
	shaderBackend.unmapUniformBufferMemory = (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_sdlUnmapUniformBufferMemory;
	shaderBackend.getError = (MOJOSHADER_getErrorFunc) MOJOSHADER_sdlGetError;
	shaderBackend.m = NULL;
	shaderBackend.f = NULL;
	shaderBackend.malloc_data = NULL;

    *effectData = MOJOSHADER_compileEffect(
        effectCode,
        effectCodeLength,
        NULL,
        0,
        NULL,
        0,
        &shaderBackend
    );

    for (i = 0; i < (*effectData)->error_count; i += 1)
	{
		FNA3D_LogError(
			"MOJOSHADER_compileEffect Error: %s",
			(*effectData)->errors[i].error
		);
	}

    result = (SDLGPU_Effect*) SDL_malloc(sizeof(SDLGPU_Effect));
    result->effect = *effectData;
    *effect = (FNA3D_Effect*) result;
}

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

static void SDLGPU_ApplyVertexBufferBindings(
    FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    MOJOSHADER_sdlShader *vertexShader, *blah;
    void* bindingsResult;
    FNA3D_VertexBufferBinding *src, *dst;
    int32_t i, bindingsIndex;
    uint32_t hash;

    /* link/compile shader program if it hasn't been yet */
    if (!MOJOSHADER_sdlCheckProgramStatus(renderer->mojoshaderContext))
    {
        MOJOSHADER_sdlLinkProgram(renderer->mojoshaderContext);
    }

	/* Check VertexBufferBindings */
	MOJOSHADER_sdlGetBoundShaders(renderer->mojoshaderContext, &vertexShader, &blah);

	bindingsResult = PackedVertexBufferBindingsArray_Fetch(
		renderer->vertexBufferBindingsCache,
		bindings,
		numBindings,
		vertexShader,
		&bindingsIndex,
		&hash
	);

    if (bindingsResult == NULL)
    {
        PackedVertexBufferBindingsArray_Insert(
			&renderer->vertexBufferBindingsCache,
			bindings,
			numBindings,
			vertexShader,
			(void*) 69420
		);
    }

    /* TODO: set elements for mojoshader */
    /* should we copy the d3d11 FetchBindingsInputLayout structure? */
    if (bindingsUpdated)
    {
        renderer->numVertexBindings = numBindings;
        for (i = 0; i < numBindings; i += 1)
        {
            src = &bindings[i];
            dst = &renderer->vertexBindings[i];
            dst->vertexBuffer = src->vertexBuffer;
            dst->vertexOffset = src->vertexOffset;
            dst->instanceFrequency = src->instanceFrequency;
            dst->vertexDeclaration.vertexStride = src->vertexDeclaration.vertexStride;
            dst->vertexDeclaration.elementCount = src->vertexDeclaration.elementCount;
            SDL_memcpy(
                dst->vertexDeclaration.elementCount,
                src->vertexDeclaration.elementCount,
                sizeof(FNA3D_VertexElement) * src->vertexDeclaration.elementCount
            );
        }
    }

    if (bindingsIndex != renderer->currentVertexBufferBindingsIndex)
    {
        renderer->currentVertexBufferBindingsIndex = bindingsIndex;
        renderer->needNewGraphicsPipeline = 1;
    }

    /* Don't actually bind buffers yet because pipelines are lazily bound */
    for (i = 0; i < numBindings; i += 1)
    {
        renderer->vertexBufferBindings[i].gpuBuffer = (SDL_GpuBuffer*) bindings[i].vertexBuffer;
        renderer->vertexBufferBindings[i].offset = (bindings[i].vertexOffset + baseVertex) * bindings[i].vertexDeclaration.vertexStride;
    }
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

    renderer->mojoshaderContext = MOJOSHADER_sdlCreateContext(
        device,
        selectedBackend,
        NULL,
        NULL,
        NULL
    );

    return result;
}

FNA3D_Driver SDLGPUDriver = {
    "SDLGPU",
    SDLGPU_PrepareWindowAttributes,
    SDLGPU_CreateDevice
};

/* Mojoshader interop */

#include "mojoshader_internal.h"

/* Max entries for each register file type */
#define MAX_REG_FILE_F 8192
#define MAX_REG_FILE_I 2047
#define MAX_REG_FILE_B 2047

typedef struct MOJOSHADER_sdlContext MOJOSHADER_sdlContext;
typedef struct MOJOSHADER_sdlShader MOJOSHADER_sdlShader;
typedef struct MOJOSHADER_sdlProgram MOJOSHADER_sdlProgram;

struct MOJOSHADER_sdlContext
{
    SDL_GpuDevice *device;
    SDL_GpuBackend backend;
    const char *profile;

    MOJOSHADER_malloc malloc_fn;
    MOJOSHADER_free free_fn;
    void *malloc_data;

    /* The constant register files...
     * !!! FIXME: Man, it kills me how much memory this takes...
     * !!! FIXME:  ... make this dynamically allocated on demand.
     */
    float vs_reg_file_f[MAX_REG_FILE_F * 4];
    int32_t vs_reg_file_i[MAX_REG_FILE_I * 4];
    uint8_t vs_reg_file_b[MAX_REG_FILE_B * 4];
    float ps_reg_file_f[MAX_REG_FILE_F * 4];
    int32_t ps_reg_file_i[MAX_REG_FILE_I * 4];
    uint8_t ps_reg_file_b[MAX_REG_FILE_B * 4];

    MOJOSHADER_sdlProgram *bound_program;
    HashTable *linker_cache;

    /*
     * Note that these may not necessarily align with bound_program!
     * We need to store these so effects can have overlapping shaders.
     */
    MOJOSHADER_sdlShader *vertex_shader;
    MOJOSHADER_sdlShader *pixel_shader;

    uint8_t vertex_shader_needs_bind;
    uint8_t pixel_shader_needs_bind;
};

struct MOJOSHADER_sdlShader
{
    const MOJOSHADER_parseData *parseData;
    uint16_t tag;
    uint32_t refcount;
};

struct MOJOSHADER_sdlProgram
{
    SDL_GpuShaderModule *vertexModule;
    SDL_GpuShaderModule *pixelModule;
    MOJOSHADER_sdlShader *vertexShader;
    MOJOSHADER_sdlShader *pixelShader;
};

typedef struct BoundShaders
{
    MOJOSHADER_sdlShader *vertex;
    MOJOSHADER_sdlShader *fragment;
} BoundShaders;

/* Error state... */
static char error_buffer[1024] = { '\0' };

static void set_error(const char *str)
{
    snprintf(error_buffer, sizeof (error_buffer), "%s", str);
}

static inline void out_of_memory(void)
{
    set_error("out of memory");
}

/* Internals */

static uint32_t hash_shaders(const void *sym, void *data)
{
    (void) data;
    const BoundShaders *s = (const BoundShaders *) sym;
    const uint16_t v = (s->vertex) ? s->vertex->tag : 0;
    const uint16_t f = (s->fragment) ? s->fragment->tag : 0;
    return ((uint32_t) v << 16) | (uint32_t) f;
} // hash_shaders

static int match_shaders(const void *_a, const void *_b, void *data)
{
    (void) data;
    const BoundShaders *a = (const BoundShaders *) _a;
    const BoundShaders *b = (const BoundShaders *) _b;

    const uint16_t av = (a->vertex) ? a->vertex->tag : 0;
    const uint16_t bv = (b->vertex) ? b->vertex->tag : 0;
    if (av != bv)
        return 0;

    const uint16_t af = (a->fragment) ? a->fragment->tag : 0;
    const uint16_t bf = (b->fragment) ? b->fragment->tag : 0;
    if (af != bf)
        return 0;

    return 1;
} // match_shaders

static void nuke_shaders(
    const void *_ctx,
    const void *key,
    const void *value,
    void *data
) {
    MOJOSHADER_sdlContext *ctx = (MOJOSHADER_sdlContext *) _ctx;
    (void) data;
    ctx->free_fn((void *) key, ctx->malloc_data); // this was a BoundShaders struct.
    MOJOSHADER_sdlDeleteProgram(ctx, (MOJOSHADER_sdlProgram *) value);
} // nuke_shaders

static void update_uniform_buffer(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *shader
) {
    int32_t i, j;
    int32_t offset;
    uint8_t *contents;
    uint32_t content_size;
    uint32_t *contentsI;
    float *regF; int *regI; uint8_t *regB;

    if (shader == NULL || shader->parseData->uniform_count == 0)
        return;

    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
    {
        regF = ctx->vs_reg_file_f;
        regI = ctx->vs_reg_file_i;
        regB = ctx->vs_reg_file_b;
    }
    else
    {
        regF = ctx->ps_reg_file_f;
        regI = ctx->ps_reg_file_i;
        regB = ctx->ps_reg_file_b;
    }
    content_size = 0;

    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        const int32_t size = arrayCount ? arrayCount : 1;
        content_size += size * 16;
    }

    contents = (uint8_t*) ctx->malloc_fn(content_size, ctx->malloc_data);

    offset = 0;
    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t index = shader->parseData->uniforms[i].index;
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        const int32_t size = arrayCount ? arrayCount : 1;

        switch (shader->parseData->uniforms[i].type)
        {
            case MOJOSHADER_UNIFORM_FLOAT:
                memcpy(
                    contents + offset,
                    &regF[4 * index],
                    size * 16
                );
                break;

            case MOJOSHADER_UNIFORM_INT:
                memcpy(
                    contents + offset,
                    &regI[4 * index],
                    size * 16
                );
                break;

            case MOJOSHADER_UNIFORM_BOOL:
                contentsI = (uint32_t *) (contents + offset);
                for (j = 0; j < size; j++)
                    contentsI[j * 4] = regB[index + j];
                break;

            default:
                set_error(
                    "SOMETHING VERY WRONG HAPPENED WHEN UPDATING UNIFORMS"
                );
                assert(0);
                break;
        }

        offset += size * 16;
    }

    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
    {
        regF = ctx->vs_reg_file_f;
        regI = ctx->vs_reg_file_i;
        regB = ctx->vs_reg_file_b;

        SDL_GpuPushVertexShaderUniforms(
            ctx->device,
            commandBuffer,
            contents,
            content_size
        );
    }
    else
    {
        regF = ctx->ps_reg_file_f;
        regI = ctx->ps_reg_file_i;
        regB = ctx->ps_reg_file_b;

        SDL_GpuPushFragmentShaderUniforms(
            ctx->device,
            commandBuffer,
            contents,
            content_size
        );
    }

    ctx->free_fn(contents, ctx->malloc_data);
}

static uint16_t shaderTagCounter = 1;

MOJOSHADER_sdlContext *MOJOSHADER_sdlCreateContext(
    SDL_GpuDevice *device,
    SDL_GpuBackend backend,
    MOJOSHADER_malloc m,
    MOJOSHADER_free f,
    void *malloc_d
) {
    MOJOSHADER_sdlContext* resultCtx;

    if (m == NULL) m = MOJOSHADER_internal_malloc;
    if (f == NULL) f = MOJOSHADER_internal_free;

    resultCtx = (MOJOSHADER_sdlContext*) m(sizeof(MOJOSHADER_sdlContext), malloc_d);
    if (resultCtx == NULL)
    {
        out_of_memory();
        goto init_fail;
    }

    SDL_memset(resultCtx, '\0', sizeof(MOJOSHADER_sdlContext));
    resultCtx->device = device;
    resultCtx->backend = backend;
    resultCtx->profile = "spirv"; /* always use spirv and interop with SDL3_shader */

    resultCtx->malloc_fn = m;
    resultCtx->free_fn = f;
    resultCtx->malloc_data = malloc_d;

    return resultCtx;

init_fail:
    if (resultCtx != NULL)
        f(resultCtx, malloc_d);
    return NULL;
}

MOJOSHADER_sdlShader *MOJOSHADER_sdlCompileShader(
    MOJOSHADER_sdlContext *ctx,
    const char *mainfn,
    const unsigned char *tokenbuf,
    const unsigned int bufsize,
    const MOJOSHADER_swizzle *swiz,
    const unsigned int swizcount,
    const MOJOSHADER_samplerMap *smap,
    const unsigned int smapcount
) {
    MOJOSHADER_sdlShader *shader;

    const MOJOSHADER_parseData *pd = MOJOSHADER_parse(
        "spirv", mainfn,
        tokenbuf, bufsize,
        swiz, swizcount,
        smap, smapcount,
        ctx->malloc_fn,
        ctx->free_fn,
        ctx->malloc_data
    );

    if (pd->error_count > 0)
    {
        set_error(pd->errors[0].error);
        goto parse_shader_fail;
    }

    shader = (MOJOSHADER_sdlShader*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlShader), ctx->malloc_data);
    if (shader == NULL)
    {
        out_of_memory();
        goto parse_shader_fail;
    }

    shader->parseData = pd;
    shader->refcount = 1;
    shader->tag = shaderTagCounter++;
    return shader;

parse_shader_fail:
    MOJOSHADER_freeParseData(pd);
    if (shader != NULL)
        ctx->free_fn(shader, ctx->malloc_data);
    return NULL;
}

MOJOSHADER_sdlProgram *MOJOSHADER_sdlLinkProgram(
    MOJOSHADER_sdlContext *ctx
) {
    MOJOSHADER_sdlProgram *result;
    MOJOSHADER_sdlShader *vshader = ctx->vertex_shader;
    MOJOSHADER_sdlShader *pshader = ctx->pixel_shader;
    const char *v_shader_source;
    const char *p_shader_source;
    uint32_t v_shader_len;
    uint32_t p_shader_len;
    const char *v_transpiled_source;
    const char *p_transpiled_source;
    size_t v_transpiled_len;
    size_t p_transpiled_len;
    SDL_GpuShaderModuleCreateRawInfo createInfo;

    if ((vshader == NULL) || (pshader == NULL)) /* Both shaders MUST exist! */
    {
        return NULL;
    }

    result = (MOJOSHADER_sdlProgram*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlProgram), ctx->malloc_data);

    if (result == NULL)
    {
        out_of_memory();
        return NULL;
    }

    MOJOSHADER_spirv_link_attributes(vshader->parseData, pshader->parseData);
    v_shader_source = vshader->parseData->output;
    p_shader_source = pshader->parseData->output;
    v_shader_len = vshader->parseData->output - sizeof(SpirvPatchTable);
    p_shader_len = pshader->parseData->output - sizeof(SpirvPatchTable);

    v_transpiled_source = SHD_TranslateFromSPIRV(
        ctx->backend,
        v_shader_source,
        v_shader_len,
        &v_transpiled_len
    );

    if (v_transpiled_source == NULL)
    {
        set_error("Failed to transpile vertex shader from SPIR-V!");
        return NULL;
    }

    p_transpiled_source = SHD_TranslateFromSPIRV(
        ctx->backend,
        p_shader_source,
        p_shader_len,
        &p_transpiled_len
    );

    if (p_transpiled_source == NULL)
    {
        set_error("Failed to transpile pixel shader from SPIR-V!");
        ctx->free_fn(v_transpiled_source, ctx->malloc_data);
        return NULL;
    }

    createInfo.code     = v_transpiled_source;
    createInfo.codeSize = v_transpiled_len;
    createInfo.type     = SDL_GPU_SHADERTYPE_VERTEX;

    result->vertexModule = SDL_GpuCreateShaderModuleRaw(
        ctx->device,
        &createInfo
    );

    ctx->free_fn(v_transpiled_source, ctx->malloc_data);

    if (result->vertexModule == NULL)
    {
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    }

    createInfo.code     = p_transpiled_source;
    createInfo.codeSize = p_transpiled_len;
    createInfo.codeSize = SDL_GPU_SHADERTYPE_FRAGMENT;

    result->pixelModule = SDL_GpuCreateShaderModuleRaw(
        ctx->device,
        &createInfo
    );

    ctx->free_fn(p_transpiled_source, ctx->malloc_data);

    if (result->pixelModule == NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, result->vertexModule);
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    }

    result->vertexShader = vshader;
    result->pixelShader = pshader;

    ctx->bound_program = result;

    return result;
}

void MOJOSHADER_sdlBindShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *vshader,
    MOJOSHADER_sdlShader *pshader
) {

    if (vshader != NULL)
    {
        ctx->vertex_shader = vshader;
    }

    if (pshader != NULL)
    {
        ctx->pixel_shader = pshader;
    }
}

void MOJOSHADER_sdlGetBoundShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader **vshader,
    MOJOSHADER_sdlShader **pshader
) {
    if (vshader != NULL)
    {
        if (ctx->bound_program != NULL)
        {
            *vshader = ctx->bound_program->vertexShader;
        }
        else
        {
            *vshader = ctx->vertex_shader; /* In case a pshader isn't set yet */
        }
    }
    if (pshader != NULL)
    {
        if (ctx->bound_program != NULL)
        {
            *pshader = ctx->bound_program->pixelShader;
        }
        else
        {
            *pshader = ctx->pixel_shader; /* In case a vshader isn't set yet */
        }
    }
}

void MOJOSHADER_sdlMapUniformBufferMemory(
    MOJOSHADER_sdlContext *ctx,
    float **vsf, int **vsi, unsigned char **vsb,
    float **psf, int **psi, unsigned char **psb
) {
    *vsf = ctx->vs_reg_file_f;
    *vsi = ctx->vs_reg_file_i;
    *vsb = ctx->vs_reg_file_b;
    *psf = ctx->ps_reg_file_f;
    *psi = ctx->ps_reg_file_i;
    *psb = ctx->ps_reg_file_b;
}

void MOJOSHADER_sdlUnmapUniformBufferMemory(MOJOSHADER_sdlContext *ctx)
{
    if (ctx->bound_program == NULL)
    {
        return; /* Ignore buffer updates until we have a real program linked */
    }

    update_uniform_buffer(ctx, ctx->bound_program->vertexShader);
    update_uniform_buffer(ctx, ctx->bound_program->pixelShader);
}

void MOJOSHADER_sdlDeleteShader(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *shader
) {
    if (shader != NULL)
    {
        if (shader->refcount > 1)
            shader->refcount--;
        else
        {
            // See if this was bound as an unlinked program anywhere...
            if (ctx->linker_cache)
            {
                const void *key = NULL;
                void *iter = NULL;
                int morekeys = hash_iter_keys(ctx->linker_cache, &key, &iter);
                while (morekeys)
                {
                    const BoundShaders *shaders = (const BoundShaders *) key;
                    // Do this here so we don't confuse the iteration by removing...
                    morekeys = hash_iter_keys(ctx->linker_cache, &key, &iter);
                    if ((shaders->vertex == shader) || (shaders->fragment == shader))
                    {
                        // Deletes the linked program
                        hash_remove(ctx->linker_cache, shaders, ctx);
                    }
                }
            }

            MOJOSHADER_freeParseData(shader->parseData);
            ctx->free_fn(shader, ctx->malloc_data);
        }
    }
}

/* Returns 0 if program not already linked. */
uint8_t MOJOSHADER_sdlCheckProgramStatus(
    MOJOSHADER_sdlContext *ctx
) {
    MOJOSHADER_sdlShader *vshader = ctx->vertex_shader;
    MOJOSHADER_sdlShader *pshader = ctx->pixel_shader;

    if (ctx->linker_cache == NULL)
    {
        ctx->linker_cache = hash_create(NULL, hash_shaders, match_shaders,
                                        nuke_shaders, 0, ctx->malloc_fn,
                                        ctx->free_fn, ctx->malloc_data);

        if (ctx->linker_cache == NULL)
        {
            out_of_memory();
            return 0;
        }
    }

    BoundShaders shaders;
    shaders.vertex = vshader;
    shaders.fragment = pshader;

    const void *val = NULL;
    return hash_find(ctx->linker_cache, &shaders, &val);
}

void MOJOSHADER_sdlShaderAddRef(MOJOSHADER_sdlShader *shader)
{
    if (shader != NULL)
        shader->refcount++;
}

const MOJOSHADER_parseData *MOJOSHADER_sdlGetShaderParseData(
    MOJOSHADER_sdlShader *shader
) {
    return (shader != NULL) ? shader->parseData : NULL;
}

void MOJOSHADER_sdlDeleteProgram(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlProgram *p
) {
    if (p->vertexModule != NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->vertexModule);
    }
    if (p->pixelModule != NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->pixelModule);
    }
    ctx->free_fn(p, ctx->malloc_data);
}

void MOJOSHADER_sdlGetError(
    MOJOSHADER_sdlContext *ctx
) {
    return error_buffer;
}

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_SDL */
