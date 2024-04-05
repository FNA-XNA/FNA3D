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

#include <SDL3/SDL.h>

/* TODO: Incorporate into MojoShader when the ABI freezes */
#include "mojoshader_sdlgpu.c"

#include "FNA3D_Driver.h"
#include "FNA3D_PipelineCache.h"

#define MAX_FRAMES_IN_FLIGHT 3

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

static inline float XNAToSDL_DepthBiasScale(SDL_GpuTextureFormat format)
{
	switch (format)
	{
		case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
			return (float) ((1 << 16) - 1);

		case SDL_GPU_TEXTUREFORMAT_D32_SFLOAT:
		case SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
			return (float) ((1 << 23) - 1);

		default:
			return 0.0f;
	}
}

static inline SDL_GpuTextureFormat XNAToSDL_DepthFormat(
	FNA3D_DepthFormat format
) {
	switch (format)
	{
		case FNA3D_DEPTHFORMAT_D16:
			return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		case FNA3D_DEPTHFORMAT_D24:
			return SDL_GPU_TEXTUREFORMAT_D32_SFLOAT;
		case FNA3D_DEPTHFORMAT_D24S8:
			return SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT;
		default:
			FNA3D_LogError("Unrecognized depth format!");
			return 0;
	}
}

static inline uint32_t D3D11_INTERNAL_RoundToAlignment(
	uint32_t value,
	uint32_t alignment
) {
	return alignment * ((value + alignment - 1) / alignment);
}

/* TODO: add the relevant SRGB formats to SDL_gpu */
static SDL_GpuTextureFormat XNAToSDL_SurfaceFormat[] =
{
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8,			/* SurfaceFormat.Color */
	SDL_GPU_TEXTUREFORMAT_R5G6B5,			/* SurfaceFormat.Bgr565 */
	SDL_GPU_TEXTUREFORMAT_A1R5G5B5,			/* SurfaceFormat.Bgra5551 */
	SDL_GPU_TEXTUREFORMAT_B4G4R4A4,			/* SurfaceFormat.Bgra4444 */
	SDL_GPU_TEXTUREFORMAT_BC1,			/* SurfaceFormat.Dxt1 */
	SDL_GPU_TEXTUREFORMAT_BC2,			/* SurfaceFormat.Dxt3 */
	SDL_GPU_TEXTUREFORMAT_BC3,			/* SurfaceFormat.Dxt5 */
	SDL_GPU_TEXTUREFORMAT_R8G8_SNORM,		/* SurfaceFormat.NormalizedByte2 */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,		/* SurfaceFormat.NormalizedByte4 */
	SDL_GPU_TEXTUREFORMAT_A2R10G10B10,		/* SurfaceFormat.Rgba1010102 */
	SDL_GPU_TEXTUREFORMAT_R16G16,			/* SurfaceFormat.Rg32 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16,		/* SurfaceFormat.Rgba64 */
	SDL_GPU_TEXTUREFORMAT_A8,			/* SurfaceFormat.Alpha8 */
	SDL_GPU_TEXTUREFORMAT_R32_SFLOAT,		/* SurfaceFormat.Single */
	SDL_GPU_TEXTUREFORMAT_R32G32_SFLOAT,		/* SurfaceFormat.Vector2 */
	SDL_GPU_TEXTUREFORMAT_R32G32B32A32_SFLOAT,	/* SurfaceFormat.Vector4 */
	SDL_GPU_TEXTUREFORMAT_R16_SFLOAT,		/* SurfaceFormat.HalfSingle */
	SDL_GPU_TEXTUREFORMAT_R16G16_SFLOAT,		/* SurfaceFormat.HalfVector2 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT,	/* SurfaceFormat.HalfVector4 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT,	/* SurfaceFormat.HdrBlendable */
	SDL_GPU_TEXTUREFORMAT_B8G8R8A8,			/* SurfaceFormat.ColorBgraEXT */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8,			/* FIXME SRGB */ /* SurfaceFormat.ColorSrgbEXT */
	SDL_GPU_TEXTUREFORMAT_BC3,			/* FIXME SRGB */ /* SurfaceFormat.Dxt5SrgbEXT */
	SDL_GPU_TEXTUREFORMAT_BC7,			/* SurfaceFormat.Bc7EXT */
	SDL_GPU_TEXTUREFORMAT_BC7			/* FIXME SRGB */ /* SurfaceFormat.Bc7SrgbEXT */
};

static SDL_GpuPrimitiveType XNAToSDL_PrimitiveType[] =
{
	SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,	/* FNA3D_PRIMITIVETYPE_TRIANGLELIST */
	SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP,	/* FNA3D_PRIMITIVETYPE_TRIANGLESTRIP */
	SDL_GPU_PRIMITIVETYPE_LINELIST,		/* FNA3D_PRIMITIVETYPE_LINELIST */
	SDL_GPU_PRIMITIVETYPE_LINESTRIP,	/* FNA3D_PRIMITIVETYPE_LINESTRIP */
	SDL_GPU_PRIMITIVETYPE_POINTLIST		/* FNA3D_PRIMITIVETYPE_POINTLIST_EXT */
};

static SDL_GpuIndexElementSize XNAToSDL_IndexElementSize[] =
{
	SDL_GPU_INDEXELEMENTSIZE_16BIT,	/* FNA3D_INDEXELEMENTSIZE_16BIT */
	SDL_GPU_INDEXELEMENTSIZE_32BIT	/* FNA3D_INDEXELEMENTSIZE_32BIT */
};

static SDL_GpuBlendFactor XNAToSDL_BlendFactor[] =
{
	SDL_GPU_BLENDFACTOR_ONE,			/* FNA3D_BLEND_ONE */
	SDL_GPU_BLENDFACTOR_ZERO,			/* FNA3D_BLEND_ZERO */
	SDL_GPU_BLENDFACTOR_SRC_COLOR,			/* FNA3D_BLEND_SOURCECOLOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,	/* FNA3D_BLEND_INVERSESOURCECOLOR */
	SDL_GPU_BLENDFACTOR_SRC_ALPHA,			/* FNA3D_BLEND_SOURCEALPHA */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,	/* FNA3D_BLEND_INVERSESOURCEALPHA */
	SDL_GPU_BLENDFACTOR_DST_COLOR,			/* FNA3D_BLEND_DESTINATIONCOLOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,	/* FNA3D_BLEND_INVERSEDESTINATIONCOLOR */
	SDL_GPU_BLENDFACTOR_DST_ALPHA,			/* FNA3D_BLEND_DESTINATIONALPHA */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,	/* FNA3D_BLEND_INVERSEDESTINATIONALPHA */
	SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,		/* FNA3D_BLEND_BLENDFACTOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,	/* FNA3D_BLEND_INVERSEBLENDFACTOR */
	SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE		/* FNA3D_BLEND_SOURCEALPHASATURATION */
};

static SDL_GpuBlendOp XNAToSDL_BlendOp[] =
{
	SDL_GPU_BLENDOP_ADD,			/* FNA3D_BLENDFUNCTION_ADD */
	SDL_GPU_BLENDOP_SUBTRACT,		/* FNA3D_BLENDFUNCTION_SUBTRACT */
	SDL_GPU_BLENDOP_REVERSE_SUBTRACT,	/* FNA3D_BLENDFUNCTION_REVERSESUBTRACT */
	SDL_GPU_BLENDOP_MAX,			/* FNA3D_BLENDFUNCTION_MAX */
	SDL_GPU_BLENDOP_MIN			/* FNA3D_BLENDFUNCTION_MIN */
};

static SDL_GpuPresentMode XNAToSDL_PresentMode[] =
{
	SDL_GPU_PRESENTMODE_FIFO, /* Falls back to FIFO if not supported */
	SDL_GPU_PRESENTMODE_FIFO, /* Falls back to FIFO if not supported */
	SDL_GPU_PRESENTMODE_FIFO,
	SDL_GPU_PRESENTMODE_IMMEDIATE /* FIXME: Should be mailbox -> immediate */
};

