/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2022 Ethan Lee
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

#if FNA3D_DRIVER_D3D12

#include "FNA3D_Driver.h"

#define CINTERFACE
#define COBJMACROS
#include <directx/d3d12.h>
#ifndef _GAMING_XBOX
#include <dxgi1_6.h>
#endif

#include <SDL.h>

#define ERROR_CHECK(msg) \
	if (FAILED(res)) \
	{ \
		D3D12_INTERNAL_LogError(renderer->device, msg, res); \
	}
#define ERROR_CHECK_RETURN(msg, ret) \
	if (FAILED(res)) \
	{ \
		D3D12_INTERNAL_LogError(renderer->device, msg, res); \
		return ret; \
	}

/* IIDs */

static const IID D3D_IID_ID3D12Device = { 0x189819f1,0x1db6,0x4b57,{0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7} };
static const IID D3D_IID_IDXGIFactory1 = { 0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87} };
static const IID D3D_IID_IDXGIFactory6 = { 0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17} };
static const IID D3D_IID_IDXGIAdapter1 = { 0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05} };
static const IID D3D_IID_ID3D12Debug = { 0x344488b7,0x6846,0x474b,{0xb9,0x89,0xf0,0x27,0x44,0x82,0x45,0xe0} };
static const IID D3D_IID_ID3D12CommandQueue = { 0x0ec870a6,0x5d7e,0x4c22,{0x8c,0xfc,0x5b,0xaa,0xe0,0x76,0x16,0xed} };
static const IID D3D_IID_ID3D12CommandAllocator = { 0x6102dee4,0xaf59,0x4b09,{0xb9,0x99,0xb4,0x4d,0x73,0xf0,0x9b,0x24} };
static const IID D3D_IID_ID3D12GraphicsCommandList = { 0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55} };
static const IID D3D_IID_ID3D12Fence = { 0x0a753dcf,0xc4d8,0x4b91,{0xad,0xf6,0xbe,0x5a,0x60,0xd9,0x5a,0x76} };

/* Internal Structures */

typedef struct D3D12Texture /* Cast FNA3D_Texture* to this! */
{
	uint8_t filler;
} D3D12Texture;

typedef struct D3D12Renderbuffer /* Cast FNA3D_Renderbuffer* to this! */
{
	uint8_t filler;
} D3D12Renderbuffer;

typedef struct D3D12Buffer /* Cast FNA3D_Buffer* to this! */
{
	intptr_t size;
} D3D12Buffer;

typedef struct D3D12Effect /* Cast FNA3D_Effect* to this! */
{
	MOJOSHADER_effect *effect;
} D3D12Effect;

typedef struct D3D12Query /* Cast FNA3D_Query* to this! */
{
	uint8_t filler;
} D3D12Query;

typedef struct D3D12TransferBuffer
{
	D3D12Buffer *buffer;
	size_t offset;
} D3D12TransferBuffer;

typedef struct D3D12TransferBufferPool
{
	D3D12TransferBuffer *fastTransferBuffer;
	uint8_t fastTransferBufferAvailable;

	D3D12TransferBuffer **availableSlowTransferBuffers;
	uint32_t availableSlowTransferBufferCount;
	uint32_t availableSlowTransferBufferCapacity;
} D3D12TransferBufferPool;

/* Command buffers have various resources associated with them
 * that can be freed after the command buffer is fully processed.
 */
typedef struct D3D12CommandBufferContainer
{
	ID3D12CommandAllocator *commandAllocator;
	ID3D12GraphicsCommandList *commandList;
	ID3D12Fence *inFlightFence;

	/* FIXME */
#if 0
	DescriptorSetData* usedDescriptorSetDatas;
	uint32_t usedDescriptorSetDataCount;
	uint32_t usedDescriptorSetDataCapacity;
#endif

	D3D12TransferBuffer** transferBuffers;
	uint32_t transferBufferCount;
	uint32_t transferBufferCapacity;

	D3D12Buffer** boundBuffers;
	uint32_t boundBufferCount;
	uint32_t boundBufferCapacity;

	D3D12Renderbuffer** renderbuffersToDestroy;
	uint32_t renderbuffersToDestroyCount;
	uint32_t renderbuffersToDestroyCapacity;

	D3D12Buffer** buffersToDestroy;
	uint32_t buffersToDestroyCount;
	uint32_t buffersToDestroyCapacity;

	D3D12Effect** effectsToDestroy;
	uint32_t effectsToDestroyCount;
	uint32_t effectsToDestroyCapacity;

	D3D12Texture** texturesToDestroy;
	uint32_t texturesToDestroyCount;
	uint32_t texturesToDestroyCapacity;
} D3D12CommandBufferContainer;

typedef struct D3D12Renderer /* Cast FNA3D_Renderer* to this! */
{
	/* Device */
	void* d3d12_dll;
	ID3D12Device *device;

	/* Queue */
	ID3D12CommandQueue *unifiedQueue;

	/* DXGI */
	void* dxgi_dll;
	IDXGIFactory1 *factory;
	IDXGIAdapter1 *adapter;

	/* Debug */
	ID3D12Debug* debug;
	uint8_t debugMode;

	/* Command Buffers */
	D3D12CommandBufferContainer **inactiveCommandBufferContainers;
	uint32_t inactiveCommandBufferContainerCount;
	uint32_t inactiveCommandBufferContainerCapacity;

	D3D12CommandBufferContainer **submittedCommandBufferContainers;
	uint32_t submittedCommandBufferContainerCount;
	uint32_t submittedCommandBufferContainerCapacity;

	uint32_t currentCommandCount;
	D3D12CommandBufferContainer *currentCommandBufferContainer;
	uint32_t numActiveCommands;

	D3D12CommandBufferContainer *defragCommandBufferContainer; /* Special command buffer for performing defrag copies */

	D3D12TransferBufferPool transferBufferPool;

	/* Dynamic State */
	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;
	FNA3D_Color blendFactor;
	int32_t multiSampleMask;
	int32_t stencilRef;

	/* Threading */
	SDL_mutex *commandLock;
	SDL_mutex *passLock;
	SDL_mutex *disposeLock;
	SDL_mutex *allocatorLock;
	SDL_mutex *transferLock;

} D3D12Renderer;