static SDL_GpuFilter XNAToSDL_MagFilter[] =
{
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuFilter XNAToSDL_MinFilter[] =
{
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuSamplerMipmapMode XNAToSDL_MipFilter[] =
{
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,	/* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,	/* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuSamplerAddressMode XNAToSDL_SamplerAddressMode[] =
{
	SDL_GPU_SAMPLERADDRESSMODE_REPEAT,		/* FNA3D_TEXTUREADDRESSMODE_WRAP */
	SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,	/* FNA3D_TEXTUREADDRESSMODE_CLAMP */
	SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT	/* FNA3D_TEXTUREADDRESSMODE_MIRROR */
};

static SDL_GpuVertexElementFormat XNAToSDL_VertexAttribType[] =
{
	SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,		/* FNA3D_VERTEXELEMENTFORMAT_SINGLE */
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR2,		/* FNA3D_VERTEXELEMENTFORMAT_VECTOR2 */
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR3,		/* FNA3D_VERTEXELEMENTFORMAT_VECTOR3 */
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR4,		/* FNA3D_VERTEXELEMENTFORMAT_VECTOR4 */
	SDL_GPU_VERTEXELEMENTFORMAT_COLOR,		/* FNA3D_VERTEXELEMENTFORMAT_COLOR */
	SDL_GPU_VERTEXELEMENTFORMAT_BYTE4,		/* FNA3D_VERTEXELEMENTFORMAT_BYTE4 */
	SDL_GPU_VERTEXELEMENTFORMAT_SHORT2,		/* FNA3D_VERTEXELEMENTFORMAT_SHORT2 */
	SDL_GPU_VERTEXELEMENTFORMAT_SHORT4,		/* FNA3D_VERTEXELEMENTFORMAT_SHORT4 */
	SDL_GPU_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2,	/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2 */
	SDL_GPU_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4,	/* FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4 */
	SDL_GPU_VERTEXELEMENTFORMAT_HALFVECTOR2,	/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR2 */
	SDL_GPU_VERTEXELEMENTFORMAT_HALFVECTOR4		/* FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4 */
};

static SDL_GpuFillMode XNAToSDL_FillMode[] =
{
	SDL_GPU_FILLMODE_FILL,	/* FNA3D_FILLMODE_SOLID */
	SDL_GPU_FILLMODE_LINE	/* FNA3D_FILLMODE_WIREFRAME */
};

static SDL_GpuCullMode XNAToSDL_CullMode[] =
{
	SDL_GPU_CULLMODE_NONE,	/* FNA3D_CULLMODE_NONE */
	SDL_GPU_CULLMODE_FRONT,	/* FNA3D_CULLMODE_CULLCLOCKWISEFACE */
	SDL_GPU_CULLMODE_BACK	/* FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE */
};

static SDL_GpuCompareOp XNAToSDL_CompareOp[] =
{
	SDL_GPU_COMPAREOP_ALWAYS,		/* FNA3D_COMPAREFUNCTION_ALWAYS */
	SDL_GPU_COMPAREOP_NEVER,		/* FNA3D_COMPAREFUNCTION_NEVER */
	SDL_GPU_COMPAREOP_LESS,			/* FNA3D_COMPAREFUNCTION_LESS */
	SDL_GPU_COMPAREOP_LESS_OR_EQUAL,	/* FNA3D_COMPAREFUNCTION_LESSEQUAL */
	SDL_GPU_COMPAREOP_EQUAL,		/* FNA3D_COMPAREFUNCTION_EQUAL */
	SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,	/* FNA3D_COMPAREFUNCTION_GREATEREQUAL */
	SDL_GPU_COMPAREOP_GREATER,		/* FNA3D_COMPAREFUNCTION_GREATER */
	SDL_GPU_COMPAREOP_NOT_EQUAL		/* FNA3D_COMPAREFUNCTION_NOTEQUAL */
};

static SDL_GpuStencilOp XNAToSDL_StencilOp[] =
{
	SDL_GPU_STENCILOP_KEEP,			/* FNA3D_STENCILOPERATION_KEEP */
	SDL_GPU_STENCILOP_ZERO,			/* FNA3D_STENCILOPERATION_ZERO */
	SDL_GPU_STENCILOP_REPLACE,		/* FNA3D_STENCILOPERATION_REPLACE */
	SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,	/* FNA3D_STENCILOPERATION_INCREMENT */
	SDL_GPU_STENCILOP_DECREMENT_AND_WRAP,	/* FNA3D_STENCILOPERATION_DECREMENT */
	SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP,	/* FNA3D_STENCILOPERATION_INCREMENTSATURATION */
	SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP,	/* FNA3D_STENCILOPERATION_DECREMENTSATURATION */
	SDL_GPU_STENCILOP_INVERT		/* FNA3D_STENCILOPERATION_INVERT */
};

/* Indirection to cleanly handle Renderbuffers */
typedef struct SDLGPU_TextureHandle /* Cast from FNA3D_Texture* */
{
	SDL_GpuTexture *texture;
	SDL_GpuTextureCreateInfo createInfo;
} SDLGPU_TextureHandle;

typedef struct SDLGPU_Renderbuffer /* Cast from FNA3D_Renderbuffer* */
{
	SDL_GpuTexture *texture;
	SDL_GpuTextureFormat format;
	SDL_GpuSampleCount sampleCount;
	uint8_t isDepth; /* if true, owns the texture reference */
} SDLGPU_Renderbuffer;

typedef struct SDLGPU_Effect /* Cast from FNA3D_Effect* */
{
	MOJOSHADER_effect *effect;
} SDLGPU_Effect;

typedef struct SDLGPU_BufferHandle /* Cast from FNA3D_Buffer* */
{
	SDL_GpuBuffer *buffer;
	uint32_t size;
} SDLGPU_BufferHandle;

typedef struct SamplerStateHashMap
{
	PackedState key;
	SDL_GpuSampler *value;
} SamplerStateHashMap;

typedef struct SamplerStateHashArray
{
	SamplerStateHashMap *elements;
	int32_t count;
	int32_t capacity;
} SamplerStateHashArray;

static inline SDL_GpuSampler* SamplerStateHashArray_Fetch(
	SamplerStateHashArray *arr,
	PackedState key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		if (	key.a == arr->elements[i].key.a &&
			key.b == arr->elements[i].key.b		)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void SamplerStateHashArray_Insert(
	SamplerStateHashArray *arr,
	PackedState key,
	SDL_GpuSampler *value
) {
	SamplerStateHashMap map;
	map.key.a = key.a;
	map.key.b = key.b;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, SamplerStateHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* FIXME: This could be packed better */
typedef struct GraphicsPipelineHash
{
	PackedState blendState;
	PackedState rasterizerState;
	PackedState depthStencilState;
	uint32_t vertexBufferBindingsIndex;
	FNA3D_PrimitiveType primitiveType;
	SDL_GpuSampleCount sampleCount;
	uint32_t sampleMask;
	SDL_GpuShaderModule *vertShader;
	SDL_GpuShaderModule *fragShader;
	SDL_GpuTextureFormat colorFormats[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorFormatCount;
	SDL_bool hasDepthStencilAttachment;
	SDL_GpuTextureFormat depthStencilFormat;
} GraphicsPipelineHash;

typedef struct GraphicsPipelineHashMap
{
	GraphicsPipelineHash key;
	SDL_GpuGraphicsPipeline *value;
} GraphicsPipelineHashMap;

typedef struct GraphicsPipelineHashArray
{
	GraphicsPipelineHashMap *elements;
	int32_t count;
	int32_t capacity;
} GraphicsPipelineHashArray;

#define NUM_PIPELINE_HASH_BUCKETS 1031

typedef struct GraphicsPipelineHashTable
{
	GraphicsPipelineHashArray buckets[NUM_PIPELINE_HASH_BUCKETS];
} GraphicsPipelineHashTable;

static inline uint64_t GraphicsPipelineHashTable_GetHashCode(GraphicsPipelineHash hash)
{
	/* The algorithm for this hashing function
	 * is taken from Josh Bloch's "Effective Java".
	 * (https://stackoverflow.com/a/113600/12492383)
	 */
	const uint64_t HASH_FACTOR = 97;
	uint32_t i;
	uint64_t result = 1;
	result = result * HASH_FACTOR + hash.blendState.a;
	result = result * HASH_FACTOR + hash.blendState.b;
	result = result * HASH_FACTOR + hash.rasterizerState.a;
	result = result * HASH_FACTOR + hash.rasterizerState.b;
	result = result * HASH_FACTOR + hash.depthStencilState.a;
	result = result * HASH_FACTOR + hash.depthStencilState.b;
	result = result * HASH_FACTOR + hash.vertexBufferBindingsIndex;
	result = result * HASH_FACTOR + hash.primitiveType;
	result = result * HASH_FACTOR + hash.sampleCount;
	result = result * HASH_FACTOR + hash.sampleMask;
	result = result * HASH_FACTOR + (uint64_t) (size_t) hash.vertShader;
	result = result * HASH_FACTOR + (uint64_t) (size_t) hash.fragShader;
	result = result * HASH_FACTOR + hash.colorFormatCount;
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		result = result * HASH_FACTOR + hash.colorFormats[i];
	}
	result = result * HASH_FACTOR + hash.hasDepthStencilAttachment;
	result = result * HASH_FACTOR + hash.depthStencilFormat;
	return result;
}

static inline SDL_GpuGraphicsPipeline *GraphicsPipelineHashTable_Fetch(
	GraphicsPipelineHashTable *table,
	GraphicsPipelineHash key
) {
	int32_t i;
	uint64_t hashcode = GraphicsPipelineHashTable_GetHashCode(key);
	GraphicsPipelineHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_HASH_BUCKETS];

	for (i = 0; i < arr->count; i += 1)
	{
		const GraphicsPipelineHash *e = &arr->elements[i].key;
		if (	key.blendState.a == e->blendState.a &&
			key.blendState.b == e->blendState.b &&
			key.rasterizerState.a == e->rasterizerState.a &&
			key.rasterizerState.b == e->rasterizerState.b &&
			key.depthStencilState.a == e->depthStencilState.a &&
			key.depthStencilState.b == e->depthStencilState.b &&
			key.vertexBufferBindingsIndex == e->vertexBufferBindingsIndex &&
			key.primitiveType == e->primitiveType &&
			key.sampleMask == e->sampleMask &&
			key.vertShader == e->vertShader &&
			key.fragShader == e->fragShader &&
			key.colorFormatCount == e->colorFormatCount &&
			key.colorFormats[0] == e->colorFormats[0] &&
			key.colorFormats[1] == e->colorFormats[1] &&
			key.colorFormats[2] == e->colorFormats[2] &&
			key.colorFormats[3] == e->colorFormats[3] &&
			key.hasDepthStencilAttachment == e->hasDepthStencilAttachment &&
			key.depthStencilFormat == e->depthStencilFormat )
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void GraphicsPipelineHashTable_Insert(
	GraphicsPipelineHashTable *table,
	GraphicsPipelineHash key,
	SDL_GpuGraphicsPipeline *value
) {
	uint64_t hashcode = GraphicsPipelineHashTable_GetHashCode(key);
	GraphicsPipelineHashArray *arr = &table->buckets[hashcode % NUM_PIPELINE_HASH_BUCKETS];
	GraphicsPipelineHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 2, GraphicsPipelineHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct ComputePipelineHashMap
{
	SDL_GpuShaderModule *key;
	SDL_GpuComputePipeline *value;
} ComputePipelineHashMap;

typedef struct ComputePipelineHashArray
{
	ComputePipelineHashMap *elements;
	int32_t count;
	int32_t capacity;
} ComputePipelineHashArray;

static inline SDL_GpuComputePipeline* ComputePipelineHashArray_Fetch(
	ComputePipelineHashArray *arr,
	SDL_GpuShaderModule *key
) {
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		if (key == arr->elements[i].key)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

static inline void ComputePipelineHashArray_Insert(
	ComputePipelineHashArray *arr,
	SDL_GpuShaderModule *key,
	SDL_GpuComputePipeline *value
) {
	ComputePipelineHashMap map;
	map.key = key;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, ComputePipelineHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct SDLGPU_Renderer
{
	SDL_GpuDevice *device;
	SDL_GpuCommandBuffer *renderCommandBuffer;
	SDL_GpuCommandBuffer *uploadCommandBuffer;

	uint8_t renderPassInProgress;
	uint8_t needNewRenderPass;

	uint8_t copyPassInProgress;

	uint8_t shouldClearColorOnBeginPass;
	uint8_t shouldClearDepthOnBeginPass;
	uint8_t shouldClearStencilOnBeginPass;

	SDL_GpuVec4 clearColorValue;
	SDL_GpuDepthStencilValue clearDepthStencilValue;

	/* Defer render pass settings */
	SDL_GpuTexture *nextRenderPassColorAttachments[MAX_RENDERTARGET_BINDINGS];
	SDL_GpuCubeMapFace nextRenderPassColorAttachmentCubeFace[MAX_RENDERTARGET_BINDINGS];
	SDL_GpuTextureFormat nextRenderPassColorAttachmentFormats[MAX_RENDERTARGET_BINDINGS];
	uint32_t nextRenderPassColorAttachmentCount;
	SDL_GpuSampleCount nextRenderPassMultisampleCount;

	SDL_GpuTexture *nextRenderPassDepthStencilAttachment; /* may be NULL */
	SDL_GpuTextureFormat nextRenderPassDepthStencilFormat;

	uint8_t renderTargetInUse;

	uint8_t needNewGraphicsPipeline;
	int32_t currentVertexBufferBindingsIndex;

	SDL_GpuGraphicsPipeline *currentGraphicsPipeline;
	MOJOSHADER_sdlShader *currentVertexShader;
	MOJOSHADER_sdlShader *currentFragmentShader;

	PackedVertexBufferBindingsArray vertexBufferBindingsCache;

	FNA3D_Viewport viewport;

	/* Vertex buffer bind settings */
	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding vertexBindings[MAX_BOUND_VERTEX_BUFFERS];
	FNA3D_VertexElement vertexElements[MAX_BOUND_VERTEX_BUFFERS][MAX_VERTEX_ATTRIBUTES];
	SDL_GpuBufferBinding vertexBufferBindings[MAX_BOUND_VERTEX_BUFFERS];
	uint8_t needVertexBufferBind;

	/* Index buffer state shadowing */
	SDL_GpuBufferBinding indexBufferBinding;

	/* Sampler bind settings */
	SDL_GpuTextureSamplerBinding vertexTextureSamplerBindings[MAX_VERTEXTEXTURE_SAMPLERS];
	uint8_t needVertexSamplerBind;

	SDL_GpuTextureSamplerBinding fragmentTextureSamplerBindings[MAX_TEXTURE_SAMPLERS];
	uint8_t needFragmentSamplerBind;

	/* Pipeline state */
	FNA3D_BlendState fnaBlendState;
	FNA3D_RasterizerState fnaRasterizerState;
	FNA3D_DepthStencilState fnaDepthStencilState;
	FNA3D_PrimitiveType fnaPrimitiveType;
	float blendConstants[4];
	SDL_GpuSampleCount multisampleCount;
	uint32_t multisampleMask;
	uint32_t stencilReference;
	SDL_GpuRect scissorRect;
	SDL_GpuTextureFormat colorAttachmentFormats[MAX_RENDERTARGET_BINDINGS];
	uint32_t colorAttachmentCount;
	uint8_t hasDepthStencilAttachment;
	SDL_GpuTextureFormat depthStencilFormat;

	/* Presentation structure */

	void *mainWindowHandle;
	SDL_GpuTexture *fauxBackbufferColor;
	SDL_GpuTexture *fauxBackbufferDepthStencil; /* may be NULL */
	uint32_t fauxBackbufferWidth;
	uint32_t fauxBackbufferHeight;
	FNA3D_SurfaceFormat fauxBackbufferColorFormat; /* for reading back */
	FNA3D_DepthFormat fauxBackbufferDepthStencilFormat; /* for reading back */
	int32_t fauxBackbufferSampleCount; /* for reading back */

	/* Transfer structure */

	SDL_GpuTransferBuffer *textureDownloadBuffer;
	uint32_t textureDownloadBufferSize;

	SDL_GpuTransferBuffer *bufferDownloadBuffer;
	uint32_t bufferDownloadBufferSize;

	SDL_GpuTransferBuffer *textureUploadBuffer;
	uint32_t textureUploadBufferSize;
	uint32_t textureUploadBufferOffset;

	SDL_GpuTransferBuffer *bufferUploadBuffer;
	uint32_t bufferUploadBufferSize;
	uint32_t bufferUploadBufferOffset;

	/* Synchronization */

	SDL_GpuFence *fences[MAX_FRAMES_IN_FLIGHT];
	uint8_t frameCounter;

	/* Hashing */

	GraphicsPipelineHashTable graphicsPipelineHashTable;
	SamplerStateHashArray samplerStateArray;

	/* MOJOSHADER */

	MOJOSHADER_sdlContext *mojoshaderContext;
	MOJOSHADER_effect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;

	/* Shader extension */

	uint8_t graphicsShaderExtensionInUse;
	SDL_GpuShaderModule *extensionVertexShaderModule;
	SDL_GpuShaderModule *extensionFragmentShaderModule;
	SDL_GpuGraphicsShaderInfo extensionVertexShaderInfo;
	SDL_GpuGraphicsShaderInfo extensionFragmentShaderInfo;

	ComputePipelineHashArray computePipelineHashArray;

	SDL_GpuComputeShaderInfo extensionComputeShaderInfo;
	SDL_GpuComputePipeline *currentComputePipeline;

	/* Capabilities */
	uint8_t supportsBaseVertex;
} SDLGPU_Renderer;

/* Statics */

static SDL_GpuBackend preferredBackends[2] = { SDL_GPU_BACKEND_VULKAN, SDL_GPU_BACKEND_D3D11 };
static FNA3D_PresentationParameters requestedPresentationParameters;

/* Submission / Presentation */

static void SDLGPU_ResetCommandBufferState(
	SDLGPU_Renderer *renderer
) {
	renderer->renderCommandBuffer = SDL_GpuAcquireCommandBuffer(renderer->device);
	renderer->uploadCommandBuffer = SDL_GpuAcquireCommandBuffer(renderer->device);

	/* Reset state */
	renderer->needNewRenderPass = 1;
	renderer->needNewGraphicsPipeline = 1;
	renderer->needVertexBufferBind = 1;
	renderer->needVertexSamplerBind = 1;
	renderer->needFragmentSamplerBind = 1;

	renderer->textureUploadBufferOffset = 0;
	renderer->bufferUploadBufferOffset = 0;
}

static void SDLGPU_INTERNAL_EndCopyPass(
	SDLGPU_Renderer *renderer
) {
	if (renderer->copyPassInProgress)
	{
		SDL_GpuEndCopyPass(
			renderer->device,
			renderer->uploadCommandBuffer
		);
	}

	renderer->copyPassInProgress = 0;
}

static void SDLGPU_INTERNAL_EndRenderPass(
	SDLGPU_Renderer *renderer
) {
	if (renderer->renderPassInProgress)
	{
		SDL_GpuEndRenderPass(
			renderer->device,
			renderer->renderCommandBuffer
		);
	}

	renderer->renderPassInProgress = 0;
	renderer->needNewRenderPass = 1;
	renderer->currentGraphicsPipeline = NULL;
	renderer->needNewGraphicsPipeline = 1;
}

static SDL_GpuFence* SDLGPU_INTERNAL_FlushCommandsAndAcquireFence(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuFence *fence;

	SDLGPU_INTERNAL_EndCopyPass(renderer);
	SDLGPU_INTERNAL_EndRenderPass(renderer);

	SDL_GpuSubmit(renderer->device, renderer->uploadCommandBuffer);

	fence = SDL_GpuSubmitAndAcquireFence(
		renderer->device,
		renderer->renderCommandBuffer
	);

	SDLGPU_ResetCommandBufferState(renderer);

	return fence;
}

static void SDLGPU_INTERNAL_FlushCommands(
	SDLGPU_Renderer *renderer
) {
	SDLGPU_INTERNAL_EndCopyPass(renderer);
	SDLGPU_INTERNAL_EndRenderPass(renderer);
	SDL_GpuSubmit(renderer->device, renderer->uploadCommandBuffer);
	SDL_GpuSubmit(renderer->device, renderer->renderCommandBuffer);
	SDLGPU_ResetCommandBufferState(renderer);
}

static void SDLGPU_INTERNAL_FlushCommandsAndStall(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuFence *fence = SDLGPU_INTERNAL_FlushCommandsAndAcquireFence(
		renderer
	);

	SDL_GpuWaitForFences(
		renderer->device,
		1,
		1,
		&fence
	);

	SDL_GpuReleaseFence(
		renderer->device,
		fence
	);

	SDLGPU_ResetCommandBufferState(renderer);
}

/* FIXME: this will break with multi-window, need a claim/unclaim structure */
static void SDLGPU_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuTexture *swapchainTexture;
	SDL_GpuTextureRegion srcRegion;
	SDL_GpuTextureRegion dstRegion;
	uint32_t width, height;

	SDLGPU_INTERNAL_EndCopyPass(renderer);
	SDLGPU_INTERNAL_EndRenderPass(renderer);

	if (renderer->fences[renderer->frameCounter] != NULL)
	{
		/* Wait for the least-recent fence */
		SDL_GpuWaitForFences(
			renderer->device,
			1,
			1,
			&renderer->fences[renderer->frameCounter]
		);

		SDL_GpuReleaseFence(
			renderer->device,
			renderer->fences[renderer->frameCounter]
		);

		renderer->fences[renderer->frameCounter] = NULL;
	}

	swapchainTexture = SDL_GpuAcquireSwapchainTexture(
		renderer->device,
		renderer->renderCommandBuffer,
		overrideWindowHandle,
		&width,
		&height
	);

	if (swapchainTexture != NULL)
	{
		srcRegion.textureSlice.texture = renderer->fauxBackbufferColor;
		srcRegion.textureSlice.layer = 0;
		srcRegion.textureSlice.mipLevel = 0;
		srcRegion.x = 0;
		srcRegion.y = 0;
		srcRegion.z = 0;
		srcRegion.w = width;
		srcRegion.h = height;
		srcRegion.d = 1;

		dstRegion.textureSlice.texture = swapchainTexture;
		dstRegion.textureSlice.layer = 0;
		dstRegion.textureSlice.mipLevel = 0;
		dstRegion.x = 0;
		dstRegion.y = 0;
		dstRegion.z = 0;
		dstRegion.w = width;
		dstRegion.h = height;
		dstRegion.d = 1;

        SDL_GpuBlit(
            renderer->device,
            renderer->renderCommandBuffer,
            &srcRegion,
            &dstRegion,
            SDL_GPU_FILTER_LINEAR,
            SDL_FALSE
        );
    }

	renderer->fences[renderer->frameCounter] =
		SDLGPU_INTERNAL_FlushCommandsAndAcquireFence(renderer);

	renderer->frameCounter =
		(renderer->frameCounter + 1) % MAX_FRAMES_IN_FLIGHT;
}

/* Transfer */

static void SDLGPU_INTERNAL_GetTextureData(
	SDLGPU_Renderer *renderer,
	SDL_GpuTexture *texture,
	uint32_t x,
	uint32_t y,
	uint32_t z,
	uint32_t w,
	uint32_t h,
	uint32_t d,
	uint32_t layer,
	uint32_t level,
	void* data,
	uint32_t dataLength
) {
	SDL_GpuTextureRegion region;
	SDL_GpuBufferImageCopy textureCopyParams;
	SDL_GpuBufferCopy bufferCopyParams;

	/* Flush and stall so the data is up to date */
	SDLGPU_INTERNAL_FlushCommandsAndStall(renderer);

	/* Create transfer buffer if necessary */
	if (renderer->textureDownloadBuffer == NULL)
	{
		renderer->textureDownloadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_TEXTURE,
			dataLength
		);

		renderer->textureDownloadBufferSize = dataLength;
	}
	else if (renderer->textureDownloadBufferSize < dataLength)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->textureDownloadBuffer
		);

		renderer->textureDownloadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_TEXTURE,
			dataLength
		);

		renderer->textureDownloadBufferSize = dataLength;
	}

	/* Set up texture download */
	region.textureSlice.texture = renderer->fauxBackbufferColor;
	region.textureSlice.mipLevel = level;
	region.textureSlice.layer = layer;
	region.x = x;
	region.y = y;
	region.z = z;
	region.w = w;
	region.h = h;
	region.d = d;

	/* All zeroes, assume tight packing */
	textureCopyParams.bufferImageHeight = 0;
	textureCopyParams.bufferOffset = 0;
	textureCopyParams.bufferStride = 0;

	SDL_GpuDownloadFromTexture(
		renderer->device,
		&region,
		renderer->textureDownloadBuffer,
		&textureCopyParams,
		SDL_TRUE
	);

	/* Copy into data pointer */
	bufferCopyParams.srcOffset = 0;
	bufferCopyParams.dstOffset = 0;
	bufferCopyParams.size = dataLength;

	SDL_GpuGetTransferData(
		renderer->device,
		renderer->textureDownloadBuffer,
		data,
		&bufferCopyParams
	);
}

/* Drawing */

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

	SDLGPU_INTERNAL_EndRenderPass(renderer);

	/* Reset attachment formats to a reasonable default */
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		renderer->colorAttachmentFormats[i] = SDL_GPU_TEXTUREFORMAT_R8G8B8A8;
	}
	renderer->depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

	/* Set up the next render pass */
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

		colorAttachmentInfos[i].cycle =
			colorAttachmentInfos[i].loadOp == SDL_GPU_LOADOP_LOAD ?
				SDL_FALSE :
				SDL_TRUE; /* cycle if we can, it's fast! */

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

		renderer->colorAttachmentFormats[i] = renderer->nextRenderPassColorAttachmentFormats[i];
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
		depthStencilAttachmentInfo.stencilStoreOp = SDL_GPU_STOREOP_STORE;

		depthStencilAttachmentInfo.cycle =
			depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD || depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD ?
				SDL_FALSE :
				SDL_TRUE; /* Cycle if we can! */

		if (renderer->shouldClearDepthOnBeginPass || renderer->shouldClearStencilOnBeginPass)
		{
			depthStencilAttachmentInfo.depthStencilClearValue = renderer->clearDepthStencilValue;
		}

		renderer->depthStencilFormat = renderer->nextRenderPassDepthStencilFormat;
	}

	renderer->colorAttachmentCount = renderer->nextRenderPassColorAttachmentCount;
	renderer->multisampleCount = renderer->nextRenderPassMultisampleCount;
	renderer->hasDepthStencilAttachment = renderer->nextRenderPassDepthStencilAttachment != NULL;

	SDL_GpuBeginRenderPass(
		renderer->device,
		renderer->renderCommandBuffer,
		colorAttachmentInfos,
		renderer->nextRenderPassColorAttachmentCount,
		renderer->nextRenderPassDepthStencilAttachment != NULL ? &depthStencilAttachmentInfo : NULL
	);

	renderer->needNewRenderPass = 0;

	renderer->shouldClearColorOnBeginPass = 0;
	renderer->shouldClearDepthOnBeginPass = 0;
	renderer->shouldClearStencilOnBeginPass = 0;

	renderer->needNewGraphicsPipeline = 1;

	renderer->renderPassInProgress = 1;
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
	int32_t i;

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
		renderer->nextRenderPassColorAttachments[0] = renderer->fauxBackbufferColor;
		renderer->nextRenderPassColorAttachmentCubeFace[0] = 0;
		renderer->nextRenderPassColorAttachmentFormats[0] = XNAToSDL_SurfaceFormat[renderer->fauxBackbufferColorFormat];
		renderer->nextRenderPassMultisampleCount = XNAToSDL_SampleCount(renderer->fauxBackbufferSampleCount);
		renderer->nextRenderPassColorAttachmentCount = 1;

		renderer->nextRenderPassDepthStencilAttachment = renderer->fauxBackbufferDepthStencil;
		renderer->nextRenderPassDepthStencilFormat = XNAToSDL_DepthFormat(renderer->fauxBackbufferDepthStencilFormat);

		renderer->renderTargetInUse = 0;
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
				renderer->nextRenderPassColorAttachments[i] = ((SDLGPU_Renderbuffer*) renderTargets[i].colorBuffer)->texture;
				renderer->nextRenderPassColorAttachmentFormats[i] = ((SDLGPU_Renderbuffer*) renderTargets[i].colorBuffer)->format;
				renderer->nextRenderPassMultisampleCount = ((SDLGPU_Renderbuffer*) renderTargets[i].colorBuffer)->sampleCount;
			}
			else
			{
				renderer->nextRenderPassColorAttachments[i] = ((SDLGPU_TextureHandle*) renderTargets[i].texture)->texture;
				renderer->nextRenderPassColorAttachmentFormats[i] = ((SDLGPU_TextureHandle*) renderTargets[i].texture)->createInfo.format;
				renderer->nextRenderPassMultisampleCount = SDL_GPU_SAMPLECOUNT_1;
			}
		}

		renderer->nextRenderPassColorAttachmentCount = numRenderTargets;
		renderer->renderTargetInUse = 1;
	}

	if (depthStencilBuffer != NULL)
	{
		renderer->nextRenderPassDepthStencilAttachment = ((SDLGPU_Renderbuffer*) depthStencilBuffer)->texture;
		renderer->nextRenderPassDepthStencilFormat = XNAToSDL_DepthFormat(depthFormat);
	}

	renderer->needNewRenderPass = 1;
}

static void SDLGPU_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	/* no-op? SDL_gpu auto-resolves MSAA targets */
}

static void SDLGPU_INTERNAL_GenerateVertexInputInfo(
	SDLGPU_Renderer *renderer,
	SDL_GpuVertexBinding *bindings,
	SDL_GpuVertexAttribute *attributes,
	uint32_t *attributeCount
) {
	MOJOSHADER_sdlShader *vertexShader, *blah;
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];
	uint32_t attributeDescriptionCounter = 0;
	int32_t i, j, k;
	FNA3D_VertexDeclaration vertexDeclaration;
	FNA3D_VertexElement element;
	FNA3D_VertexElementUsage usage;
	int32_t index, attribLoc;

	MOJOSHADER_sdlGetBoundShaders(renderer->mojoshaderContext, &vertexShader, &blah);

	SDL_memset(attrUse, '\0', sizeof(attrUse));
	for (i = 0; i < (int32_t) renderer->numVertexBindings; i += 1)
	{
		vertexDeclaration =
			renderer->vertexBindings[i].vertexDeclaration;

		for (j = 0; j < vertexDeclaration.elementCount; j += 1)
		{
			element = vertexDeclaration.elements[j];
			usage = element.vertexElementUsage;
			index = element.usageIndex;

			if (attrUse[usage][index])
			{
				index = -1;

				for (k = 0; k < MAX_VERTEX_ATTRIBUTES; k += 1)
				{
					if (!attrUse[usage][k])
					{
						index = k;
						break;
					}
				}

				if (index < 0)
				{
					FNA3D_LogError("Vertex usage collision!");
				}
			}

			attrUse[usage][index] = 1;

			attribLoc = MOJOSHADER_sdlGetVertexAttribLocation(
				vertexShader,
				VertexAttribUsage(usage),
				index
			);

			if (attribLoc == -1)
			{
				/* Stream not in use! */
				continue;
			}

			attributes[attributeDescriptionCounter].location = attribLoc;
			attributes[attributeDescriptionCounter].format = XNAToSDL_VertexAttribType[
				element.vertexElementFormat
			];
			attributes[attributeDescriptionCounter].offset = element.offset;
			attributes[attributeDescriptionCounter].binding = i;

			attributeDescriptionCounter += 1;
		}

		bindings[i].binding = i;
		bindings[i].stride = vertexDeclaration.vertexStride;

		if (renderer->vertexBindings[i].instanceFrequency > 0)
		{
			bindings[i].inputRate =
				SDL_GPU_VERTEXINPUTRATE_INSTANCE;
			bindings[i].stepRate = renderer->vertexBindings[i].instanceFrequency;
		}
		else
		{
			bindings[i].inputRate =
				SDL_GPU_VERTEXINPUTRATE_VERTEX;
			bindings[i].stepRate = 0; /* should be ignored */
		}
	}

	*attributeCount = attributeDescriptionCounter;
}