/* XNA->D3D12 Translation Arrays */

static DXGI_FORMAT XNAToD3D_TextureFormat[] =
{
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* SurfaceFormat.Color */
	DXGI_FORMAT_B5G6R5_UNORM,	/* SurfaceFormat.Bgr565 */
	DXGI_FORMAT_B5G5R5A1_UNORM,	/* SurfaceFormat.Bgra5551 */
	DXGI_FORMAT_B4G4R4A4_UNORM,	/* SurfaceFormat.Bgra4444 */
	DXGI_FORMAT_BC1_UNORM,		/* SurfaceFormat.Dxt1 */
	DXGI_FORMAT_BC2_UNORM,		/* SurfaceFormat.Dxt3 */
	DXGI_FORMAT_BC3_UNORM,		/* SurfaceFormat.Dxt5 */
	DXGI_FORMAT_R8G8_SNORM, 	/* SurfaceFormat.NormalizedByte2 */
	DXGI_FORMAT_R8G8B8A8_SNORM,	/* SurfaceFormat.NormalizedByte4 */
	DXGI_FORMAT_R10G10B10A2_UNORM,	/* SurfaceFormat.Rgba1010102 */
	DXGI_FORMAT_R16G16_UNORM,	/* SurfaceFormat.Rg32 */
	DXGI_FORMAT_R16G16B16A16_UNORM,	/* SurfaceFormat.Rgba64 */
	DXGI_FORMAT_A8_UNORM,		/* SurfaceFormat.Alpha8 */
	DXGI_FORMAT_R32_FLOAT,		/* SurfaceFormat.Single */
	DXGI_FORMAT_R32G32_FLOAT,	/* SurfaceFormat.Vector2 */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* SurfaceFormat.Vector4 */
	DXGI_FORMAT_R16_FLOAT,		/* SurfaceFormat.HalfSingle */
	DXGI_FORMAT_R16G16_FLOAT,	/* SurfaceFormat.HalfVector2 */
	DXGI_FORMAT_R16G16B16A16_FLOAT,	/* SurfaceFormat.HalfVector4 */
	DXGI_FORMAT_R16G16B16A16_FLOAT,	/* SurfaceFormat.HdrBlendable */
	DXGI_FORMAT_B8G8R8A8_UNORM,	/* SurfaceFormat.ColorBgraEXT */
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,/* SurfaceFormat.ColorSrgbEXT */
	DXGI_FORMAT_BC3_UNORM_SRGB,	/* SurfaceFormat.Dxt5SrgbEXT */
	DXGI_FORMAT_BC7_UNORM, /* SurfaceFormat.BC7EXT */
	DXGI_FORMAT_BC7_UNORM_SRGB,	/* SurfaceFormat.BC7SrgbEXT */
};

static DXGI_FORMAT XNAToD3D_DepthFormat[] =
{
	DXGI_FORMAT_UNKNOWN,		/* DepthFormat.None */
	DXGI_FORMAT_D16_UNORM,		/* DepthFormat.Depth16 */
	DXGI_FORMAT_D24_UNORM_S8_UINT,	/* DepthFormat.Depth24 */
	DXGI_FORMAT_D24_UNORM_S8_UINT	/* DepthFormat.Depth24Stencil8 */
};

static LPCSTR XNAToD3D_VertexAttribSemanticName[] =
{
	"POSITION",			/* VertexElementUsage.Position */
	"COLOR",			/* VertexElementUsage.Color */
	"TEXCOORD",			/* VertexElementUsage.TextureCoordinate */
	"NORMAL",			/* VertexElementUsage.Normal */
	"BINORMAL",			/* VertexElementUsage.Binormal */
	"TANGENT",			/* VertexElementUsage.Tangent */
	"BLENDINDICES",			/* VertexElementUsage.BlendIndices */
	"BLENDWEIGHT",			/* VertexElementUsage.BlendWeight */
	"SV_DEPTH",			/* VertexElementUsage.Depth */
	"FOG",				/* VertexElementUsage.Fog */
	"PSIZE",			/* VertexElementUsage.PointSize */
	"SV_SampleIndex",		/* VertexElementUsage.Sample */
	"TESSFACTOR"			/* VertexElementUsage.TessellateFactor */
};