static SDL_GpuGraphicsPipeline* SDLGPU_INTERNAL_FetchGraphicsPipeline(
	SDLGPU_Renderer *renderer
) {
	MOJOSHADER_sdlShader *mojoshaderVertShader, *mojoshaderFragShader;
	SDL_GpuShaderModule *vertShaderModule, *fragShaderModule;
	GraphicsPipelineHash hash;
	SDL_GpuGraphicsPipeline *pipeline;
	SDL_GpuGraphicsPipelineCreateInfo createInfo;
	SDL_GpuColorAttachmentDescription colorAttachmentDescriptions[MAX_RENDERTARGET_BINDINGS];
	SDL_GpuVertexBinding *vertexBindings;
	SDL_GpuVertexAttribute *vertexAttributes;

	hash.blendState = GetPackedBlendState(renderer->fnaBlendState);
	hash.rasterizerState = GetPackedRasterizerState(
		renderer->fnaRasterizerState,
		renderer->fnaRasterizerState.depthBias * XNAToSDL_DepthBiasScale(
			renderer->depthStencilFormat
		)
	);
	hash.depthStencilState = GetPackedDepthStencilState(
		renderer->fnaDepthStencilState
	);
	hash.vertexBufferBindingsIndex = renderer->currentVertexBufferBindingsIndex;
	hash.primitiveType = renderer->fnaPrimitiveType;
	hash.sampleCount = renderer->multisampleCount;
	hash.sampleMask = renderer->multisampleMask;

	if (renderer->graphicsShaderExtensionInUse)
	{
		hash.fragShader = renderer->extensionVertexShaderInfo.shaderModule;
		hash.vertShader = renderer->extensionFragmentShaderInfo.shaderModule;
	}
	else
	{
		MOJOSHADER_sdlGetShaderModules(
			renderer->mojoshaderContext,
			&vertShaderModule,
			&fragShaderModule
		);
		hash.vertShader = vertShaderModule;
		hash.fragShader = fragShaderModule;
	}

	hash.colorFormatCount = renderer->colorAttachmentCount;
	hash.colorFormats[0] = renderer->colorAttachmentFormats[0];
	hash.colorFormats[1] = renderer->colorAttachmentFormats[1];
	hash.colorFormats[2] = renderer->colorAttachmentFormats[2];
	hash.colorFormats[3] = renderer->colorAttachmentFormats[3];

	hash.depthStencilFormat = renderer->depthStencilFormat;
	hash.hasDepthStencilAttachment = renderer->hasDepthStencilAttachment;

	pipeline = GraphicsPipelineHashTable_Fetch(
		&renderer->graphicsPipelineHashTable,
		hash
	);

	if (pipeline != NULL)
	{
		return pipeline;
	}

	createInfo.primitiveType = XNAToSDL_PrimitiveType[renderer->fnaPrimitiveType];

	/* Vertex Input State */

	vertexBindings = SDL_malloc(
		renderer->numVertexBindings *
		sizeof(SDL_GpuVertexBinding)
	);
	vertexAttributes = SDL_malloc(
		renderer->numVertexBindings *
		MAX_VERTEX_ATTRIBUTES *
		sizeof(SDL_GpuVertexAttribute)
	);

	SDLGPU_INTERNAL_GenerateVertexInputInfo(
		renderer,
		vertexBindings,
		vertexAttributes,
		&createInfo.vertexInputState.vertexAttributeCount
	);

	createInfo.vertexInputState.vertexBindings = vertexBindings;
	createInfo.vertexInputState.vertexBindingCount = renderer->numVertexBindings;
	createInfo.vertexInputState.vertexAttributes = vertexAttributes;

	/* Rasterizer */

	createInfo.rasterizerState.cullMode = XNAToSDL_CullMode[renderer->fnaRasterizerState.cullMode];
	createInfo.rasterizerState.depthBiasClamp = 0.0f;
	createInfo.rasterizerState.depthBiasConstantFactor = 0.0f;
	createInfo.rasterizerState.depthBiasEnable = 1;
	createInfo.rasterizerState.depthBiasSlopeFactor = 0.0f;
	createInfo.rasterizerState.fillMode = XNAToSDL_FillMode[renderer->fnaRasterizerState.fillMode];
	createInfo.rasterizerState.frontFace = SDL_GPU_FRONTFACE_CLOCKWISE;

	/* Multisample */

	createInfo.multisampleState.multisampleCount = renderer->multisampleCount;
	createInfo.multisampleState.sampleMask = renderer->multisampleMask;

	/* Blend State */

	colorAttachmentDescriptions[0].blendState.blendEnable = !(
		renderer->fnaBlendState.colorSourceBlend == FNA3D_BLEND_ONE &&
		renderer->fnaBlendState.colorDestinationBlend == FNA3D_BLEND_ZERO &&
		renderer->fnaBlendState.alphaSourceBlend == FNA3D_BLEND_ONE &&
		renderer->fnaBlendState.alphaDestinationBlend == FNA3D_BLEND_ZERO
	);
	if (colorAttachmentDescriptions[0].blendState.blendEnable)
	{
		colorAttachmentDescriptions[0].blendState.srcColorBlendFactor = XNAToSDL_BlendFactor[
			renderer->fnaBlendState.colorSourceBlend
		];
		colorAttachmentDescriptions[0].blendState.srcAlphaBlendFactor = XNAToSDL_BlendFactor[
			renderer->fnaBlendState.alphaSourceBlend
		];
		colorAttachmentDescriptions[0].blendState.dstColorBlendFactor = XNAToSDL_BlendFactor[
			renderer->fnaBlendState.colorDestinationBlend
		];
		colorAttachmentDescriptions[0].blendState.dstAlphaBlendFactor = XNAToSDL_BlendFactor[
			renderer->fnaBlendState.alphaDestinationBlend
		];

		colorAttachmentDescriptions[0].blendState.colorBlendOp = XNAToSDL_BlendOp[
			renderer->fnaBlendState.colorBlendFunction
		];
		colorAttachmentDescriptions[0].blendState.alphaBlendOp = XNAToSDL_BlendOp[
			renderer->fnaBlendState.alphaBlendFunction
		];
	}
	else
	{
		colorAttachmentDescriptions[0].blendState.srcColorBlendFactor = SDL_GPU_BLENDFACTOR_ONE;
		colorAttachmentDescriptions[0].blendState.srcAlphaBlendFactor = SDL_GPU_BLENDFACTOR_ONE;
		colorAttachmentDescriptions[0].blendState.dstColorBlendFactor = SDL_GPU_BLENDFACTOR_ZERO;
		colorAttachmentDescriptions[0].blendState.dstAlphaBlendFactor = SDL_GPU_BLENDFACTOR_ZERO;
		colorAttachmentDescriptions[0].blendState.colorBlendOp = SDL_GPU_BLENDOP_ADD;
		colorAttachmentDescriptions[0].blendState.alphaBlendOp = SDL_GPU_BLENDOP_ADD;
	}

	colorAttachmentDescriptions[1].blendState = colorAttachmentDescriptions[0].blendState;
	colorAttachmentDescriptions[2].blendState = colorAttachmentDescriptions[0].blendState;
	colorAttachmentDescriptions[3].blendState = colorAttachmentDescriptions[0].blendState;

	colorAttachmentDescriptions[0].blendState.colorWriteMask =
		renderer->fnaBlendState.colorWriteEnable;
	colorAttachmentDescriptions[1].blendState.colorWriteMask =
		renderer->fnaBlendState.colorWriteEnable1;
	colorAttachmentDescriptions[2].blendState.colorWriteMask =
		renderer->fnaBlendState.colorWriteEnable2;
	colorAttachmentDescriptions[3].blendState.colorWriteMask =
		renderer->fnaBlendState.colorWriteEnable3;

	colorAttachmentDescriptions[0].format = renderer->colorAttachmentFormats[0];
	colorAttachmentDescriptions[1].format = renderer->colorAttachmentFormats[1];
	colorAttachmentDescriptions[2].format = renderer->colorAttachmentFormats[2];
	colorAttachmentDescriptions[3].format = renderer->colorAttachmentFormats[3];

	createInfo.attachmentInfo.colorAttachmentCount = renderer->colorAttachmentCount;
	createInfo.attachmentInfo.colorAttachmentDescriptions = colorAttachmentDescriptions;
	createInfo.attachmentInfo.hasDepthStencilAttachment = renderer->hasDepthStencilAttachment;
	createInfo.attachmentInfo.depthStencilFormat = renderer->depthStencilFormat;

	createInfo.blendConstants[0] = renderer->blendConstants[0];
	createInfo.blendConstants[1] = renderer->blendConstants[1];
	createInfo.blendConstants[2] = renderer->blendConstants[2];
	createInfo.blendConstants[3] = renderer->blendConstants[3];

	/* Depth Stencil */

	createInfo.depthStencilState.depthTestEnable =
		renderer->fnaDepthStencilState.depthBufferEnable;
	createInfo.depthStencilState.depthWriteEnable =
		renderer->fnaDepthStencilState.depthBufferWriteEnable;
	createInfo.depthStencilState.compareOp = XNAToSDL_CompareOp[
		renderer->fnaDepthStencilState.depthBufferFunction
	];
	createInfo.depthStencilState.stencilTestEnable =
		renderer->fnaDepthStencilState.stencilEnable;

	createInfo.depthStencilState.frontStencilState.compareOp = XNAToSDL_CompareOp[
		renderer->fnaDepthStencilState.stencilFunction
	];
	createInfo.depthStencilState.frontStencilState.depthFailOp = XNAToSDL_StencilOp[
		renderer->fnaDepthStencilState.stencilDepthBufferFail
	];
	createInfo.depthStencilState.frontStencilState.failOp = XNAToSDL_StencilOp[
		renderer->fnaDepthStencilState.stencilFail
	];
	createInfo.depthStencilState.frontStencilState.passOp = XNAToSDL_StencilOp[
		renderer->fnaDepthStencilState.stencilPass
	];

	if (renderer->fnaDepthStencilState.twoSidedStencilMode)
	{
		createInfo.depthStencilState.backStencilState.compareOp = XNAToSDL_CompareOp[
			renderer->fnaDepthStencilState.ccwStencilFunction
		];
		createInfo.depthStencilState.backStencilState.depthFailOp = XNAToSDL_StencilOp[
			renderer->fnaDepthStencilState.ccwStencilDepthBufferFail
		];
		createInfo.depthStencilState.backStencilState.failOp = XNAToSDL_StencilOp[
			renderer->fnaDepthStencilState.ccwStencilFail
		];
		createInfo.depthStencilState.backStencilState.passOp = XNAToSDL_StencilOp[
			renderer->fnaDepthStencilState.ccwStencilPass
		];
	}
	else
	{
		createInfo.depthStencilState.backStencilState = createInfo.depthStencilState.frontStencilState;
	}

	createInfo.depthStencilState.compareMask =
		renderer->fnaDepthStencilState.stencilMask;
	createInfo.depthStencilState.writeMask =
		renderer->fnaDepthStencilState.stencilWriteMask;
	createInfo.depthStencilState.reference =
		renderer->fnaDepthStencilState.referenceStencil;

	createInfo.depthStencilState.depthBoundsTestEnable = 0;
	createInfo.depthStencilState.minDepthBounds = 0;
	createInfo.depthStencilState.maxDepthBounds = 0;

	/* Shaders */

	if (renderer->graphicsShaderExtensionInUse)
	{
		createInfo.vertexShaderInfo = renderer->extensionVertexShaderInfo;
		createInfo.fragmentShaderInfo = renderer->extensionFragmentShaderInfo;
	}
	else
	{
		MOJOSHADER_sdlGetBoundShaders(
			renderer->mojoshaderContext,
			&mojoshaderVertShader,
			&mojoshaderFragShader
		);

		MOJOSHADER_sdlGetShaderModules(
			renderer->mojoshaderContext,
			&createInfo.vertexShaderInfo.shaderModule,
			&createInfo.fragmentShaderInfo.shaderModule
		);

		createInfo.vertexShaderInfo.entryPointName = MOJOSHADER_sdlGetShaderParseData(mojoshaderVertShader)->mainfn;
		createInfo.vertexShaderInfo.samplerBindingCount = (uint32_t) MOJOSHADER_sdlGetShaderParseData(mojoshaderVertShader)->sampler_count;
		createInfo.vertexShaderInfo.uniformBufferSize = MOJOSHADER_sdlGetUniformBufferSize(mojoshaderVertShader);

		createInfo.fragmentShaderInfo.entryPointName = MOJOSHADER_sdlGetShaderParseData(mojoshaderFragShader)->mainfn;
		createInfo.fragmentShaderInfo.samplerBindingCount = (uint32_t) MOJOSHADER_sdlGetShaderParseData(mojoshaderFragShader)->sampler_count;
		createInfo.fragmentShaderInfo.uniformBufferSize = MOJOSHADER_sdlGetUniformBufferSize(mojoshaderFragShader);
	}

	/* Finally, after 1000 years, create the pipeline! */

	pipeline = SDL_GpuCreateGraphicsPipeline(
		renderer->device,
		&createInfo
	);

	SDL_free(vertexBindings);
	SDL_free(vertexAttributes);

	if (pipeline == NULL)
	{
		FNA3D_LogError("Failed to create graphics pipeline!");
	}

	GraphicsPipelineHashTable_Insert(
		&renderer->graphicsPipelineHashTable,
		hash,
		pipeline
	);

	return pipeline;
}