static DXGI_FORMAT XNAToD3D_VertexAttribFormat[] =
{
	DXGI_FORMAT_R32_FLOAT,		/* VertexElementFormat.Single */
	DXGI_FORMAT_R32G32_FLOAT,	/* VertexElementFormat.Vector2 */
	DXGI_FORMAT_R32G32B32_FLOAT,	/* VertexElementFormat.Vector3 */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* VertexElementFormat.Vector4 */
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* VertexElementFormat.Color */
	DXGI_FORMAT_R8G8B8A8_UINT,	/* VertexElementFormat.Byte4 */
	DXGI_FORMAT_R16G16_SINT,	/* VertexElementFormat.Short2 */
	DXGI_FORMAT_R16G16B16A16_SINT,	/* VertexElementFormat.Short4 */
	DXGI_FORMAT_R16G16_SNORM,	/* VertexElementFormat.NormalizedShort2 */
	DXGI_FORMAT_R16G16B16A16_SNORM,	/* VertexElementFormat.NormalizedShort4 */
	DXGI_FORMAT_R16G16_FLOAT,	/* VertexElementFormat.HalfVector2 */
	DXGI_FORMAT_R16G16B16A16_FLOAT	/* VertexElementFormat.HalfVector4 */
};

static DXGI_FORMAT XNAToD3D_IndexType[] =
{
	DXGI_FORMAT_R16_UINT,		/* IndexElementSize.SixteenBits */
	DXGI_FORMAT_R32_UINT		/* IndexElementSize.ThirtyTwoBits */
};

static D3D12_BLEND XNAToD3D_BlendMode[] =
{
	D3D12_BLEND_ONE,		/* Blend.One */
	D3D12_BLEND_ZERO,		/* Blend.Zero */
	D3D12_BLEND_SRC_COLOR,		/* Blend.SourceColor */
	D3D12_BLEND_INV_SRC_COLOR,	/* Blend.InverseSourceColor */
	D3D12_BLEND_SRC_ALPHA,		/* Blend.SourceAlpha */
	D3D12_BLEND_INV_SRC_ALPHA,	/* Blend.InverseSourceAlpha */
	D3D12_BLEND_DEST_COLOR,		/* Blend.DestinationColor */
	D3D12_BLEND_INV_DEST_COLOR,	/* Blend.InverseDestinationColor */
	D3D12_BLEND_DEST_ALPHA,		/* Blend.DestinationAlpha */
	D3D12_BLEND_INV_DEST_ALPHA,	/* Blend.InverseDestinationAlpha */
	D3D12_BLEND_BLEND_FACTOR,	/* Blend.BlendFactor */
	D3D12_BLEND_INV_BLEND_FACTOR,	/* Blend.InverseBlendFactor */
	D3D12_BLEND_SRC_ALPHA_SAT	/* Blend.SourceAlphaSaturation */
};

static D3D12_BLEND XNAToD3D_BlendModeAlpha[] =
{
	D3D12_BLEND_ONE,		/* Blend.One */
	D3D12_BLEND_ZERO,		/* Blend.Zero */
	D3D12_BLEND_SRC_ALPHA,		/* Blend.SourceColor */
	D3D12_BLEND_INV_SRC_ALPHA,	/* Blend.InverseSourceColor */
	D3D12_BLEND_SRC_ALPHA,		/* Blend.SourceAlpha */
	D3D12_BLEND_INV_SRC_ALPHA,	/* Blend.InverseSourceAlpha */
	D3D12_BLEND_DEST_ALPHA,		/* Blend.DestinationColor */
	D3D12_BLEND_INV_DEST_ALPHA,	/* Blend.InverseDestinationColor */
	D3D12_BLEND_DEST_ALPHA,		/* Blend.DestinationAlpha */
	D3D12_BLEND_INV_DEST_ALPHA,	/* Blend.InverseDestinationAlpha */
	D3D12_BLEND_BLEND_FACTOR,	/* Blend.BlendFactor */
	D3D12_BLEND_INV_BLEND_FACTOR,	/* Blend.InverseBlendFactor */
	D3D12_BLEND_SRC_ALPHA_SAT	/* Blend.SourceAlphaSaturation */
};

static D3D12_BLEND_OP XNAToD3D_BlendOperation[] =
{
	D3D12_BLEND_OP_ADD,		/* BlendFunction.Add */
	D3D12_BLEND_OP_SUBTRACT,	/* BlendFunction.Subtract */
	D3D12_BLEND_OP_REV_SUBTRACT,	/* BlendFunction.ReverseSubtract */
	D3D12_BLEND_OP_MAX,		/* BlendFunction.Max */
	D3D12_BLEND_OP_MIN		/* BlendFunction.Min */
};

static D3D12_COMPARISON_FUNC XNAToD3D_CompareFunc[] =
{
	D3D12_COMPARISON_FUNC_ALWAYS,	/* CompareFunction.Always */
	D3D12_COMPARISON_FUNC_NEVER,		/* CompareFunction.Never */
	D3D12_COMPARISON_FUNC_LESS,		/* CompareFunction.Less */
	D3D12_COMPARISON_FUNC_LESS_EQUAL,	/* CompareFunction.LessEqual */
	D3D12_COMPARISON_FUNC_EQUAL,		/* CompareFunction.Equal */
	D3D12_COMPARISON_FUNC_GREATER_EQUAL,	/* CompareFunction.GreaterEqual */
	D3D12_COMPARISON_FUNC_GREATER,	/* CompareFunction.Greater */
	D3D12_COMPARISON_FUNC_NOT_EQUAL	/* CompareFunction.NotEqual */
};

static D3D12_STENCIL_OP XNAToD3D_StencilOp[] =
{
	D3D12_STENCIL_OP_KEEP,		/* StencilOperation.Keep */
	D3D12_STENCIL_OP_ZERO,		/* StencilOperation.Zero */
	D3D12_STENCIL_OP_REPLACE,	/* StencilOperation.Replace */
	D3D12_STENCIL_OP_INCR,		/* StencilOperation.Increment */
	D3D12_STENCIL_OP_DECR,		/* StencilOperation.Decrement */
	D3D12_STENCIL_OP_INCR_SAT,	/* StencilOperation.IncrementSaturation */
	D3D12_STENCIL_OP_DECR_SAT,	/* StencilOperation.DecrementSaturation */
	D3D12_STENCIL_OP_INVERT		/* StencilOperation.Invert */
};

static D3D12_FILL_MODE XNAToD3D_FillMode[] =
{
	D3D12_FILL_MODE_SOLID,		/* FillMode.Solid */
	D3D12_FILL_MODE_WIREFRAME		/* FillMode.WireFrame */
};

static float XNAToD3D_DepthBiasScale[] =
{
	0.0f,				/* DepthFormat.None */
	(float)((1 << 16) - 1),	/* DepthFormat.Depth16 */
	(float)((1 << 24) - 1),	/* DepthFormat.Depth24 */
	(float)((1 << 24) - 1) 	/* DepthFormat.Depth24Stencil8 */
};

static D3D12_CULL_MODE XNAToD3D_CullMode[] =
{
	D3D12_CULL_MODE_NONE,		/* CullMode.None */
	D3D12_CULL_MODE_BACK,		/* CullMode.CullClockwiseFace */
	D3D12_CULL_MODE_FRONT 		/* CullMode.CullCounterClockwiseFace */
};

static D3D12_TEXTURE_ADDRESS_MODE XNAToD3D_Wrap[] =
{
	D3D12_TEXTURE_ADDRESS_MODE_WRAP,	/* TextureAddressMode.Wrap */
	D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	/* TextureAddressMode.Clamp */
	D3D12_TEXTURE_ADDRESS_MODE_MIRROR	/* TextureAddressMode.Mirror */
};

static D3D12_FILTER XNAToD3D_Filter[] =
{
	D3D12_FILTER_MIN_MAG_MIP_LINEAR,		/* TextureFilter.Linear */
	D3D12_FILTER_MIN_MAG_MIP_POINT,			/* TextureFilter.Point */
	D3D12_FILTER_ANISOTROPIC,			/* TextureFilter.Anisotropic */
	D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,		/* TextureFilter.LinearMipPoint */
	D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,		/* TextureFilter.PointMipLinear */
	D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,	/* TextureFilter.MinLinearMagPointMipLinear */
	D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,		/* TextureFilter.MinLinearMagPointMipPoint */
	D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,		/* TextureFilter.MinPointMagLinearMipLinear */
	D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT	/* TextureFilter.MinPointMagLinearMipPoint */
};

static D3D_PRIMITIVE_TOPOLOGY XNAToD3D_Primitive[] =
{
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	/* PrimitiveType.TriangleList */
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,	/* PrimitiveType.TriangleStrip */
	D3D_PRIMITIVE_TOPOLOGY_LINELIST,	/* PrimitiveType.LineList */
	D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,	/* PrimitiveType.LineStrip */
	D3D_PRIMITIVE_TOPOLOGY_POINTLIST	/* PrimitiveType.PointListEXT */
};

/* Helper Functions */

static void D3D12_INTERNAL_LogError(
	ID3D12Device *device,
	const char *msg,
	HRESULT res
) {
	#define MAX_ERROR_LEN 1024 /* FIXME: Arbitrary! */

	/* Buffer for text, ensure space for \0 terminator after buffer */
	char wszMsgBuff[MAX_ERROR_LEN + 1];
	DWORD dwChars; /* Number of chars returned. */

	if (res == DXGI_ERROR_DEVICE_REMOVED)
	{
		res = ID3D12Device_GetDeviceRemovedReason(device);
	}

	/* Try to get the message from the system errors. */
	dwChars = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		res,
		0,
		wszMsgBuff,
		MAX_ERROR_LEN,
		NULL
	);

	/* No message? Screw it, just post the code. */
	if (dwChars == 0)
	{
		FNA3D_LogError("%s! Error Code: 0x%08X", msg, res);
		return;
	}

	/* Ensure valid range */
	dwChars = SDL_min(dwChars, MAX_ERROR_LEN);

	/* Trim whitespace from tail of message */
	while (dwChars > 0)
	{
		if (wszMsgBuff[dwChars - 1] <= ' ')
		{
			dwChars--;
		}
		else
		{
			break;
		}
	}

	/* Ensure null-terminated string */
	wszMsgBuff[dwChars] = '\0';

	FNA3D_LogError("%s! Error Code: %s (0x%08X)", msg, wszMsgBuff, res);
}

/* D3D12 Internal Implementation */

/* D3D12: Command Buffers */