static void SDLGPU_INTERNAL_BindGraphicsPipeline(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuGraphicsPipeline *pipeline;
	MOJOSHADER_sdlShader *vertShader, *fragShader;

	if (renderer->graphicsShaderExtensionInUse)
	{
		if (
			!renderer->needNewGraphicsPipeline &&
			renderer->extensionVertexShaderModule == renderer->extensionVertexShaderInfo.shaderModule &&
			renderer->extensionFragmentShaderModule == renderer->extensionFragmentShaderInfo.shaderModule
		) {
			return;
		}
	}
	else
	{
		MOJOSHADER_sdlGetBoundShaders(
			renderer->mojoshaderContext,
			&vertShader,
			&fragShader
		);

		if (
			!renderer->needNewGraphicsPipeline &&
			renderer->currentVertexShader == vertShader &&
			renderer->currentFragmentShader == fragShader
		) {
			return;
		}
	}

	pipeline = SDLGPU_INTERNAL_FetchGraphicsPipeline(renderer);

	if (pipeline != renderer->currentGraphicsPipeline)
	{
		SDL_GpuBindGraphicsPipeline(
			renderer->device,
			renderer->renderCommandBuffer,
			pipeline
		);

		renderer->currentGraphicsPipeline = pipeline;
	}

	MOJOSHADER_sdlUpdateUniformBuffers(
		renderer->mojoshaderContext,
		renderer->renderCommandBuffer
	);

	renderer->currentVertexShader = vertShader;
	renderer->currentFragmentShader = fragShader;

	/* Reset deferred binding state */
	renderer->needNewGraphicsPipeline = 0;
	renderer->needFragmentSamplerBind = 1;
	renderer->needVertexSamplerBind = 1;
	renderer->needVertexBufferBind = 1;
	renderer->indexBufferBinding.gpuBuffer = NULL;
}

static SDL_GpuSampler* SDLGPU_INTERNAL_FetchSamplerState(
	SDLGPU_Renderer *renderer,
	FNA3D_SamplerState *samplerState
) {
	SDL_GpuSamplerStateCreateInfo samplerCreateInfo;
	SDL_GpuSampler *sampler;

	PackedState hash = GetPackedSamplerState(*samplerState);
	sampler = SamplerStateHashArray_Fetch(
		&renderer->samplerStateArray,
		hash
	);
	if (sampler != NULL)
	{
		return sampler;
	}

	samplerCreateInfo.magFilter = XNAToSDL_MagFilter[samplerState->filter];
	samplerCreateInfo.minFilter = XNAToSDL_MinFilter[samplerState->filter];
	samplerCreateInfo.mipmapMode = XNAToSDL_MipFilter[samplerState->filter];
	samplerCreateInfo.addressModeU = XNAToSDL_SamplerAddressMode[
		samplerState->addressU
	];
	samplerCreateInfo.addressModeV = XNAToSDL_SamplerAddressMode[
		samplerState->addressV
	];
	samplerCreateInfo.addressModeW = XNAToSDL_SamplerAddressMode[
		samplerState->addressW
	];

	samplerCreateInfo.mipLodBias = samplerState->mipMapLevelOfDetailBias;
	samplerCreateInfo.anisotropyEnable = (samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC);
	samplerCreateInfo.maxAnisotropy = (float) SDL_max(1, samplerState->maxAnisotropy);
	samplerCreateInfo.compareEnable = 0;
	samplerCreateInfo.compareOp = 0;
	samplerCreateInfo.minLod = (float) samplerState->maxMipLevel;
	samplerCreateInfo.maxLod = 1000.0f;
	samplerCreateInfo.borderColor = SDL_GPU_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK;

	sampler = SDL_GpuCreateSampler(
		renderer->device,
		&samplerCreateInfo
	);

	if (sampler == NULL)
	{
		FNA3D_LogError("Failed to create sampler!");
		return NULL;
	}

	SamplerStateHashArray_Insert(
		&renderer->samplerStateArray,
		hash,
		sampler
	);

	return sampler;
}

static void SDLGPU_VerifyVertexSampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
	SDL_GpuSampler *gpuSampler;

	if (texture == NULL)
	{
		if (renderer->vertexTextureSamplerBindings[index].texture != NULL)
		{
			renderer->vertexTextureSamplerBindings[index].texture = NULL;
			renderer->vertexTextureSamplerBindings[index].sampler = NULL;
			renderer->needVertexSamplerBind = 1;
		}

		return;
	}

	if (textureHandle->texture != renderer->vertexTextureSamplerBindings[index].texture)
	{
		renderer->vertexTextureSamplerBindings[index].texture = textureHandle->texture;
		renderer->needVertexSamplerBind = 1;
	}

	gpuSampler = SDLGPU_INTERNAL_FetchSamplerState(
		renderer,
		sampler
	);

	if (gpuSampler != renderer->vertexTextureSamplerBindings[index].sampler)
	{
		renderer->vertexTextureSamplerBindings[index].sampler = gpuSampler;
		renderer->needVertexSamplerBind = 1;
	}
}

static void SDLGPU_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
	SDL_GpuSampler *gpuSampler;

	if (texture == NULL)
	{
		if (renderer->fragmentTextureSamplerBindings[index].texture != NULL)
		{
			renderer->fragmentTextureSamplerBindings[index].texture = NULL;
			renderer->fragmentTextureSamplerBindings[index].sampler = NULL;
			renderer->needFragmentSamplerBind = 1;
		}

		return;
	}

	if (textureHandle->texture != renderer->fragmentTextureSamplerBindings[index].texture)
	{
		renderer->fragmentTextureSamplerBindings[index].texture = textureHandle->texture;
		renderer->needFragmentSamplerBind = 1;
	}

	gpuSampler = SDLGPU_INTERNAL_FetchSamplerState(
		renderer,
		sampler
	);

	if (gpuSampler != renderer->fragmentTextureSamplerBindings[index].sampler)
	{
		renderer->fragmentTextureSamplerBindings[index].sampler = gpuSampler;
		renderer->needFragmentSamplerBind = 1;
	}
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

	if (renderer->supportsBaseVertex)
	{
		baseVertex = 0;
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
				dst->vertexDeclaration.elements,
				src->vertexDeclaration.elements,
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
		renderer->vertexBufferBindings[i].gpuBuffer = ((SDLGPU_BufferHandle*) bindings[i].vertexBuffer)->buffer;
		renderer->vertexBufferBindings[i].offset = (bindings[i].vertexOffset + baseVertex) * bindings[i].vertexDeclaration.vertexStride;
	}

	renderer->needVertexBufferBind = 1;
}

static void SDLGPU_SetViewport(
	FNA3D_Renderer *driverData,
	FNA3D_Viewport *viewport
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuViewport gpuViewport;

	if (	viewport->x != renderer->viewport.x ||
		viewport->y != renderer->viewport.y ||
		viewport->w != renderer->viewport.w ||
		viewport->h != renderer->viewport.h ||
		viewport->minDepth != renderer->viewport.minDepth ||
		viewport->maxDepth != renderer->viewport.maxDepth	)
	{
		renderer->viewport = *viewport;

		gpuViewport.x = (float) viewport->x;
		gpuViewport.y = (float) viewport->y;
		gpuViewport.w = (float) viewport->w;
		gpuViewport.h = (float) viewport->h;
		gpuViewport.minDepth = viewport->minDepth;
		gpuViewport.maxDepth = viewport->maxDepth;

		SDL_GpuSetViewport(
			renderer->device,
			renderer->renderCommandBuffer,
			&gpuViewport
		);
	}
}

static void SDLGPU_SetScissorRect(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *scissor
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	renderer->scissorRect.x = scissor->x;
	renderer->scissorRect.y = scissor->y;
	renderer->scissorRect.w = scissor->w;
	renderer->scissorRect.h = scissor->h;

	SDL_GpuSetScissor(
		renderer->device,
		renderer->renderCommandBuffer,
		&renderer->scissorRect
	);
}

static void SDLGPU_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	blendFactor->r = (uint8_t) SDL_roundf(renderer->blendConstants[0] * 255.0f);
	blendFactor->g = (uint8_t) SDL_roundf(renderer->blendConstants[1] * 255.0f);
	blendFactor->b = (uint8_t) SDL_roundf(renderer->blendConstants[2] * 255.0f);
	blendFactor->a = (uint8_t) SDL_roundf(renderer->blendConstants[3] * 255.0f);
}

static void SDLGPU_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	if (
		blendFactor->r != renderer->blendConstants[0] ||
		blendFactor->g != renderer->blendConstants[1] ||
		blendFactor->b != renderer->blendConstants[2] ||
		blendFactor->a != renderer->blendConstants[3]
	) {
		renderer->blendConstants[0] = (float) blendFactor->r / 255.0f;
		renderer->blendConstants[1] = (float) blendFactor->g / 255.0f;
		renderer->blendConstants[2] = (float) blendFactor->b / 255.0f;
		renderer->blendConstants[3] = (float) blendFactor->a / 255.0f;

		renderer->needNewGraphicsPipeline = 1;
	}
}

static int32_t SDLGPU_GetMultiSampleMask(
	FNA3D_Renderer *driverData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return (int32_t) renderer->multisampleMask;
}

static void SDLGPU_SetMultiSampleMask(
	FNA3D_Renderer *driverData,
	int32_t mask
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	if (renderer->multisampleMask != (uint32_t) mask)
	{
		renderer->multisampleMask = (uint32_t) mask;
		renderer->needNewGraphicsPipeline = 1;
	}
}

static int32_t SDLGPU_GetReferenceStencil(
	FNA3D_Renderer *driverData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return (int32_t) renderer->stencilReference;
}

static void SDLGPU_SetReferenceStencil(
	FNA3D_Renderer *driverData,
	int32_t ref
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	if (renderer->stencilReference != (uint32_t) ref)
	{
		renderer->stencilReference = (uint32_t) ref;
		renderer->needNewGraphicsPipeline = 1;
	}
}

static void SDLGPU_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDLGPU_SetBlendFactor(
		driverData,
		&blendState->blendFactor
	);

	SDLGPU_SetMultiSampleMask(
		driverData,
		blendState->multiSampleMask
	);

	if (SDL_memcmp(&renderer->fnaBlendState, blendState, sizeof(FNA3D_BlendState)) != 0)
	{
		SDL_memcpy(&renderer->fnaBlendState, blendState, sizeof(FNA3D_BlendState));
		renderer->needNewGraphicsPipeline = 1;
	}
}

static void SDLGPU_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	/* TODO: Arrange these checks in an optimized priority */
	if (	renderer->fnaDepthStencilState.depthBufferEnable != depthStencilState->depthBufferEnable ||
		renderer->fnaDepthStencilState.depthBufferWriteEnable != depthStencilState->depthBufferWriteEnable ||
		renderer->fnaDepthStencilState.depthBufferFunction != depthStencilState->depthBufferFunction ||
		renderer->fnaDepthStencilState.stencilEnable != depthStencilState->stencilEnable ||
		renderer->fnaDepthStencilState.stencilMask != depthStencilState->stencilMask ||
		renderer->fnaDepthStencilState.stencilWriteMask != depthStencilState->stencilWriteMask ||
		renderer->fnaDepthStencilState.twoSidedStencilMode != depthStencilState->twoSidedStencilMode ||
		renderer->fnaDepthStencilState.stencilFail != depthStencilState->stencilFail ||
		renderer->fnaDepthStencilState.stencilDepthBufferFail != depthStencilState->stencilDepthBufferFail ||
		renderer->fnaDepthStencilState.stencilPass != depthStencilState->stencilPass ||
		renderer->fnaDepthStencilState.stencilFunction != depthStencilState->stencilFunction ||
		renderer->fnaDepthStencilState.ccwStencilFail != depthStencilState->ccwStencilFail ||
		renderer->fnaDepthStencilState.ccwStencilDepthBufferFail != depthStencilState->ccwStencilDepthBufferFail ||
		renderer->fnaDepthStencilState.ccwStencilPass != depthStencilState->ccwStencilPass ||
		renderer->fnaDepthStencilState.ccwStencilFunction != depthStencilState->ccwStencilFunction ||
		renderer->fnaDepthStencilState.referenceStencil != depthStencilState->referenceStencil	)
	{
		renderer->needNewGraphicsPipeline = 1;

		SDL_memcpy(
			&renderer->fnaDepthStencilState,
			depthStencilState,
			sizeof(FNA3D_DepthStencilState)
		);
	}

	SDLGPU_SetReferenceStencil(
		driverData,
		depthStencilState->referenceStencil
	);
}

static void SDLGPU_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != renderer->fnaRasterizerState.scissorTestEnable)
	{
		renderer->fnaRasterizerState.scissorTestEnable = rasterizerState->scissorTestEnable;
		SDL_GpuSetScissor(
			renderer->device,
			renderer->renderCommandBuffer,
			&renderer->scissorRect
		);
	}

	realDepthBias = rasterizerState->depthBias * XNAToSDL_DepthBiasScale(
		renderer->depthStencilFormat
	);

	if (
		rasterizerState->cullMode != renderer->fnaRasterizerState.cullMode ||
		rasterizerState->fillMode != renderer->fnaRasterizerState.fillMode ||
		rasterizerState->multiSampleAntiAlias != renderer->fnaRasterizerState.multiSampleAntiAlias ||
		realDepthBias != renderer->fnaRasterizerState.depthBias ||
		rasterizerState->slopeScaleDepthBias != renderer->fnaRasterizerState.slopeScaleDepthBias
	) {
		renderer->fnaRasterizerState.cullMode = rasterizerState->cullMode;
		renderer->fnaRasterizerState.fillMode = rasterizerState->fillMode;
		renderer->fnaRasterizerState.multiSampleAntiAlias = rasterizerState->multiSampleAntiAlias;
		renderer->fnaRasterizerState.depthBias = realDepthBias;
		renderer->fnaRasterizerState.slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
		renderer->needNewGraphicsPipeline = 1;
	}
}