static D3D12CommandBufferContainer* D3D12_INTERNAL_AllocateCommandBuffer(
	D3D12Renderer *renderer,
	uint8_t fenceSignaled
) {
	D3D12CommandBufferContainer *d3d12CommandBufferContainer = SDL_malloc(sizeof(D3D12CommandBufferContainer));
	HRESULT res;

	res = ID3D12Device_CreateCommandAllocator(
		renderer->device,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		&D3D_IID_ID3D12CommandAllocator,
		&d3d12CommandBufferContainer->commandAllocator
	);
	ERROR_CHECK_RETURN("Could not create command allocator", NULL);

	res = ID3D12Device_CreateCommandList(
		renderer->device,
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		d3d12CommandBufferContainer->commandAllocator,
		NULL,
		&D3D_IID_ID3D12GraphicsCommandList,
		&d3d12CommandBufferContainer->commandList
	);
	ERROR_CHECK_RETURN("Could not create command list", NULL);

	/* Command lists start in the recording state. */
	ID3D12GraphicsCommandList_Close(d3d12CommandBufferContainer->commandList);

	res = ID3D12Device_CreateFence(
		renderer->device,
		fenceSignaled ? 1 : 0,
		D3D12_FENCE_FLAG_NONE,
		&D3D_IID_ID3D12Fence,
		&d3d12CommandBufferContainer->inFlightFence
	);
	ERROR_CHECK_RETURN("Could not create fence", NULL);

	/* Transfer buffer tracking */

	d3d12CommandBufferContainer->transferBufferCapacity = 0;
	d3d12CommandBufferContainer->transferBufferCount = 0;
	d3d12CommandBufferContainer->transferBuffers = NULL;

	/* FIXME */
#if 0
	/* Descriptor set tracking */

	d3d12CommandBufferContainer->usedDescriptorSetDataCapacity = 16;
	d3d12CommandBufferContainer->usedDescriptorSetDataCount = 0;
	d3d12CommandBufferContainer->usedDescriptorSetDatas = SDL_malloc(
		d3d12CommandBufferContainer->usedDescriptorSetDataCapacity * sizeof(DescriptorSetData)
	);
#endif

	/* Bound buffer tracking */

	d3d12CommandBufferContainer->boundBufferCapacity = 4;
	d3d12CommandBufferContainer->boundBufferCount = 0;
	d3d12CommandBufferContainer->boundBuffers = SDL_malloc(
		d3d12CommandBufferContainer->boundBufferCapacity * sizeof(D3D12Buffer*)
	);

	/* Destroyed resources tracking */

	d3d12CommandBufferContainer->renderbuffersToDestroyCapacity = 16;
	d3d12CommandBufferContainer->renderbuffersToDestroyCount = 0;

	d3d12CommandBufferContainer->renderbuffersToDestroy = (D3D12Renderbuffer**) SDL_malloc(
		sizeof(D3D12Renderbuffer*) *
		d3d12CommandBufferContainer->renderbuffersToDestroyCapacity
	);

	d3d12CommandBufferContainer->buffersToDestroyCapacity = 16;
	d3d12CommandBufferContainer->buffersToDestroyCount = 0;

	d3d12CommandBufferContainer->buffersToDestroy = (D3D12Buffer**) SDL_malloc(
		sizeof(D3D12Buffer*) *
		d3d12CommandBufferContainer->buffersToDestroyCapacity
	);

	d3d12CommandBufferContainer->effectsToDestroyCapacity = 16;
	d3d12CommandBufferContainer->effectsToDestroyCount = 0;

	d3d12CommandBufferContainer->effectsToDestroy = (D3D12Effect**) SDL_malloc(
		sizeof(D3D12Effect*) *
		d3d12CommandBufferContainer->effectsToDestroyCapacity
	);

	d3d12CommandBufferContainer->texturesToDestroyCapacity = 16;
	d3d12CommandBufferContainer->texturesToDestroyCount = 0;

	d3d12CommandBufferContainer->texturesToDestroy = (D3D12Texture**) SDL_malloc(
		sizeof(D3D12Texture*) *
		d3d12CommandBufferContainer->texturesToDestroyCapacity
	);

	return d3d12CommandBufferContainer;
}

static void D3D12_INTERNAL_BeginCommandBuffer(D3D12Renderer *renderer)
{
	HRESULT res;

	SDL_LockMutex(renderer->commandLock);

	/* If we are out of unused command lists, allocate some more */
	if (renderer->inactiveCommandBufferContainerCount == 0)
	{
		renderer->inactiveCommandBufferContainers[renderer->inactiveCommandBufferContainerCount] =
			D3D12_INTERNAL_AllocateCommandBuffer(renderer, 0);

		renderer->inactiveCommandBufferContainerCount += 1;
	}

	renderer->currentCommandBufferContainer =
		renderer->inactiveCommandBufferContainers[renderer->inactiveCommandBufferContainerCount - 1];

	renderer->inactiveCommandBufferContainerCount -= 1;

	res = ID3D12GraphicsCommandList_Reset(
		renderer->currentCommandBufferContainer->commandList,
		renderer->currentCommandBufferContainer->commandAllocator,
		NULL
	);
	ERROR_CHECK("Could not reset command list for recording");

	SDL_UnlockMutex(renderer->commandLock);
}

static void D3D12_INTERNAL_EndCommandBuffer(
	D3D12Renderer *renderer,
	uint8_t startNext,
	uint8_t allowFlush
) {
	HRESULT res;

	SDL_LockMutex(renderer->commandLock);

	res = ID3D12GraphicsCommandList_Close(
		renderer->currentCommandBufferContainer->commandList
	);
	ERROR_CHECK("Could not close command list");

	SDL_UnlockMutex(renderer->commandLock);

	renderer->currentCommandBufferContainer = NULL;
	renderer->numActiveCommands = 0;

	if (allowFlush)
	{
		/* TODO: Figure out how to properly submit commands mid-frame */
	}

	if (startNext)
	{
		D3D12_INTERNAL_BeginCommandBuffer(renderer);
	}
}

/* Renderer Implementation */

/* Quit */

static void D3D12_DestroyDevice(FNA3D_Device *device)
{
	D3D12Renderer* renderer = (D3D12Renderer*) device->driverData;

	/* Release the factory */
	IDXGIFactory1_Release(renderer->factory);

	/* TODO: Release command buffers, allocators, and containers */

	/* Release the queue */
	ID3D12CommandQueue_Release(renderer->unifiedQueue);

	/* Release the device */
	ID3D12Device_Release(renderer->device);

	/* Release the debug interface, if applicable */
	ID3D12Debug_Release(renderer->debug);

	/* Unload the DLLs */
	SDL_UnloadObject(renderer->d3d12_dll);
	SDL_UnloadObject(renderer->dxgi_dll);

	SDL_free(renderer);
	SDL_free(device);
}

/* Presentation */

static void D3D12_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	SDL_assert(0 && "unimplemented");
}

/* Drawing */

static void D3D12_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_DrawIndexedPrimitives(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_DrawInstancedPrimitives(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	SDL_assert(0 && "unimplemented");
}

/* Mutable Render States */

static void D3D12_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12_VIEWPORT vp =
	{
		(float) viewport->x,
		(float) viewport->y,
		(float) viewport->w,
		(float) viewport->h,
		viewport->minDepth,
		viewport->maxDepth
	};

	if (renderer->viewport.x != viewport->x ||
		renderer->viewport.y != viewport->y ||
		renderer->viewport.w != viewport->w ||
		renderer->viewport.h != viewport->h ||
		renderer->viewport.minDepth != viewport->minDepth ||
		renderer->viewport.maxDepth != viewport->maxDepth)
	{
		renderer->viewport = *viewport;
		ID3D12GraphicsCommandList_RSSetViewports(
			renderer->currentCommandBufferContainer->commandList,
			1,
			&vp
		);
	}
}

static void D3D12_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12_RECT rect =
	{
		scissor->x,
		scissor->y,
		scissor->x + scissor->w,
		scissor->y + scissor->h
	};

	/* FIXME: Handle case where rasterizer state scissorTest is disabled */

	if (renderer->scissorRect.x != scissor->x ||
		renderer->scissorRect.y != scissor->y ||
		renderer->scissorRect.w != scissor->w ||
		renderer->scissorRect.h != scissor->h)
	{
		renderer->scissorRect = *scissor;
		ID3D12GraphicsCommandList_RSSetScissorRects(
			renderer->currentCommandBufferContainer->commandList,
			1,
			&rect
		);
	}
}

static void D3D12_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	*blendFactor = renderer->blendFactor;
}

static void D3D12_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	float factor[4];
	if (renderer->blendFactor.r != blendFactor->r ||
		renderer->blendFactor.g != blendFactor->g ||
		renderer->blendFactor.b != blendFactor->b ||
		renderer->blendFactor.a != blendFactor->a)
	{
		factor[0] = blendFactor->r / 255.0f;
		factor[1] = blendFactor->g / 255.0f;
		factor[2] = blendFactor->b / 255.0f;
		factor[3] = blendFactor->a / 255.0f;
		renderer->blendFactor = *blendFactor;
		ID3D12GraphicsCommandList_OMSetBlendFactor(
			renderer->currentCommandBufferContainer->commandList,
			factor
		);
	}
}

static int32_t D3D12_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	return renderer->multiSampleMask;
}

static void D3D12_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	if (renderer->multiSampleMask != mask)
	{
		renderer->multiSampleMask = mask;
		/* FIXME: What should we do here? */
	}
}

static int32_t D3D12_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	return renderer->stencilRef;
}

static void D3D12_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	if (renderer->stencilRef != ref)
	{
		renderer->stencilRef = ref;
		ID3D12GraphicsCommandList_OMSetStencilRef(
			renderer->currentCommandBufferContainer->commandList,
			ref
		);
	}
}

/* Immutable Render States */

static void D3D12_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_VerifyVertexSampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	D3D12_VerifySampler(
		driverData,
		MAX_TEXTURE_SAMPLERS + index,
		texture,
		sampler
	);
}

/* Vertex State */

static void D3D12_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	SDL_assert(0 && "unimplemented");
}

/* Render Targets */

static void D3D12_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveTargetContents
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	SDL_assert(0 && "unimplemented");
}

/* Backbuffer Functions */

static void D3D12_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	SDL_assert(0 && "unimplemented");
}

static FNA3D_SurfaceFormat D3D12_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	SDL_assert(0 && "unimplemented");
	return FNA3D_SURFACEFORMAT_COLOR;
}

static FNA3D_DepthFormat D3D12_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	SDL_assert(0 && "unimplemented");
	return FNA3D_DEPTHFORMAT_NONE;
}

static int32_t D3D12_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	SDL_assert(0 && "unimplemented");
	return 0;
}

/* Textures */

static FNA3D_Texture* D3D12_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static FNA3D_Texture* D3D12_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static FNA3D_Texture* D3D12_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static void D3D12_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetTextureData2D(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetTextureData3D(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetTextureDataCube(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetTextureDataYUV(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetTextureData2D(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetTextureData3D(
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
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetTextureDataCube(
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
	SDL_assert(0 && "unimplemented");
}

/* Renderbuffers */

static FNA3D_Renderbuffer* D3D12_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static FNA3D_Renderbuffer* D3D12_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static void D3D12_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	SDL_assert(0 && "unimplemented");
}

/* Vertex Buffers */

static FNA3D_Buffer* D3D12_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static void D3D12_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	SDL_assert(0 && "unimplemented");
}

/* Index Buffers */

static FNA3D_Buffer* D3D12_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static void D3D12_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	SDL_assert(0 && "unimplemented");
}

/* Effects */

static void D3D12_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	SDL_assert(0 && "unimplemented");
	*effect = NULL;
	*effectData = NULL;
}

static void D3D12_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	SDL_assert(0 && "unimplemented");
	*effect = NULL;
	*effectData = NULL;
}

static void D3D12_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	SDL_assert(0 && "unimplemented");
}