/* Actually bind all deferred state before drawing! */
static void SDLGPU_INTERNAL_BindDeferredState(
	SDLGPU_Renderer *renderer,
	FNA3D_PrimitiveType primitiveType,
	SDL_GpuBuffer *indexBuffer, /* can be NULL */
	SDL_GpuIndexElementSize indexElementSize
) {
	if (primitiveType != renderer->fnaPrimitiveType)
	{
		renderer->fnaPrimitiveType = primitiveType;
		renderer->needNewGraphicsPipeline = 1;
	}

	SDLGPU_INTERNAL_BeginRenderPass(renderer);
	SDLGPU_INTERNAL_BindGraphicsPipeline(renderer);

	if (renderer->needVertexSamplerBind || renderer->needFragmentSamplerBind)
	{
		if (renderer->needVertexSamplerBind)
		{
			SDL_GpuBindVertexSamplers(
				renderer->device,
				renderer->renderCommandBuffer,
				renderer->vertexTextureSamplerBindings
			);
		}

		if (renderer->needFragmentSamplerBind)
		{
			SDL_GpuBindFragmentSamplers(
				renderer->device,
				renderer->renderCommandBuffer,
				renderer->fragmentTextureSamplerBindings
			);
		}
	}

	if (
		indexBuffer != NULL &&
		renderer->indexBufferBinding.gpuBuffer != indexBuffer
	) {
		renderer->indexBufferBinding.gpuBuffer = indexBuffer;

		SDL_GpuBindIndexBuffer(
			renderer->device,
			renderer->renderCommandBuffer,
			&renderer->indexBufferBinding,
			indexElementSize
		);
	}

	if (renderer->needVertexBufferBind)
	{
		SDL_GpuBindVertexBuffers(
			renderer->device,
			renderer->renderCommandBuffer,
			0,
			renderer->numVertexBindings,
			renderer->vertexBufferBindings
		);
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

	/* Note that minVertexIndex/numVertices are NOT used! */

	SDLGPU_INTERNAL_BindDeferredState(
		renderer,
		primitiveType,
		((SDLGPU_BufferHandle*) indices)->buffer,
		XNAToSDL_IndexElementSize[indexElementSize]
	);

	SDL_GpuDrawInstancedPrimitives(
		renderer->device,
		renderer->renderCommandBuffer,
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

static void SDLGPU_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDLGPU_INTERNAL_BindDeferredState(
		renderer,
		primitiveType,
		NULL,
		SDL_GPU_INDEXELEMENTSIZE_16BIT
	);

	SDL_GpuDrawPrimitives(
		renderer->device,
		renderer->renderCommandBuffer,
		vertexStart,
		primitiveCount
	);
}

/* Backbuffer Functions */

static void SDLGPU_INTERNAL_DestroyFauxBackbuffer(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuQueueDestroyTexture(
		renderer->device,
		renderer->fauxBackbufferColor
	);

	if (renderer->fauxBackbufferDepthStencil != NULL)
	{
		SDL_GpuQueueDestroyTexture(
			renderer->device,
			renderer->fauxBackbufferDepthStencil
		);
	}

	renderer->fauxBackbufferColor = NULL;
	renderer->fauxBackbufferDepthStencil = NULL;
}

static void SDLGPU_INTERNAL_CreateFauxBackbuffer(
	SDLGPU_Renderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	SDL_GpuTextureCreateInfo backbufferCreateInfo;

	backbufferCreateInfo.width = presentationParameters->backBufferWidth;
	backbufferCreateInfo.height = presentationParameters->backBufferHeight;
	backbufferCreateInfo.depth = 1;
	backbufferCreateInfo.format = XNAToSDL_SurfaceFormat[presentationParameters->backBufferFormat];
	backbufferCreateInfo.isCube = 0;
	backbufferCreateInfo.layerCount = 1;
	backbufferCreateInfo.levelCount = 1;
	backbufferCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT | SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;
	backbufferCreateInfo.sampleCount = XNAToSDL_SampleCount(presentationParameters->multiSampleCount);

	renderer->fauxBackbufferColor = SDL_GpuCreateTexture(
		renderer->device,
		&backbufferCreateInfo
	);

	if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		backbufferCreateInfo.format = XNAToSDL_DepthFormat(presentationParameters->depthStencilFormat);
		backbufferCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT;

		renderer->fauxBackbufferDepthStencil = SDL_GpuCreateTexture(
			renderer->device,
			&backbufferCreateInfo
		);
	}

	renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
	renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;

	renderer->fauxBackbufferColorFormat = presentationParameters->backBufferFormat;
	renderer->fauxBackbufferDepthStencilFormat = presentationParameters->depthStencilFormat;

	renderer->fauxBackbufferSampleCount = presentationParameters->multiSampleCount;

	/* Set default render pass state if necessary */
	if (!renderer->renderTargetInUse)
	{
		renderer->nextRenderPassColorAttachments[0] = renderer->fauxBackbufferColor;
		renderer->nextRenderPassColorAttachmentCubeFace[0] = 0;
		renderer->nextRenderPassColorAttachmentFormats[0] = XNAToSDL_SurfaceFormat[renderer->fauxBackbufferColorFormat];
		renderer->nextRenderPassColorAttachmentCount = 1;
		renderer->nextRenderPassMultisampleCount = renderer->fauxBackbufferSampleCount;

		if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
		{
			renderer->nextRenderPassDepthStencilAttachment = renderer->fauxBackbufferDepthStencil;
			renderer->nextRenderPassDepthStencilFormat = XNAToSDL_DepthFormat(renderer->fauxBackbufferDepthStencilFormat);
		}
	}
}

static void SDLGPU_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDLGPU_INTERNAL_FlushCommandsAndStall(renderer);

	SDLGPU_INTERNAL_DestroyFauxBackbuffer(renderer);
	SDLGPU_INTERNAL_CreateFauxBackbuffer(
		renderer,
		presentationParameters
	);

	SDL_GpuUnclaimWindow(
		renderer->device,
		renderer->mainWindowHandle
	);

	if (!SDL_GpuClaimWindow(
		renderer->device,
		presentationParameters->deviceWindowHandle,
		XNAToSDL_PresentMode[presentationParameters->presentationInterval]
	)) {
		FNA3D_LogError("Failed to claim window!");
		return;
	}

	renderer->mainWindowHandle = presentationParameters->deviceWindowHandle;
}

static void SDLGPU_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDLGPU_INTERNAL_GetTextureData(
		renderer,
		renderer->fauxBackbufferColor,
		(uint32_t) x,
		(uint32_t) y,
		0,
		(uint32_t) w,
		(uint32_t) h,
		1,
		0,
		0,
		data,
		(uint32_t) dataLength
	);
}

static void SDLGPU_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	*w = (int32_t) renderer->fauxBackbufferWidth;
	*h = (int32_t) renderer->fauxBackbufferHeight;
}

static FNA3D_SurfaceFormat SDLGPU_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return renderer->fauxBackbufferColorFormat;
}

static FNA3D_DepthFormat SDLGPU_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return renderer->fauxBackbufferDepthStencilFormat;
}

static int32_t SDLGPU_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return renderer->fauxBackbufferSampleCount;
}

/* Textures */

static SDLGPU_TextureHandle* SDLGPU_INTERNAL_CreateTextureWithHandle(
	SDLGPU_Renderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	SDL_GpuTextureFormat format,
	uint32_t layerCount,
	uint32_t levelCount,
	SDL_GpuTextureUsageFlags usageFlags,
	SDL_GpuSampleCount sampleCount
) {
	SDL_GpuTextureCreateInfo textureCreateInfo;
	SDL_GpuTexture *texture;
	SDLGPU_TextureHandle *textureHandle;

	textureCreateInfo.width = width;
	textureCreateInfo.height = height;
	textureCreateInfo.depth = depth;
	textureCreateInfo.format = format;
	textureCreateInfo.layerCount = layerCount;
	textureCreateInfo.levelCount = levelCount;
	textureCreateInfo.isCube = layerCount == 6;
	textureCreateInfo.usageFlags = usageFlags;
	textureCreateInfo.sampleCount = sampleCount;

	texture = SDL_GpuCreateTexture(
		renderer->device,
		&textureCreateInfo
	);

	if (texture == NULL)
	{
		FNA3D_LogError("Failed to create texture!");
		return NULL;
	}

	textureHandle = SDL_malloc(sizeof(SDLGPU_TextureHandle));
	textureHandle->texture = texture;
	textureHandle->createInfo = textureCreateInfo;

	return textureHandle;
}

static FNA3D_Texture* SDLGPU_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	FNA3D_TextureUsageFlags usageFlags
) {
	SDL_GpuTextureUsageFlags sdlUsageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;

	if (usageFlags & FNA3D_TEXTUREUSAGE_RENDERTARGET_BIT)
	{
		sdlUsageFlags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT;
	}

	if (usageFlags & FNA3D_TEXTUREUSAGE_COMPUTE_BIT)
	{
		sdlUsageFlags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_BIT;
	}

	return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
		(SDLGPU_Renderer*) driverData,
		(uint32_t) width,
		(uint32_t) height,
		1,
		XNAToSDL_SurfaceFormat[format],
		1,
		levelCount,
		sdlUsageFlags,
		SDL_GPU_SAMPLECOUNT_1
	);
}

static FNA3D_Texture* SDLGPU_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
		(SDLGPU_Renderer*) driverData,
		(uint32_t) width,
		(uint32_t) height,
		(uint32_t) depth,
		XNAToSDL_SurfaceFormat[format],
		1,
		levelCount,
		SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT,
		SDL_GPU_SAMPLECOUNT_1
	);
}

static FNA3D_Texture* SDLGPU_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	FNA3D_TextureUsageFlags usageFlags
) {
	SDL_GpuTextureUsageFlags sdlUsageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;

	if (usageFlags & FNA3D_TEXTUREUSAGE_RENDERTARGET_BIT)
	{
		sdlUsageFlags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT;
	}

	if (usageFlags & FNA3D_TEXTUREUSAGE_COMPUTE_BIT)
	{
		sdlUsageFlags |= SDL_GPU_TEXTUREUSAGE_COMPUTE_BIT;
	}

	return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
		(SDLGPU_Renderer*) driverData,
		(uint32_t) size,
		(uint32_t) size,
		1,
		XNAToSDL_SurfaceFormat[format],
		6,
		levelCount,
		sdlUsageFlags,
		SDL_GPU_SAMPLECOUNT_1
	);
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
	SDLGPU_Renderbuffer *colorBufferHandle;

	/* Recreate texture with appropriate settings */
	SDL_GpuQueueDestroyTexture(renderer->device, textureHandle->texture);

	textureHandle->createInfo.sampleCount = XNAToSDL_SampleCount(multiSampleCount);
	textureHandle->createInfo.usageFlags =
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT |
		SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;

	textureHandle->texture = SDL_GpuCreateTexture(
		renderer->device,
		&textureHandle->createInfo
	);

	if (textureHandle->texture == NULL)
	{
		FNA3D_LogError("Failed to recreate color buffer texture!");
		return NULL;
	}

	colorBufferHandle = SDL_malloc(sizeof(SDLGPU_Renderbuffer));
	colorBufferHandle->texture = textureHandle->texture;
	colorBufferHandle->isDepth = 0;
	colorBufferHandle->sampleCount = XNAToSDL_SampleCount(multiSampleCount);
	colorBufferHandle->format = XNAToSDL_SurfaceFormat[format];

	return (FNA3D_Renderbuffer*) colorBufferHandle;
}

static FNA3D_Renderbuffer* SDLGPU_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuTextureCreateInfo textureCreateInfo;
	SDL_GpuTexture *texture;
	SDLGPU_Renderbuffer *renderbuffer;

	textureCreateInfo.width = (uint32_t) width;
	textureCreateInfo.height = (uint32_t) height;
	textureCreateInfo.depth = 1;
	textureCreateInfo.format = XNAToSDL_DepthFormat(format);
	textureCreateInfo.layerCount = 1;
	textureCreateInfo.levelCount = 1;
	textureCreateInfo.isCube = 0;
	textureCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT;
	textureCreateInfo.sampleCount = XNAToSDL_SampleCount(multiSampleCount);

	texture = SDL_GpuCreateTexture(
		renderer->device,
		&textureCreateInfo
	);

	if (texture == NULL)
	{
		FNA3D_LogError("Failed to create depth stencil buffer!");
		return NULL;
	}

	renderbuffer = SDL_malloc(sizeof(SDLGPU_Renderbuffer));
	renderbuffer->texture = texture;
	renderbuffer->isDepth = 1;
	renderbuffer->sampleCount = XNAToSDL_SampleCount(multiSampleCount);
	renderbuffer->format = XNAToSDL_DepthFormat(format);

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static void SDLGPU_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDL_GpuQueueDestroyTexture(
		renderer->device,
		textureHandle->texture
	);

	SDL_free(textureHandle);
}

static void SDLGPU_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_Renderbuffer *renderbufferHandle = (SDLGPU_Renderbuffer*) renderbuffer;

	if (renderbufferHandle->isDepth)
	{
		SDL_GpuQueueDestroyTexture(
			renderer->device,
			renderbufferHandle->texture
		);
	}

	SDL_free(renderbufferHandle);
}

static void SDLGPU_INTERNAL_BeginCopyPass(
	SDLGPU_Renderer *renderer
) {
	if (!renderer->copyPassInProgress)
	{
		SDL_GpuBeginCopyPass(
			renderer->device,
			renderer->uploadCommandBuffer
		);

		renderer->copyPassInProgress = 1;
	}
}