/* Queries */

static FNA3D_Query* D3D12_CreateQuery(FNA3D_Renderer *driverData)
{
	SDL_assert(0 && "unimplemented");
	return NULL;
}

static void D3D12_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDL_assert(0 && "unimplemented");
}

static void D3D12_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	SDL_assert(0 && "unimplemented");
}

static void D3D12_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	SDL_assert(0 && "unimplemented");
}

static uint8_t D3D12_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDL_assert(0 && "unimplemented");
	return 1;
}

static int32_t D3D12_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	SDL_assert(0 && "unimplemented");
	return 0;
}

/* Feature Queries */

static uint8_t D3D12_SupportsDXT1(FNA3D_Renderer *driverData)
{
	/* Required by Feature Level 11.0 */
	return 1;
}

static uint8_t D3D12_SupportsS3TC(FNA3D_Renderer *driverData)
{
	/* DXT3 and DXT5 are required by Feature Level 11.0 */
	return 1;
}

static uint8_t D3D12_SupportsBC7(FNA3D_Renderer *driverData)
{
	/* Required by Feature Level 11.0 */
	return 0;
}

static uint8_t D3D12_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t D3D12_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t D3D12_SupportsSRGBRenderTargets(FNA3D_Renderer *driverData)
{
	/* Required by Feature Level 11.0 */
	return 1;
}

static void D3D12_GetMaxTextureSlots(
	FNA3D_Renderer *driverData,
	int32_t *textures,
	int32_t *vertexTextures
) {
	*textures = D3D12_COMMONSHADER_SAMPLER_SLOT_COUNT;
	*vertexTextures = D3D12_COMMONSHADER_SAMPLER_SLOT_COUNT;
}

static int32_t D3D12_GetMaxMultiSampleCount(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount
) {
	SDL_assert(0 && "unimplemented");
	return 0;
}

/* Debugging */

static void D3D12_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	SDL_assert(0 && "unimplemented");
}

/* External Interop */

static void D3D12_GetSysRenderer(
	FNA3D_Renderer *driverData,
	FNA3D_SysRendererEXT *sysrenderer
) {
	SDL_assert(0 && "unimplemented");
}

static FNA3D_Texture* D3D12_CreateSysTexture(
	FNA3D_Renderer *driverData,
	FNA3D_SysTextureEXT *systexture
) {
	SDL_assert(0 && "unimplemented");
	return NULL;
}

/* Driver */

static uint8_t D3D12_PrepareWindowAttributes(uint32_t *flags)
{
#ifndef _GAMING_XBOX /* We know that D3D12 is always available on Xbox */
	void* module;
	PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceFunc;
	MOJOSHADER_d3d11Context* shaderContext;
	HRESULT res;

	/* Check to see if we can compile HLSL */
	/* FIXME: This will need to change for d3d12. */
	shaderContext = MOJOSHADER_d3d11CreateContext(
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	);
	if (shaderContext == NULL)
	{
		return 0;
	}
	MOJOSHADER_d3d11DestroyContext(shaderContext);

	module = SDL_LoadObject("d3d12.dll");
	if (module == NULL)
	{
		return 0;
	}
	D3D12CreateDeviceFunc = SDL_LoadFunction(module, "D3D12CreateDevice");
	if (D3D12CreateDeviceFunc == NULL)
	{
		SDL_UnloadObject(module);
		return 0;
	}

	res = D3D12CreateDeviceFunc(
		NULL,
		D3D_FEATURE_LEVEL_11_0,
		&D3D_IID_ID3D12Device,
		NULL
	);

	SDL_UnloadObject(module);

	if (FAILED(res))
	{
		FNA3D_LogWarn("D3D12 is unsupported! Error Code: %08X", res);
		return 0;
	}
#endif

	/* No window flags required */
	SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
	return 1;
}

static void D3D12_GetDrawableSize(void* window, int32_t *w, int32_t *h)
{
	SDL_GetWindowSize((SDL_Window*) window, w, h);
}

#ifdef _GAMING_XBOX

static HRESULT D3D12_PLATFORM_CreateD3D12Device(
	D3D12Renderer* renderer,
	uint8_t debugMode
) {
	/* TODO */
	return -1;
}

#else

static HRESULT D3D12_PLATFORM_CreateD3D12Device(
	D3D12Renderer* renderer,
	uint8_t debugMode
) {
	typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY)(const GUID* riid, void** ppFactory);
	PFN_CREATE_DXGI_FACTORY CreateDXGIFactoryFunc;
	PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceFunc;
	PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceFunc;
	IDXGIFactory6 *factory6;
	DXGI_ADAPTER_DESC1 adapterDesc;
	HRESULT res;

	/* Load DXGI... */
	renderer->dxgi_dll = SDL_LoadObject("dxgi.dll");
	if (renderer->dxgi_dll == NULL)
	{
		FNA3D_LogError("Could not find dxgi.dll");
		return -1;
	}

	/* Load CreateFactory... */
	CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY) SDL_LoadFunction(
		renderer->dxgi_dll,
		"CreateDXGIFactory1"
	);
	if (CreateDXGIFactoryFunc == NULL)
	{
		FNA3D_LogError("Could not load function CreateDXGIFactory1");
		return -1;
	}

	/* Create the factory */
	res = CreateDXGIFactoryFunc(
		&D3D_IID_IDXGIFactory1,
		&renderer->factory
	);
	ERROR_CHECK_RETURN("Could not create DXGIFactory1", -1);

	/* Check for DXGIFactory6 support */
	res = IDXGIFactory1_QueryInterface(
		renderer->factory,
		&D3D_IID_IDXGIFactory6,
		&factory6
	);
	if (SUCCEEDED(res))
	{
		IDXGIFactory6_EnumAdapterByGpuPreference(
			factory6,
			0,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			&D3D_IID_IDXGIAdapter1,
			&renderer->adapter
		);
	}
	else
	{
		IDXGIFactory1_EnumAdapters1(
			renderer->factory,
			0,
			&renderer->adapter
		);
	}

	/* Get the adapter description for logging */
	IDXGIAdapter1_GetDesc1(renderer->adapter, &adapterDesc);

	/* Load D3D12... */
	renderer->d3d12_dll = SDL_LoadObject("d3d12.dll");
	if (renderer->d3d12_dll == NULL)
	{
		FNA3D_LogError("Could not find d3d12.dll");
		return -1;
	}

	/* Load the debug interface, if applicable.
	 * This must happen before device creation.
	 */
	if (debugMode)
	{
		/* Load GetDebugInterface... */
		D3D12GetDebugInterfaceFunc = (PFN_D3D12_GET_DEBUG_INTERFACE) SDL_LoadFunction(
			renderer->d3d12_dll,
			"D3D12GetDebugInterface"
		);
		if (D3D12GetDebugInterfaceFunc == NULL)
		{
			FNA3D_LogWarn("Could not load function D3D12GetDebugInterface");
		}
		else
		{
			/* Get the debug interface */
			res = D3D12GetDebugInterfaceFunc(&D3D_IID_ID3D12Debug, &renderer->debug);
			if (FAILED(res))
			{
				FNA3D_LogWarn("Could not get D3D12 debug interface. Error code: 0x%08", res);
			}
		}
	}

	/* Load CreateDevice... */
	D3D12CreateDeviceFunc = (PFN_D3D12_CREATE_DEVICE) SDL_LoadFunction(
		renderer->d3d12_dll,
		"D3D12CreateDevice"
	);
	if (D3D12CreateDeviceFunc == NULL)
	{
		FNA3D_LogError("Could not load function D3D12CreateDevice");
		return -1;
	}

	/* Create the device */
	res = D3D12CreateDeviceFunc(
		(IUnknown*) renderer->adapter,
		D3D_FEATURE_LEVEL_11_0,
		&D3D_IID_ID3D12Device,
		&renderer->device
	);
	ERROR_CHECK_RETURN("Could not create D3D12 device", -1);

	/* Print driver info */
	FNA3D_LogInfo("FNA3D Driver: D3D12");
	FNA3D_LogInfo("D3D12 Adapter: %S", adapterDesc.Description);

	return 0;
}

#endif

static FNA3D_Device* D3D12_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	FNA3D_Device *result;
	D3D12Renderer *renderer;
	D3D12_COMMAND_QUEUE_DESC queueDesc;
	HRESULT res;

	/* Allocate and zero out the renderer */
	renderer = (D3D12Renderer*) SDL_malloc(sizeof(D3D12Renderer));
	SDL_memset(renderer, '\0', sizeof(D3D12Renderer));

	/* Initialize adapters and create the D3D12 device */
	res = D3D12_PLATFORM_CreateD3D12Device(renderer, debugMode);
	ERROR_CHECK_RETURN("Could not create D3D12Device", NULL);

	/* Create the command queue */
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	res = ID3D12Device_CreateCommandQueue(
		renderer->device,
		&queueDesc,
		&D3D_IID_ID3D12CommandQueue,
		&renderer->unifiedQueue
	);
	ERROR_CHECK_RETURN("Could not create D3D12CommandQueue", NULL);

	/* Create the command buffers */
	renderer->inactiveCommandBufferContainerCapacity = 1;
	renderer->inactiveCommandBufferContainers = SDL_malloc(sizeof(D3D12CommandBufferContainer*));
	renderer->inactiveCommandBufferContainerCount = 0;
	renderer->currentCommandCount = 0;

	renderer->submittedCommandBufferContainerCapacity = 1;
	renderer->submittedCommandBufferContainers = SDL_malloc(sizeof(D3D12CommandBufferContainer*));
	renderer->submittedCommandBufferContainerCount = 0;

	renderer->defragCommandBufferContainer = D3D12_INTERNAL_AllocateCommandBuffer(renderer, 1);

	D3D12_INTERNAL_BeginCommandBuffer(renderer);

	/* Initialize renderer members not covered by SDL_memset('\0') */
	renderer->debugMode = debugMode;

	/* Create mutexes */
	renderer->commandLock = SDL_CreateMutex();
	renderer->passLock = SDL_CreateMutex();
	renderer->disposeLock = SDL_CreateMutex();
	renderer->allocatorLock = SDL_CreateMutex();
	renderer->transferLock = SDL_CreateMutex();

	/* Create and return the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	result->driverData = (FNA3D_Renderer*) renderer;
	ASSIGN_DRIVER(D3D12)
	return result;
}

FNA3D_Driver D3D12Driver = {
	"D3D12",
	D3D12_PrepareWindowAttributes,
	D3D12_GetDrawableSize,
	D3D12_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_D3D12 */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