static void SDLGPU_INTERNAL_SetTextureData(
	SDLGPU_Renderer *renderer,
	SDL_GpuTexture *texture,
	SDL_GpuTextureFormat format,
	uint32_t x,
	uint32_t y,
	uint32_t z,
	uint32_t w,
	uint32_t h,
	uint32_t d,
	uint32_t layer,
	uint32_t mipLevel,
	void* data,
	uint32_t dataLength
) {
	SDL_GpuBufferCopy copyParams;
	SDL_GpuTextureRegion textureRegion;
	SDL_GpuBufferImageCopy textureCopyParams;

	SDLGPU_INTERNAL_BeginCopyPass(renderer);

	renderer->textureUploadBufferOffset = D3D11_INTERNAL_RoundToAlignment(
		renderer->textureUploadBufferOffset,
		SDL_GpuTextureFormatTexelBlockSize(format)
	);

	 /* Recreate transfer buffer if necessary */
	if (renderer->textureUploadBufferOffset + dataLength >= renderer->textureUploadBufferSize)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->textureUploadBuffer
		);

		renderer->textureUploadBufferSize = dataLength;
		renderer->textureUploadBufferOffset = 0;
		renderer->textureUploadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_TEXTURE,
			renderer->textureUploadBufferSize
		);
	}

	copyParams.srcOffset = 0;
	copyParams.dstOffset = renderer->textureUploadBufferOffset;
	copyParams.size = dataLength;

	SDL_GpuSetTransferData(
		renderer->device,
		data,
		renderer->textureUploadBuffer,
		&copyParams,
		(SDL_bool) renderer->textureUploadBufferOffset == 0
	);

	textureRegion.textureSlice.texture = texture;
	textureRegion.textureSlice.layer = layer;
	textureRegion.textureSlice.mipLevel = mipLevel;
	textureRegion.x = x;
	textureRegion.y = y;
	textureRegion.z = z;
	textureRegion.w = w;
	textureRegion.h = h;
	textureRegion.d = d;

	textureCopyParams.bufferOffset = renderer->textureUploadBufferOffset;
	textureCopyParams.bufferStride = 0;		/* default, assume tightly packed */
	textureCopyParams.bufferImageHeight = 0;	/* default, assume tightly packed */

	SDL_GpuUploadToTexture(
		renderer->device,
		renderer->uploadCommandBuffer,
		renderer->textureUploadBuffer,
		&textureRegion,
		&textureCopyParams,
		SDL_FALSE /* FIXME: we could check if it's a complete overwrite and set it to cycle here */
	);

	renderer->textureUploadBufferOffset += dataLength;
}

static void SDLGPU_SetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		textureHandle->texture,
		textureHandle->createInfo.format,
		(uint32_t) x,
		(uint32_t) y,
		0,
		(uint32_t) w,
		(uint32_t) h,
		1,
		0,
		(uint32_t) level,
		data,
		dataLength
	);
}

static void SDLGPU_SetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		textureHandle->texture,
		textureHandle->createInfo.format,
		(uint32_t) x,
		(uint32_t) y,
		(uint32_t) z,
		(uint32_t) w,
		(uint32_t) h,
		(uint32_t) d,
		0,
		(uint32_t) level,
		data,
		dataLength
	);
}

static void SDLGPU_SetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		textureHandle->texture,
		textureHandle->createInfo.format,
		(uint32_t) x,
		(uint32_t) y,
		0,
		(uint32_t) w,
		(uint32_t) h,
		1,
		(uint32_t) cubeMapFace,
		(uint32_t) level,
		data,
		dataLength
	);
}

static void SDLGPU_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t yWidth,
	int32_t yHeight,
	int32_t uvWidth,
	int32_t uvHeight,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *yHandle = (SDLGPU_TextureHandle*) y;
	SDLGPU_TextureHandle *uHandle = (SDLGPU_TextureHandle*) u;
	SDLGPU_TextureHandle *vHandle = (SDLGPU_TextureHandle*) v;

	int32_t yDataLength = BytesPerImage(yWidth, yHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, FNA3D_SURFACEFORMAT_ALPHA8);

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		yHandle->texture,
		yHandle->createInfo.format,
		0,
		0,
		0,
		(uint32_t) yWidth,
		(uint32_t) yHeight,
		1,
		0,
		0,
		data,
		yDataLength
	);

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		uHandle->texture,
		uHandle->createInfo.format,
		0,
		0,
		0,
		(uint32_t) uvWidth,
		(uint32_t) uvHeight,
		1,
		0,
		0,
		(uint8_t*) data + yDataLength,
		uvDataLength
	);

	SDLGPU_INTERNAL_SetTextureData(
		(SDLGPU_Renderer*) driverData,
		vHandle->texture,
		vHandle->createInfo.format,
		0,
		0,
		0,
		(uint32_t) uvWidth,
		(uint32_t) uvHeight,
		1,
		0,
		0,
		(uint8_t*) data + yDataLength + uvDataLength,
		uvDataLength
	);
}

static void SDLGPU_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDLGPU_INTERNAL_GetTextureData(
		(SDLGPU_Renderer*) driverData,
		textureHandle->texture,
		(uint32_t) x,
		(uint32_t) y,
		0,
		(uint32_t) w,
		(uint32_t) h,
		1,
		0,
		level,
		data,
		(uint32_t) dataLength
	);
}

static void SDLGPU_GetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	FNA3D_LogError(
		"GetTextureData3D is unsupported!"
	);
}

static void SDLGPU_GetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDLGPU_INTERNAL_GetTextureData(
		(SDLGPU_Renderer*) driverData,
		textureHandle->texture,
		(uint32_t) x,
		(uint32_t) y,
		0,
		(uint32_t) w,
		(uint32_t) h,
		1,
		(uint32_t) cubeMapFace,
		level,
		data,
		dataLength
	);
}

/* Buffers */

static FNA3D_Buffer* SDLGPU_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle =
		SDL_malloc(sizeof(SDLGPU_BufferHandle));

	SDL_GpuBufferUsageFlags usageFlags = SDL_GPU_BUFFERUSAGE_VERTEX_BIT;

	if (usage == FNA3D_BUFFERUSAGE_COMPUTE_EXT)
	{
		usageFlags |= SDL_GPU_BUFFERUSAGE_COMPUTE_BIT;
	}

	bufferHandle->buffer = SDL_GpuCreateGpuBuffer(
		renderer->device,
		usageFlags,
		sizeInBytes
	);
	bufferHandle->size = sizeInBytes;

	return (FNA3D_Buffer*) bufferHandle;
}

static FNA3D_Buffer* SDLGPU_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle =
		SDL_malloc(sizeof(SDLGPU_BufferHandle));

	SDL_GpuBufferUsageFlags usageFlags = SDL_GPU_BUFFERUSAGE_INDEX_BIT;

	if (usage == FNA3D_BUFFERUSAGE_COMPUTE_EXT)
	{
		usageFlags |= SDL_GPU_BUFFERUSAGE_COMPUTE_BIT;
	}

	bufferHandle->buffer = SDL_GpuCreateGpuBuffer(
		renderer->device,
		usageFlags,
		sizeInBytes
	);
	bufferHandle->size = (uint32_t) sizeInBytes;

	return (FNA3D_Buffer*) bufferHandle;
}

static void SDLGPU_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;

	SDL_GpuQueueDestroyGpuBuffer(
		renderer->device,
		bufferHandle->buffer
	);

	SDL_free(bufferHandle);
}

static void SDLGPU_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;

	SDL_GpuQueueDestroyGpuBuffer(
		renderer->device,
		bufferHandle->buffer
	);

	SDL_free(bufferHandle);
}

static void SDLGPU_INTERNAL_SetBufferData(
	SDLGPU_Renderer *renderer,
	SDL_GpuBuffer *buffer,
	uint32_t dstOffset,
	void *data,
	uint32_t dataLength,
	SDL_bool cycle
) {
	SDL_GpuBufferCopy transferCopyParams;
	SDL_GpuBufferCopy uploadParams;

	SDLGPU_INTERNAL_BeginCopyPass(renderer);

	/* Recreate transfer buffer if necessary */
	if (renderer->bufferUploadBufferOffset + dataLength >= renderer->bufferUploadBufferSize)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->bufferUploadBuffer
		);

		renderer->bufferUploadBufferSize = dataLength;
		renderer->bufferUploadBufferOffset = 0;
		renderer->bufferUploadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_BUFFER,
			renderer->bufferUploadBufferSize
		);
	}

	transferCopyParams.srcOffset = 0;
	transferCopyParams.dstOffset = renderer->bufferUploadBufferOffset;
	transferCopyParams.size = dataLength;

	SDL_GpuSetTransferData(
		renderer->device,
		data,
		renderer->bufferUploadBuffer,
		&transferCopyParams,
		(SDL_bool) renderer->bufferUploadBufferOffset == 0
	);

	uploadParams.srcOffset = renderer->bufferUploadBufferOffset;
	uploadParams.dstOffset = dstOffset;
	uploadParams.size = dataLength;

	SDL_GpuUploadToBuffer(
		renderer->device,
		renderer->uploadCommandBuffer,
		renderer->bufferUploadBuffer,
		buffer,
		&uploadParams,
		cycle
	);

	renderer->bufferUploadBufferOffset += dataLength;
}

static void SDLGPU_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;

	SDL_bool cycle;
	uint32_t dataLen = (uint32_t) elementCount * (uint32_t) vertexStride;

	if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		cycle = SDL_TRUE;
	}
	else if (options == FNA3D_SETDATAOPTIONS_NOOVERWRITE)
	{
		cycle = SDL_FALSE;
	}
	else if (dataLen == bufferHandle->size) /* NONE and full buffer update */
	{
		cycle = SDL_TRUE;
	}
	else /* Partial NONE update! This will be broken! */
	{
		FNA3D_LogWarn(
			"Dynamic buffer using SetDataOptions.None, expect bad performance and broken output!"
		);

		SDLGPU_INTERNAL_FlushCommands(renderer);
		cycle = SDL_TRUE;
	}

	SDLGPU_INTERNAL_SetBufferData(
		(SDLGPU_Renderer*) driverData,
		bufferHandle->buffer,
		(uint32_t) offsetInBytes,
		data,
		elementCount * vertexStride,
		cycle
	);
}

static void SDLGPU_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;
	SDL_bool cycle;

	if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		cycle = SDL_TRUE;
	}
	else if (options == FNA3D_SETDATAOPTIONS_NOOVERWRITE)
	{
		cycle = SDL_FALSE;
	}
	else if (dataLength == bufferHandle->size) /* NONE and full buffer update */
	{
		cycle = SDL_TRUE;
	}
	else /* Partial NONE update! This will be broken! */
	{
		FNA3D_LogWarn(
			"Dynamic buffer using SetDataOptions.None, expect bad performance and broken output!"
		);

		SDLGPU_INTERNAL_FlushCommands(renderer);
		cycle = SDL_TRUE;
	}

	SDLGPU_INTERNAL_SetBufferData(
		(SDLGPU_Renderer*) driverData,
		bufferHandle->buffer,
		(uint32_t) offsetInBytes,
		data,
		dataLength,
		cycle
	);
}

static void SDLGPU_INTERNAL_GetBufferData(
	SDLGPU_Renderer *renderer,
	SDL_GpuBuffer *buffer,
	uint32_t offset,
	void *data,
	uint32_t dataLength
) {
	SDL_GpuBufferCopy downloadParams;
	SDL_GpuBufferCopy copyParams;

	/* Flush and stall so the data is up to date */
	SDLGPU_INTERNAL_FlushCommandsAndStall(renderer);

	/* Create transfer buffer if necessary */
	if (renderer->bufferDownloadBuffer == NULL)
	{
		renderer->bufferDownloadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_BUFFER,
			dataLength
		);

		renderer->bufferDownloadBufferSize = dataLength;
	}
	else if (renderer->bufferDownloadBufferSize < dataLength)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->bufferDownloadBuffer
		);

		renderer->bufferDownloadBuffer = SDL_GpuCreateTransferBuffer(
			renderer->device,
			SDL_GPU_TRANSFERUSAGE_TEXTURE,
			dataLength
		);

		renderer->bufferDownloadBufferSize = dataLength;
	}

	/* Set up buffer download */
	downloadParams.srcOffset = offset;
	downloadParams.dstOffset = 0;
	downloadParams.size = dataLength;

	SDL_GpuDownloadFromBuffer(
		renderer->device,
		buffer,
		renderer->bufferDownloadBuffer,
		&downloadParams,
		SDL_TRUE
	);

	/* Copy into data pointer */
	copyParams.srcOffset = 0;
	copyParams.dstOffset = 0;
	copyParams.size = dataLength;

	SDL_GpuGetTransferData(
		renderer->device,
		renderer->bufferDownloadBuffer,
		data,
		&copyParams
	);
}

static void SDLGPU_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;

	SDLGPU_INTERNAL_GetBufferData(
		(SDLGPU_Renderer*) driverData,
		bufferHandle->buffer,
		offsetInBytes,
		data,
		elementCount * vertexStride
	);
}

static void SDLGPU_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	SDLGPU_BufferHandle *bufferHandle = (SDLGPU_BufferHandle*) buffer;

	SDLGPU_INTERNAL_GetBufferData(
		(SDLGPU_Renderer*) driverData,
		bufferHandle->buffer,
		offsetInBytes,
		data,
		dataLength
	);
}

/* Effects */

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

static void SDLGPU_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_Effect *sdlCloneSource = (SDLGPU_Effect*) cloneSource;
	SDLGPU_Effect *result;

	*effectData = MOJOSHADER_cloneEffect(sdlCloneSource->effect);
	if (*effectData == NULL)
	{
		FNA3D_LogError(MOJOSHADER_sdlGetError(renderer->mojoshaderContext));
	}

	result = (SDLGPU_Effect*) SDL_malloc(sizeof(SDLGPU_Effect));
	result->effect = *effectData;
	*effect = (FNA3D_Effect*) result;
}

/* TODO: check if we need to defer this */
static void SDLGPU_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
	MOJOSHADER_effect *effectData = gpuEffect->effect;

	if (effectData == renderer->currentEffect)
	{
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;
	}
	MOJOSHADER_deleteEffect(effectData);
	SDL_free(gpuEffect);
}

static void SDLGPU_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
	MOJOSHADER_effectSetTechnique(gpuEffect->effect, technique);
}

static void SDLGPU_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
	MOJOSHADER_effect *effectData = gpuEffect->effect;
	const MOJOSHADER_effectTechnique *technique = gpuEffect->effect->current_technique;
	uint32_t numPasses;

	renderer->needFragmentSamplerBind = 1;
	renderer->needVertexSamplerBind = 1;
	renderer->needNewGraphicsPipeline = 1;
	renderer->graphicsShaderExtensionInUse = 0;

	if (effectData == renderer->currentEffect)
	{
		if (
			technique == renderer->currentTechnique &&
			pass == renderer->currentPass
		) {
			MOJOSHADER_effectCommitChanges(
				renderer->currentEffect
			);

			return;
		}

		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectBeginPass(renderer->currentEffect, pass);
		renderer->currentTechnique = technique;
		renderer->currentPass = pass;

		return;
	}
	else if (renderer->currentEffect != NULL)
	{
		MOJOSHADER_effectEndPass(renderer->currentEffect);
		MOJOSHADER_effectEnd(renderer->currentEffect);
	}

	MOJOSHADER_effectBegin(
		effectData,
		&numPasses,
		0,
		stateChanges
	);

	MOJOSHADER_effectBeginPass(effectData, pass);
	renderer->currentEffect = effectData;
	renderer->currentTechnique = technique;
	renderer->currentPass = pass;
}

static void SDLGPU_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	MOJOSHADER_effect *effectData = ((SDLGPU_Effect*) effect)->effect;
	uint32_t whatever;

	MOJOSHADER_effectBegin(
			effectData,
			&whatever,
			1,
			stateChanges
	);
	MOJOSHADER_effectBeginPass(effectData, 0);
}

static void SDLGPU_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MOJOSHADER_effect *effectData = ((SDLGPU_Effect*) effect)->effect;
	MOJOSHADER_effectEndPass(effectData);
	MOJOSHADER_effectEnd(effectData);
}

/* Queries */

static FNA3D_Query* SDLGPU_CreateQuery(FNA3D_Renderer *driverData)
{
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	return (FNA3D_Query*) SDL_GpuCreateOcclusionQuery(renderer->device);
}

static void SDLGPU_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuQueueDestroyOcclusionQuery(
		renderer->device,
		(SDL_GpuOcclusionQuery*) query
	);
}

static void SDLGPU_QueryBegin(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuOcclusionQueryBegin(
		renderer->device,
		renderer->renderCommandBuffer,
		(SDL_GpuOcclusionQuery*) query
	);
}

static void SDLGPU_QueryEnd(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDL_GpuOcclusionQueryEnd(
		renderer->device,
		renderer->renderCommandBuffer,
		(SDL_GpuOcclusionQuery*) query
	);
}

static uint8_t SDLGPU_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	uint32_t blah;

	return (uint8_t) SDL_GpuOcclusionQueryPixelCount(
		renderer->device,
		(SDL_GpuOcclusionQuery*) query,
		&blah
	);
}

static int32_t SDLGPU_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	uint32_t pixelCount;
	SDL_bool result;

	result = SDL_GpuOcclusionQueryPixelCount(
		renderer->device,
		(SDL_GpuOcclusionQuery*) query,
		&pixelCount
	);

	if (!result)
	{
		return 0;
	}

	return (int32_t) pixelCount;
}

static uint8_t SDLGPU_SupportsDXT1(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsS3TC(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsBC7(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsSRGBRenderTargets(FNA3D_Renderer *driverData)
{
	/* TODO */
	return 1;
}

static void SDLGPU_GetMaxTextureSlots(
	FNA3D_Renderer *driverData,
	int32_t *textures,
	int32_t *vertexTextures
) {
	/* TODO */
	*textures = MAX_TEXTURE_SAMPLERS;
	*vertexTextures = MAX_VERTEXTEXTURE_SAMPLERS;
}

static int32_t SDLGPU_GetMaxMultiSampleCount(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount
) {
	/* TODO */
	return 8;
}

/* Debugging */

static void SDLGPU_SetStringMarker(
	FNA3D_Renderer *driverData,
	const char *text
) {
	/* TODO */
}

static void SDLGPU_SetTextureName(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	const char *text
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

	SDL_GpuSetTextureName(
		renderer->device,
		textureHandle->texture,
		text
	);
}

/* External Interop */

static void SDLGPU_GetSysRenderer(
	FNA3D_Renderer *driverData,
	FNA3D_SysRendererEXT *sysrenderer
) {
	/* TODO */
	SDL_memset(sysrenderer, '\0', sizeof(FNA3D_SysRendererEXT));
}

static FNA3D_Texture* SDLGPU_CreateSysTexture(
	FNA3D_Renderer *driverData,
	FNA3D_SysTextureEXT *systexture
) {
	/* TODO */
	return NULL;
}

/* Shader Extension */

static void SDLGPU_BindGraphicsShadersEXT(
	FNA3D_Renderer *driverData,
	SDL_GpuGraphicsShaderInfo *vertShaderInfo,
	SDL_GpuGraphicsShaderInfo *fragShaderInfo
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	renderer->needVertexSamplerBind = 1;
	renderer->needFragmentSamplerBind = 1;
	renderer->needNewGraphicsPipeline = 1;

	renderer->graphicsShaderExtensionInUse = 1;

	SDL_memcpy(&renderer->extensionVertexShaderInfo, vertShaderInfo, sizeof(SDL_GpuGraphicsShaderInfo));
	SDL_memcpy(&renderer->extensionFragmentShaderInfo, fragShaderInfo, sizeof(SDL_GpuGraphicsShaderInfo));

	/* Immediately bind deferred pipeline state so uniforms can be pushed */
	SDLGPU_INTERNAL_BeginRenderPass(renderer);
	SDLGPU_INTERNAL_BindGraphicsPipeline(renderer);
}

static void SDLGPU_PushVertexShaderUniformsEXT(
	FNA3D_Renderer *driverData,
	void *data,
	uint32_t dataLengthInBytes
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuPushVertexShaderUniforms(
		renderer->device,
		renderer->renderCommandBuffer,
		data,
		dataLengthInBytes
	);
}

static void SDLGPU_PushFragmentShaderUniformsEXT(
	FNA3D_Renderer *driverData,
	void *data,
	uint32_t dataLengthInBytes
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuPushFragmentShaderUniforms(
		renderer->device,
		renderer->renderCommandBuffer,
		data,
		dataLengthInBytes
	);
}

static SDL_GpuComputePipeline* SDLGPU_INTERNAL_FetchComputePipeline(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuComputePipeline *pipeline;

	pipeline = ComputePipelineHashArray_Fetch(
		&renderer->computePipelineHashArray,
		renderer->extensionComputeShaderInfo.shaderModule
	);

	if (pipeline != NULL)
	{
		return pipeline;
	}

	pipeline = SDL_GpuCreateComputePipeline(
		renderer->device,
		&renderer->extensionComputeShaderInfo
	);

	ComputePipelineHashArray_Insert(
		&renderer->computePipelineHashArray,
		renderer->extensionComputeShaderInfo.shaderModule,
		pipeline
	);

	return pipeline;
}

void SDLGPU_INTERNAL_BindComputePipeline(
	SDLGPU_Renderer *renderer
) {
	SDL_GpuComputePipeline *pipeline;

	pipeline = SDLGPU_INTERNAL_FetchComputePipeline(renderer);

	SDL_GpuBindComputePipeline(
		renderer->device,
		renderer->renderCommandBuffer,
		pipeline
	);

	renderer->currentComputePipeline = pipeline;
}

static void SDLGPU_BindComputeShaderEXT(
	FNA3D_Renderer *driverData,
	SDL_GpuComputeShaderInfo *computeShaderInfo
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDLGPU_INTERNAL_EndRenderPass(renderer);

	SDL_memcpy(&renderer->extensionComputeShaderInfo, computeShaderInfo, sizeof(SDL_GpuComputeShaderInfo));

	/* Fetch compute pipeline */
	SDLGPU_INTERNAL_BindComputePipeline(renderer);

	SDL_GpuBeginComputePass(
		renderer->device,
		renderer->renderCommandBuffer
	);
}

static void SDLGPU_BindComputeBuffersEXT(
	FNA3D_Renderer *driverData,
	SDL_GpuComputeBufferBinding *pBindings
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuBindComputeBuffers(
		renderer->device,
		renderer->renderCommandBuffer,
		pBindings
	);
}

static void SDLGPU_BindComputeTexturesEXT(
	FNA3D_Renderer *driverData,
	SDL_GpuComputeTextureBinding *pBindings
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuBindComputeTextures(
		renderer->device,
		renderer->renderCommandBuffer,
		pBindings
	);
}

static void SDLGPU_PushComputeShaderUniformsEXT(
	FNA3D_Renderer *driverData,
	void *data,
	uint32_t dataLengthInBytes
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuPushComputeShaderUniforms(
		renderer->device,
		renderer->renderCommandBuffer,
		data,
		dataLengthInBytes
	);
}

static void SDLGPU_DispatchComputeEXT(
	FNA3D_Renderer *driverData,
	uint32_t groupCountX,
	uint32_t groupCountY,
	uint32_t groupCountZ
) {
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	SDL_GpuDispatchCompute(
		renderer->device,
		renderer->renderCommandBuffer,
		groupCountX,
		groupCountY,
		groupCountZ
	);
}

/* Destroy */

static void SDLGPU_DestroyDevice(FNA3D_Device *device)
{
	SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) device->driverData;
	int32_t i, j;

	if (renderer->textureDownloadBuffer != NULL)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->textureDownloadBuffer
		);
	}

	if (renderer->bufferDownloadBuffer != NULL)
	{
		SDL_GpuQueueDestroyTransferBuffer(
			renderer->device,
			renderer->bufferDownloadBuffer
		);
	}

	SDL_GpuQueueDestroyTransferBuffer(renderer->device, renderer->textureUploadBuffer);
	SDL_GpuQueueDestroyTransferBuffer(renderer->device, renderer->bufferUploadBuffer);

	SDLGPU_INTERNAL_DestroyFauxBackbuffer(renderer);

	for (i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1)
	{
		if (renderer->fences[i] != NULL)
		{
			SDL_GpuReleaseFence(
				renderer->device,
				renderer->fences[i]
			);
		}
	}

	for (i = 0; i < NUM_PIPELINE_HASH_BUCKETS; i += 1)
	{
		for (j = 0; j < renderer->graphicsPipelineHashTable.buckets[i].count; j += 1)
		{
			SDL_GpuQueueDestroyGraphicsPipeline(
				renderer->device,
				renderer->graphicsPipelineHashTable.buckets[i].elements[j].value
			);
		}
	}

	for (i = 0; i < renderer->samplerStateArray.count; i += 1)
	{
		SDL_GpuQueueDestroySampler(
			renderer->device,
			renderer->samplerStateArray.elements[i].value
		);
	}

	MOJOSHADER_sdlDestroyContext(renderer->mojoshaderContext);

	SDL_GpuDestroyDevice(renderer->device);

	SDL_free(renderer);
	SDL_free(device);
}

/* Initialization */

static uint8_t SDLGPU_PrepareWindowAttributes(uint32_t *flags)
{
	SDL_GpuBackend selectedBackend =
		SDL_GpuSelectBackend(
			preferredBackends,
			SDL_arraysize(preferredBackends),
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
	int32_t i;

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

	if (!SDL_GpuClaimWindow(
		renderer->device,
		presentationParameters->deviceWindowHandle,
		XNAToSDL_PresentMode[presentationParameters->presentationInterval]
	)) {
		FNA3D_LogError("Failed to claim window!");
		return NULL;
	}

	renderer->mainWindowHandle = presentationParameters->deviceWindowHandle;

	SDLGPU_INTERNAL_CreateFauxBackbuffer(
		renderer,
		presentationParameters
	);

	if (renderer->fauxBackbufferColor == NULL)
	{
		FNA3D_LogError("Failed to create faux backbuffer!");
		return NULL;
	}

	renderer->textureUploadBufferSize = 8388608; /* 8 MiB */
	renderer->textureUploadBufferOffset = 0;
	renderer->textureUploadBuffer = SDL_GpuCreateTransferBuffer(
		renderer->device,
		SDL_GPU_TRANSFERUSAGE_TEXTURE,
		renderer->textureUploadBufferSize
	);

	if (renderer->textureUploadBuffer == NULL)
	{
		FNA3D_LogError("Failed to create texture transfer buffer!");
		return NULL;
	}

	renderer->bufferUploadBufferSize = 8388608; /* 8 MiB */
	renderer->bufferUploadBufferOffset = 0;
	renderer->bufferUploadBuffer = SDL_GpuCreateTransferBuffer(
		renderer->device,
		SDL_GPU_TRANSFERUSAGE_BUFFER,
		renderer->bufferUploadBufferSize
	);

	/*
	 * Initialize renderer members not covered by SDL_memset('\0')
	 */

	renderer->multisampleMask = 0xFFFFFFFF;

	for (i = 0; i < MAX_BOUND_VERTEX_BUFFERS; i += 1)
	{
		renderer->vertexBindings[i].vertexDeclaration.elements =
			renderer->vertexElements[i];
	}

	renderer->mojoshaderContext = MOJOSHADER_sdlCreateContext(
		device,
		NULL,
		NULL,
		NULL
	);

	/* FIXME: moltenVK fix */
	renderer->supportsBaseVertex = 1;

	/* Acquire command buffer, we are ready for takeoff */

	SDLGPU_ResetCommandBufferState(renderer);

	return result;
}

/* Driver struct */

FNA3D_Driver SDLGPUDriver = {
	"SDLGPU",
	SDLGPU_PrepareWindowAttributes,
	SDLGPU_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_SDL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
