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
#include <SDL_syswm.h>

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

/* Constant Values */

/* Create descriptor heaps large enough to conceivably contain all the descriptors we would need for a game. */
#define D3D12_INTERNAL_MAX_TEXTURE_COUNT 16384
#define D3D12_INTERNAL_MAX_RT_COUNT 16384

/* IIDs */

static const IID D3D_IID_ID3D12Device = { 0x189819f1,0x1db6,0x4b57,{0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7} };
static const IID D3D_IID_IDXGIFactory2 = { 0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0} };
static const IID D3D_IID_IDXGIFactory6 = { 0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17} };
static const IID D3D_IID_IDXGIAdapter1 = { 0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05} };
static const IID D3D_IID_ID3D12Debug = { 0x344488b7,0x6846,0x474b,{0xb9,0x89,0xf0,0x27,0x44,0x82,0x45,0xe0} };
static const IID D3D_IID_ID3D12DebugDevice = { 0x3febd6dd,0x4973,0x4787,{0x81,0x94,0xe4,0x5f,0x9e,0x28,0x92,0x3e} };
static const IID D3D_IID_ID3D12InfoQueue = { 0x0742a90b,0xc387,0x483f,{0xb9,0x46,0x30,0xa7,0xe4,0xe6,0x14,0x58} };
static const IID D3D_IID_ID3D12CommandQueue = { 0x0ec870a6,0x5d7e,0x4c22,{0x8c,0xfc,0x5b,0xaa,0xe0,0x76,0x16,0xed} };
static const IID D3D_IID_ID3D12CommandAllocator = { 0x6102dee4,0xaf59,0x4b09,{0xb9,0x99,0xb4,0x4d,0x73,0xf0,0x9b,0x24} };
static const IID D3D_IID_ID3D12GraphicsCommandList = { 0x5b160d0f,0xac1b,0x4185,{0x8b,0xa8,0xb3,0xae,0x42,0xa5,0xa4,0x55} };
static const IID D3D_IID_ID3D12Fence = { 0x0a753dcf,0xc4d8,0x4b91,{0xad,0xf6,0xbe,0x5a,0x60,0xd9,0x5a,0x76} };
static const IID D3D_IID_ID3D12Resource = { 0x696442be,0xa72e,0x4059,{0xbc,0x79,0x5b,0x5c,0x98,0x04,0x0f,0xad} };
static const IID D3D_IID_ID3D12DescriptorHeap = { 0x8efb471d,0x616c,0x4f49,{0x90,0xf7,0x12,0x7b,0xb7,0x63,0xfa,0x51} };
static const IID D3D_IID_ID3D12Heap = { 0x6b3b2502,0x6e51,0x45b3,{0x90,0xee,0x98,0x84,0x26,0x5e,0x8d,0xf3} };

/* Internal Structures */

typedef struct D3D12Texture /* Cast FNA3D_Texture* to this! */
{
	ID3D12Resource *resourceHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle;
	D3D12_RESOURCE_STATES resourceState;
	uint8_t isRenderTarget;
	uint8_t rtType;
	uint8_t external;
	FNA3DNAMELESS union
	{
		FNA3D_SurfaceFormat colorFormat;
		FNA3D_DepthFormat depthStencilFormat;
	};
	FNA3DNAMELESS union
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptorHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE dsvDescriptorHandle;
	};
} D3D12Texture;

typedef struct VulkanColorBuffer
{
	D3D12Texture *handle;
	D3D12Texture *multiSampleTexture;
	uint32_t multiSampleCount;
} D3D12ColorBuffer;

typedef struct VulkanDepthStencilBuffer
{
	D3D12Texture *handle;
} D3D12DepthStencilBuffer;

typedef struct D3D12Renderbuffer /* Cast FNA3D_Renderbuffer* to this! */
{
	D3D12ColorBuffer *colorBuffer;
	D3D12DepthStencilBuffer *depthBuffer;
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
	ID3D12GraphicsCommandList* commandList;
	ID3D12CommandAllocator *allocator;
	ID3D12Fence *inFlightFence;
	uint64_t signalValue;

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

typedef struct D3D12SwapchainData
{
	IDXGISwapChain3 *swapchain;
	ID3D12Resource *resourceHandles[2];
	D3D12_CPU_DESCRIPTOR_HANDLE swapchainViews[2];
	D3D12_RESOURCE_STATES resourceStates[2];
	void* windowHandle;
} D3D12SwapchainData;

#define WINDOW_SWAPCHAIN_DATA "FNA3D_D3D12Swapchain"

typedef struct D3D12Backbuffer
{
	uint32_t width;
	uint32_t height;
	uint32_t multiSampleCount;
	D3D12Texture *depthStencilTexture;
	D3D12Texture *colorTexture;
	D3D12Texture *msaaResolveColorTexture;
} D3D12Backbuffer;

typedef struct D3D12Renderer /* Cast FNA3D_Renderer* to this! */
{
	/* Persistent D3D12 Objects */
	void* d3d12_dll;
	ID3D12Device *device;
	ID3D12CommandQueue *commandQueue;

	/* DXGI */
	void* dxgi_dll;
	IDXGIFactory2 *factory;
	IDXGIAdapter1 *adapter;

	/* Window surfaces */
	D3D12SwapchainData **swapchainDatas;
	int32_t swapchainDataCount;
	int32_t swapchainDataCapacity;

	/* The Faux-Backbuffer */
	D3D12Backbuffer backbuffer;

	/* Descriptor Heaps */
	ID3D12DescriptorHeap *srvDescriptorHeap;
	uint32_t srvDescriptorHeapIndex;
	uint64_t srvDescriptorIncrementSize;

	ID3D12DescriptorHeap *rtvDescriptorHeap;
	uint32_t rtvDescriptorHeapIndex;
	uint32_t rtvDescriptorIncrementSize;

	ID3D12DescriptorHeap *dsvDescriptorHeap;
	uint32_t dsvDescriptorHeapIndex;
	uint32_t dsvDescriptorIncrementSize;

	/* Debug */
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

	/* Fences */
	ID3D12Fence *waitIdleFence;
	uint64_t waitIdleFenceValue;
	HANDLE waitIdleEvent;

	/* Dynamic State */
	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;
	FNA3D_Color blendFactor;
	int32_t multiSampleMask;
	int32_t stencilRef;

	/* Threading */
	SDL_mutex *commandLock;
	SDL_mutex *disposeLock;
	SDL_mutex *allocatorLock;
	SDL_mutex *transferLock;

	/* Render Targets */
	int32_t numRenderTargets;
	int32_t multiSampleCount;
	D3D12_CPU_DESCRIPTOR_HANDLE colorViews[MAX_RENDERTARGET_BINDINGS];
	D3D12_CPU_DESCRIPTOR_HANDLE colorMultiSampleViews[MAX_RENDERTARGET_BINDINGS];
	D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView;

	/* Presentation */
	uint8_t syncInterval;

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

/* Forward declarations... */
static void D3D12_AddDisposeTexture(FNA3D_Renderer *driverData, FNA3D_Texture *texture);
static void D3D12_GetDrawableSize(void* window, int32_t *w, int32_t *h);
static void D3D12_INTERNAL_CreateSwapChain(D3D12Renderer *renderer, void* windowHandle, FNA3D_SurfaceFormat format);
static void D3D12_INTERNAL_UpdateSwapchainRT(D3D12Renderer *renderer, D3D12SwapchainData *swapchainData, DXGI_FORMAT format);
static HRESULT D3D12_INTERNAL_DeviceWaitIdle(D3D12Renderer *renderer);
static void D3D12_INTERNAL_PerformDeferredDestroys(D3D12Renderer *renderer, D3D12CommandBufferContainer *d3d12CommandBufferContainer);
static void D3D12_INTERNAL_DisposeBackbuffer(D3D12Renderer* renderer);

/* D3D12: Command Buffers */

static D3D12CommandBufferContainer* D3D12_INTERNAL_AllocateCommandBuffer(
	D3D12Renderer *renderer
) {
	D3D12CommandBufferContainer *d3d12CommandBufferContainer = SDL_malloc(sizeof(D3D12CommandBufferContainer));
	HRESULT res;

	res = ID3D12Device_CreateCommandAllocator(
		renderer->device,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		&D3D_IID_ID3D12CommandAllocator,
		&d3d12CommandBufferContainer->allocator
	);
	ERROR_CHECK_RETURN("Could not create command allocator", NULL);

	res = ID3D12Device_CreateCommandList(
		renderer->device,
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		d3d12CommandBufferContainer->allocator,
		NULL,
		&D3D_IID_ID3D12GraphicsCommandList,
		&d3d12CommandBufferContainer->commandList
	);
	ERROR_CHECK_RETURN("Could not create command list", NULL);

	/* Command lists start in the recording state. */
	ID3D12GraphicsCommandList_Close(d3d12CommandBufferContainer->commandList);

	res = ID3D12Device_CreateFence(
		renderer->device,
		0,
		D3D12_FENCE_FLAG_NONE,
		&D3D_IID_ID3D12Fence,
		&d3d12CommandBufferContainer->inFlightFence
	);
	ERROR_CHECK_RETURN("Could not create fence", NULL);
	d3d12CommandBufferContainer->signalValue = 1;

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
			D3D12_INTERNAL_AllocateCommandBuffer(renderer);

		renderer->inactiveCommandBufferContainerCount += 1;
	}

	renderer->currentCommandBufferContainer =
		renderer->inactiveCommandBufferContainers[renderer->inactiveCommandBufferContainerCount - 1];

	renderer->inactiveCommandBufferContainerCount -= 1;

	res = ID3D12GraphicsCommandList_Reset(
		renderer->currentCommandBufferContainer->commandList,
		renderer->currentCommandBufferContainer->allocator,
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

	res = ID3D12GraphicsCommandList_Close(
		renderer->currentCommandBufferContainer->commandList
	);
	ERROR_CHECK("Could not close command list");

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

static void D3D12_INTERNAL_CleanCommandBuffer(
	D3D12Renderer* renderer,
	D3D12CommandBufferContainer* d3d12CommandBufferContainer
) {
	uint32_t i;

	/* Destroy resources marked for destruction */
	D3D12_INTERNAL_PerformDeferredDestroys(renderer, d3d12CommandBufferContainer);

	/* Remove this command buffer from the submitted list */
	for (i = 0; i < renderer->submittedCommandBufferContainerCount; i += 1)
	{
		if (renderer->submittedCommandBufferContainers[i] == d3d12CommandBufferContainer)
		{
			renderer->submittedCommandBufferContainers[i] = renderer->submittedCommandBufferContainers[renderer->submittedCommandBufferContainerCount - 1];
			renderer->submittedCommandBufferContainerCount -= 1;
			break;
		}
	}

	/* Add this command buffer to the inactive list */
	if (renderer->inactiveCommandBufferContainerCount + 1 > renderer->inactiveCommandBufferContainerCapacity)
	{
		renderer->inactiveCommandBufferContainerCapacity = renderer->inactiveCommandBufferContainerCount + 1;
		renderer->inactiveCommandBufferContainers = SDL_realloc(
			renderer->inactiveCommandBufferContainers,
			renderer->inactiveCommandBufferContainerCapacity * sizeof(D3D12CommandBufferContainer*)
		);
	}

	renderer->inactiveCommandBufferContainers[renderer->inactiveCommandBufferContainerCount] = d3d12CommandBufferContainer;
	renderer->inactiveCommandBufferContainerCount += 1;
}

/* D3D12: Resource Transitions */

static void D3D12_INTERNAL_TransitionIfNeeded(
	D3D12Renderer *renderer,
	ID3D12Resource *resource,
	int32_t subresource,
	D3D12_RESOURCE_STATES *currentState,
	D3D12_RESOURCE_STATES newState
) {
	D3D12_RESOURCE_BARRIER barrier;

	if (*currentState == newState)
	{
		/* Nothing we need to do. */
		return;
	}

	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = *currentState;
	barrier.Transition.StateAfter = newState;
	barrier.Transition.Subresource = subresource;

	ID3D12GraphicsCommandList_ResourceBarrier(
		renderer->currentCommandBufferContainer->commandList,
		1,
		&barrier
	);

	*currentState = newState;
}

/* D3D12: Command Submission */

static void D3D12_INTERNAL_SubmitCommands(
	D3D12Renderer *renderer,
	uint8_t present,
	FNA3D_Rect *sourceRectangle,		/* ignored if present is false */
	FNA3D_Rect *destinationRectangle,	/* ignored if present is false */
	void* windowHandle 					/* ignored if present is false */
) {
	D3D12CommandBufferContainer *containerToSubmit;
	D3D12SwapchainData *swapchainData;
	D3D12Texture *backBufferColorTex;
	int32_t i, backBufferIndex;
	uint64_t fenceValue;

	if (present)
	{
		/* Grab the swapchain data */
		swapchainData = (D3D12SwapchainData*) SDL_GetWindowData(
			(SDL_Window*) windowHandle,
			WINDOW_SWAPCHAIN_DATA
		);

		/* FIXME: This was blindly copy-pasted over from D3D11. Not sure what it does. */
		if (swapchainData == NULL)
		{
			D3D12_INTERNAL_CreateSwapChain(
				renderer,
				(SDL_Window*) windowHandle,
				FNA3D_SURFACEFORMAT_COLOR /* FIXME: Is there something we can use here? */
			);
			swapchainData = (D3D12SwapchainData*) SDL_GetWindowData(
				(SDL_Window*) windowHandle,
				WINDOW_SWAPCHAIN_DATA
			);
			D3D12_INTERNAL_UpdateSwapchainRT(
				renderer,
				swapchainData,
				DXGI_FORMAT_R8G8B8A8_UNORM /* FIXME: No really where can we get this */
			);
		}

		backBufferColorTex = renderer->backbuffer.colorTexture;

		/* Resolve the faux backbuffer, if applicable */
		if (renderer->backbuffer.multiSampleCount > 1)
		{
			/* The MSAA texture needs to be in RESOLVE_SOURCE mode. */
			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				renderer->backbuffer.colorTexture->resourceHandle,
				0,
				&renderer->backbuffer.colorTexture->resourceState,
				D3D12_RESOURCE_STATE_RESOLVE_SOURCE
			);

			/* The resolve texture needs to be in RESOLVE_DEST mode. */
			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				renderer->backbuffer.msaaResolveColorTexture->resourceHandle,
				0,
				&renderer->backbuffer.msaaResolveColorTexture->resourceState,
				D3D12_RESOURCE_STATE_RESOLVE_DEST
			);

			ID3D12GraphicsCommandList_ResolveSubresource(
				renderer->currentCommandBufferContainer->commandList,
				renderer->backbuffer.msaaResolveColorTexture->resourceHandle,
				0,
				renderer->backbuffer.colorTexture->resourceHandle,
				0,
				XNAToD3D_TextureFormat[renderer->backbuffer.msaaResolveColorTexture->colorFormat]
			);
			backBufferColorTex = renderer->backbuffer.msaaResolveColorTexture;
		}

		/* Blit or copy the faux-backbuffer to the real backbuffer */
		backBufferIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(swapchainData->swapchain);

		if (1 /* FIXME: if (faux and real backbuffer dimensions match) */)
		{
			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				swapchainData->resourceHandles[backBufferIndex],
				0,
				&swapchainData->resourceStates[backBufferIndex],
				D3D12_RESOURCE_STATE_COPY_DEST
			);

			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				backBufferColorTex->resourceHandle,
				0,
				&backBufferColorTex->resourceState,
				D3D12_RESOURCE_STATE_COPY_SOURCE
			);

			ID3D12GraphicsCommandList_CopyResource(
				renderer->currentCommandBufferContainer->commandList,
				swapchainData->resourceHandles[backBufferIndex],
				backBufferColorTex->resourceHandle
			);
		}
		else
		{
			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				swapchainData->resourceHandles[backBufferIndex],
				0,
				&swapchainData->resourceStates[backBufferIndex],
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);

			D3D12_INTERNAL_TransitionIfNeeded(
				renderer,
				backBufferColorTex->resourceHandle,
				0,
				&backBufferColorTex->resourceState,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);

			/* FIXME: Blit the faux backbuffer! */
		}

		/* Transition back to normal... */
		/* FIXME: Do we need to transition the msaa resolve texture too? */
		D3D12_INTERNAL_TransitionIfNeeded(
			renderer,
			swapchainData->resourceHandles[backBufferIndex],
			0,
			&swapchainData->resourceStates[backBufferIndex],
			D3D12_RESOURCE_STATE_PRESENT
		);

		D3D12_INTERNAL_TransitionIfNeeded(
			renderer,
			renderer->backbuffer.colorTexture->resourceHandle,
			0,
			&renderer->backbuffer.colorTexture->resourceState,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
	}

	/* Stop recording */
	containerToSubmit = renderer->currentCommandBufferContainer;
	D3D12_INTERNAL_EndCommandBuffer(renderer, 0, 0);

	/* Signal a fence for command buffer completion */
	ID3D12CommandQueue_Signal(
		renderer->commandQueue,
		containerToSubmit->inFlightFence,
		containerToSubmit->signalValue
	);

	/* Check if we can perform any cleanups */
	for (i = renderer->submittedCommandBufferContainerCount - 1; i >= 0; i -= 1)
	{
		fenceValue = ID3D12Fence_GetCompletedValue(renderer->submittedCommandBufferContainers[i]->inFlightFence);

		if (fenceValue == renderer->submittedCommandBufferContainers[i]->signalValue)
		{
			D3D12_INTERNAL_CleanCommandBuffer(
				renderer,
				renderer->submittedCommandBufferContainers[i]
			);

			renderer->submittedCommandBufferContainers[i]->signalValue++;
		}
	}

	/* Execute commands */
	ID3D12CommandQueue_ExecuteCommandLists(
		renderer->commandQueue,
		1,
		(ID3D12CommandList**) &containerToSubmit->commandList
	);

	if (renderer->submittedCommandBufferContainerCount >= renderer->submittedCommandBufferContainerCapacity)
	{
		renderer->submittedCommandBufferContainerCapacity *= 2;
		renderer->submittedCommandBufferContainers = SDL_realloc(
			renderer->submittedCommandBufferContainers,
			renderer->submittedCommandBufferContainerCapacity * sizeof(D3D12CommandBufferContainer*)
		);
	}

	renderer->submittedCommandBufferContainers[renderer->submittedCommandBufferContainerCount] = containerToSubmit;
	renderer->submittedCommandBufferContainerCount += 1;

	/* Actually present! */
	if (present)
	{
		IDXGISwapChain3_Present(
			swapchainData->swapchain,
			renderer->syncInterval,
			0
		);
	}

	/* FIXME: Implement proper fencing */
	D3D12_INTERNAL_DeviceWaitIdle(renderer);

	/* Activate the next command buffer */
	D3D12_INTERNAL_BeginCommandBuffer(renderer);
}

/* D3D12 does not have an equivalent to vkDeviceWaitIdle, so this will have to do... */
static HRESULT D3D12_INTERNAL_DeviceWaitIdle(D3D12Renderer* renderer)
{
	uint64_t fenceValue = renderer->waitIdleFenceValue;
	HRESULT res;

	res = ID3D12CommandQueue_Signal(
		renderer->commandQueue,
		renderer->waitIdleFence,
		fenceValue
	);
	if (FAILED(res))
	{
		return res;
	}

	renderer->waitIdleFenceValue += 1;

	if (ID3D12Fence_GetCompletedValue(renderer->waitIdleFence) < fenceValue)
	{
		res = ID3D12Fence_SetEventOnCompletion(
			renderer->waitIdleFence,
			fenceValue,
			renderer->waitIdleEvent
		);
		if (FAILED(res))
		{
			return res;
		}

		WaitForSingleObject(renderer->waitIdleEvent, INFINITE);
	}

	return S_OK;
}

static void D3D12_INTERNAL_FlushCommands(D3D12Renderer *renderer, uint8_t sync)
{
	HRESULT res;

	SDL_LockMutex(renderer->commandLock);
	SDL_LockMutex(renderer->transferLock);

	D3D12_INTERNAL_SubmitCommands(renderer, 0, NULL, NULL, NULL);

	if (sync)
	{
		res = D3D12_INTERNAL_DeviceWaitIdle(renderer);
		if (FAILED(res))
		{
			FNA3D_LogWarn("DeviceWaitIdle failed. Error Code: %08X", res);
		}
	}

	SDL_UnlockMutex(renderer->commandLock);
	SDL_UnlockMutex(renderer->transferLock);
}

static void D3D12_INTERNAL_FlushCommandsAndPresent(
	D3D12Renderer *renderer,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	SDL_LockMutex(renderer->commandLock);
	SDL_LockMutex(renderer->transferLock);

	D3D12_INTERNAL_SubmitCommands(
		renderer,
		1,
		sourceRectangle,
		destinationRectangle,
		overrideWindowHandle
	);

	SDL_UnlockMutex(renderer->commandLock);
	SDL_UnlockMutex(renderer->transferLock);
}

/* D3D12: Texture Creation */

static uint8_t D3D12_INTERNAL_CreateTexture(
	D3D12Renderer *renderer,
	uint32_t width,
	uint32_t height,
	uint32_t depth,
	uint8_t isCube,
	uint8_t isRenderTarget,
	uint8_t isDepthStencil,
	uint8_t samples,
	uint32_t levelCount,
	DXGI_FORMAT format,
	D3D12Texture *texture
) {
	D3D12_RESOURCE_DESC resourceDesc;
	D3D12_HEAP_PROPERTIES committedHeapProperties;
	D3D12_HEAP_FLAGS committedHeapFlags;
	D3D12_CLEAR_VALUE optimizedClearValue;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	D3D12_RESOURCE_STATES resourceState;
	uint8_t createSRV;
	HRESULT res;

	/* Create the resource description */
	resourceDesc.Alignment = 0; /* Defaults to 64KB for most textures, 4MB for MSAA textures */
	resourceDesc.DepthOrArraySize = (isCube) ? 6 : depth;
	resourceDesc.Dimension = (depth == 1)
		? D3D12_RESOURCE_DIMENSION_TEXTURE2D
		: D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resourceDesc.Format = format;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.MipLevels = levelCount;
	resourceDesc.SampleDesc.Count = samples;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Width = width;
	resourceDesc.Height = height;

	/* We only want SRVs for non-MSAA textures */
	createSRV = (samples == 1);
	if (createSRV)
	{
		/* Create the SRV description */
		srvDesc.Format = format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; /* swizzle */
		if (!isCube)
		{
			if (depth == 1)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = levelCount;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.PlaneSlice = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0;
			}
			else
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				srvDesc.Texture3D.MipLevels = levelCount;
				srvDesc.Texture3D.MostDetailedMip = 0;
				srvDesc.Texture3D.ResourceMinLODClamp = 0;
			}
		}
		else
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MipLevels = levelCount;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = 0;
		}

		/* Get the SRV descriptor handle */
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
			renderer->srvDescriptorHeap,
			&texture->srvDescriptorHandle
		);
		texture->srvDescriptorHandle.ptr += (
			renderer->srvDescriptorIncrementSize *
			renderer->srvDescriptorHeapIndex
			);
		renderer->srvDescriptorHeapIndex++;
	}

	if (isRenderTarget)
	{
		if (isDepthStencil)
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

			dsvDesc.Format = format;
			dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
			if (samples == 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			}
			else
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			}
			dsvDesc.Texture2D.MipSlice = 0;
		}
		else
		{
			resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

			rtvDesc.Format = format;
			if (isCube)
			{
				/* FIXME */
			}
			else
			{
				if (samples == 1)
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				}
				else
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				}
				rtvDesc.Texture2D.PlaneSlice = 0;
				rtvDesc.Texture2D.MipSlice = 0;
			}
		}

		/* Set up committed resource heap information */
		committedHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		committedHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; /* These must be set to UNKNOWN if we're not using a custom heap type */
		committedHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		committedHeapProperties.CreationNodeMask = 0;
		committedHeapProperties.VisibleNodeMask = 0;

		committedHeapFlags = D3D12_HEAP_FLAG_NONE;

		/* Create an optimized clear value */
		SDL_zero(optimizedClearValue.Color);
		optimizedClearValue.DepthStencil.Depth = 0;
		optimizedClearValue.DepthStencil.Stencil = 0;
		optimizedClearValue.Format = format;

		/* FIXME: Cube RTs? */

		/* Create the texture */
		res = ID3D12Device_CreateCommittedResource(
			renderer->device,
			&committedHeapProperties,
			committedHeapFlags,
			&resourceDesc,
			resourceState,
			&optimizedClearValue,
			&D3D_IID_ID3D12Resource,
			&texture->resourceHandle
		);
		ERROR_CHECK_RETURN("Could not create committed resource for Render Target", 0);

		texture->resourceState = resourceState;

		if (isDepthStencil)
		{
			/* Get the DSV descriptor handle */
			ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
				renderer->dsvDescriptorHeap,
				&texture->dsvDescriptorHandle
			);
			texture->dsvDescriptorHandle.ptr += (
				renderer->dsvDescriptorIncrementSize *
				renderer->dsvDescriptorHeapIndex
			);
			renderer->dsvDescriptorHeapIndex++;

			/* Create the render target view */
			ID3D12Device_CreateDepthStencilView(
				renderer->device,
				texture->resourceHandle,
				&dsvDesc,
				texture->dsvDescriptorHandle
			);
		}
		else
		{
			if (createSRV)
			{
				ID3D12Device_CreateShaderResourceView(
					renderer->device,
					texture->resourceHandle,
					&srvDesc,
					texture->srvDescriptorHandle
				);
			}

			/* Get the RTV descriptor handle */
			ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
				renderer->rtvDescriptorHeap,
				&texture->rtvDescriptorHandle
			);
			texture->rtvDescriptorHandle.ptr += (
				renderer->rtvDescriptorIncrementSize *
				renderer->rtvDescriptorHeapIndex
			);
			renderer->rtvDescriptorHeapIndex++;

			/* Create the render target view */
			ID3D12Device_CreateRenderTargetView(
				renderer->device,
				texture->resourceHandle,
				&rtvDesc,
				texture->rtvDescriptorHandle
			);
		}

		return 1;
	}

	/* FIXME: Non-committed textures! */

	return 0;
}

/* D3D12: Resource Destruction */

static void D3D12_INTERNAL_DestroyDescriptor(
	D3D12Renderer *renderer,
	ID3D12DescriptorHeap *heap,
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle
) {
	/* FIXME: For now this is a no-op, since we have no way of "freeing" parts of a descriptor heap. */
}

static void D3D12_INTERNAL_DestroyTexture(
	D3D12Renderer *renderer,
	D3D12Texture *texture
) {
	if (texture->external)
	{
		SDL_free(texture);
		return;
	}

	D3D12_INTERNAL_DestroyDescriptor(
		renderer,
		renderer->srvDescriptorHeap,
		texture->srvDescriptorHandle
	);

	if (texture->isRenderTarget)
	{
		D3D12_INTERNAL_DestroyDescriptor(
			renderer,
			renderer->rtvDescriptorHeap,
			texture->rtvDescriptorHandle
		);

		/* FIXME: Free all the other cube RT views */
	}

	ID3D12Resource_Release(texture->resourceHandle);

	/* FIXME: Free non-committed allocation! */

	SDL_free(texture);
}

static void D3D12_INTERNAL_PerformDeferredDestroys(
	D3D12Renderer *renderer,
	D3D12CommandBufferContainer *d3d12CommandBufferContainer
) {
	uint32_t i;

	/* Destroy submitted resources */

#if 0
	for (i = 0; i < d3d12CommandBufferContainer->renderbuffersToDestroyCount; i += 1)
	{
		D3D12_INTERNAL_DestroyRenderbuffer(
			renderer,
			d3d12CommandBufferContainer->renderbuffersToDestroy[i]
		);
	}
	d3d12CommandBufferContainer->renderbuffersToDestroyCount = 0;

	for (i = 0; i < d3d12CommandBufferContainer->buffersToDestroyCount; i += 1)
	{
		D3D12_INTERNAL_DestroyBuffer(
			renderer,
			d3d12CommandBufferContainer->buffersToDestroy[i]
		);
	}
	d3d12CommandBufferContainer->buffersToDestroyCount = 0;

	for (i = 0; i < d3d12CommandBufferContainer->effectsToDestroyCount; i += 1)
	{
		D3D12_INTERNAL_DestroyEffect(
			renderer,
			d3d12CommandBufferContainer->effectsToDestroy[i]
		);
	}
	d3d12CommandBufferContainer->effectsToDestroyCount = 0;
#endif

	for (i = 0; i < d3d12CommandBufferContainer->texturesToDestroyCount; i += 1)
	{
		D3D12_INTERNAL_DestroyTexture(
			renderer,
			d3d12CommandBufferContainer->texturesToDestroy[i]
		);
	}
	d3d12CommandBufferContainer->texturesToDestroyCount = 0;
}

/* Renderer Implementation */

/* Quit */

static void D3D12_DestroyDevice(FNA3D_Device *device)
{
	D3D12Renderer *renderer = (D3D12Renderer*) device->driverData;
	ID3D12DebugDevice *debugDevice = NULL;
	D3D12CommandBufferContainer *d3d12CommandBufferContainer;
	int32_t i;
	HRESULT res;

	/* Grab the debug device, if applicable */
	if (renderer->debugMode)
	{
		res = ID3D12Device_QueryInterface(
			renderer->device,
			&D3D_IID_ID3D12DebugDevice,
			&debugDevice
		);
		if (FAILED(res))
		{
			FNA3D_LogWarn("Could not get D3D12 debug device for live object reporting. Error: 0x%08", res);
		}
	}

	/* Destroy the faux backbuffer */
	D3D12_INTERNAL_DisposeBackbuffer(renderer);

	/* Release the swapchain */
	for (i = 0; i < renderer->swapchainDataCount; i += 1)
	{
		ID3D12Resource_Release(renderer->swapchainDatas[i]->resourceHandles[0]);
		ID3D12Resource_Release(renderer->swapchainDatas[i]->resourceHandles[1]);
		IDXGISwapChain_Release(renderer->swapchainDatas[i]->swapchain);
	}

	/* Flush any pending commands */
	D3D12_INTERNAL_FlushCommands(renderer, 1);

	/* FIXME: We should wait for all submitted command buffer fences to complete instead of this. */
	D3D12_INTERNAL_DeviceWaitIdle(renderer);

	/* Clear out all the command buffers and associated resources */
	for (i = renderer->submittedCommandBufferContainerCount - 1; i >= 0; i -= 1)
	{
		D3D12_INTERNAL_CleanCommandBuffer(renderer, renderer->submittedCommandBufferContainers[i]);
	}

	/* Release the descriptor heaps */
	ID3D12DescriptorHeap_Release(renderer->srvDescriptorHeap);
	ID3D12DescriptorHeap_Release(renderer->rtvDescriptorHeap);
	ID3D12DescriptorHeap_Release(renderer->dsvDescriptorHeap);

	/* Release the WaitIdle object and fence */
	CloseHandle(renderer->waitIdleEvent);
	ID3D12Fence_Release(renderer->waitIdleFence);

	/* Add the current command buffer to the inactive list */
	if (renderer->inactiveCommandBufferContainerCount + 1 > renderer->inactiveCommandBufferContainerCapacity)
	{
		renderer->inactiveCommandBufferContainerCapacity = renderer->inactiveCommandBufferContainerCount + 1;
		renderer->inactiveCommandBufferContainers = SDL_realloc(
			renderer->inactiveCommandBufferContainers,
			renderer->inactiveCommandBufferContainerCapacity * sizeof(D3D12CommandBufferContainer*)
		);
	}

	renderer->inactiveCommandBufferContainers[renderer->inactiveCommandBufferContainerCount] = renderer->currentCommandBufferContainer;
	renderer->inactiveCommandBufferContainerCount += 1;

	/* Release all the inactive command buffers */
	for (i = 0; i < renderer->inactiveCommandBufferContainerCount; i += 1)
	{
		d3d12CommandBufferContainer = renderer->inactiveCommandBufferContainers[i];

		ID3D12GraphicsCommandList_Release(d3d12CommandBufferContainer->commandList);
		ID3D12Fence_Release(d3d12CommandBufferContainer->inFlightFence);
		ID3D12CommandAllocator_Release(d3d12CommandBufferContainer->allocator);

		SDL_free(d3d12CommandBufferContainer->transferBuffers);
		SDL_free(d3d12CommandBufferContainer->boundBuffers);

		SDL_free(d3d12CommandBufferContainer->renderbuffersToDestroy);
		SDL_free(d3d12CommandBufferContainer->buffersToDestroy);
		SDL_free(d3d12CommandBufferContainer->effectsToDestroy);
		SDL_free(d3d12CommandBufferContainer->texturesToDestroy);

		SDL_free(d3d12CommandBufferContainer);
	}

	SDL_free(renderer->inactiveCommandBufferContainers);
	SDL_free(renderer->submittedCommandBufferContainers);

	/* Release the queue */
	ID3D12CommandQueue_Release(renderer->commandQueue);

	/* Release the device */
	ID3D12Device_Release(renderer->device);

	/* Release the adapter */
	IDXGIAdapter_Release(renderer->adapter);

	/* Release the DXGI factory */
	IDXGIFactory2_Release(renderer->factory);

	/* Report live objects, if we can */
	if (debugDevice != NULL)
	{
		/* This counts as a reference for renderer->device, so the expected final Refcount is 1. */
		ID3D12DebugDevice_ReportLiveDeviceObjects(
			debugDevice,
			D3D12_RLDO_IGNORE_INTERNAL | D3D12_RLDO_DETAIL
		);
		ID3D12DebugDevice_Release(debugDevice);
	}

	/* Unload the DLLs */
	SDL_UnloadObject(renderer->d3d12_dll);
	SDL_UnloadObject(renderer->dxgi_dll);

	/* Delete the FNA3D structs */
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
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;

	D3D12_INTERNAL_FlushCommandsAndPresent(
		renderer,
		sourceRectangle,
		destinationRectangle,
		overrideWindowHandle
	);
}

/* Drawing */

static void D3D12_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12_CLEAR_FLAGS dsClearFlags = 0;
	float clearColor[4];
	int32_t i;

	if (options & FNA3D_CLEAROPTIONS_TARGET)
	{
		clearColor[0] = color->x;
		clearColor[1] = color->y;
		clearColor[2] = color->z;
		clearColor[3] = color->w;

		for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
		{
			if (renderer->colorViews[i].ptr != 0)
			{
				/* Clear the whole RT */
				ID3D12GraphicsCommandList_ClearRenderTargetView(
					renderer->currentCommandBufferContainer->commandList,
					renderer->colorViews[i],
					clearColor,
					0,
					NULL
				);
			}
		}
	}

	if (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER)
	{
		dsClearFlags |= D3D12_CLEAR_FLAG_DEPTH;
	}
	if (options & FNA3D_CLEAROPTIONS_STENCIL)
	{
		dsClearFlags |= D3D12_CLEAR_FLAG_STENCIL;
	}
	if (dsClearFlags != 0 && renderer->depthStencilView.ptr != 0)
	{
		ID3D12GraphicsCommandList_ClearDepthStencilView(
			renderer->currentCommandBufferContainer->commandList,
			renderer->depthStencilView,
			dsClearFlags,
			depth,
			stencil,
			0,
			NULL
		);
	}
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
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12_CPU_DESCRIPTOR_HANDLE views[MAX_RENDERTARGET_BINDINGS];
	D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilView = NULL;
	D3D12Renderbuffer *rb;
	D3D12Texture *tex;
	int32_t i;

	/* Bind the backbuffer, if applicable */
	if (numRenderTargets <= 0)
	{
		views[0] = renderer->backbuffer.colorTexture->rtvDescriptorHandle;

		if (renderer->backbuffer.depthStencilTexture != NULL)
		{
			pDepthStencilView = &renderer->backbuffer.depthStencilTexture->dsvDescriptorHandle;
		}

		ID3D12GraphicsCommandList_OMSetRenderTargets(
			renderer->currentCommandBufferContainer->commandList,
			1,
			views,
			0,
			pDepthStencilView
		);

		renderer->colorViews[0] = views[0];
		for (i = 1; i < MAX_RENDERTARGET_BINDINGS; i += 1)
		{
			renderer->colorViews[i].ptr = 0;
		}
		renderer->numRenderTargets = 1;
		return;
	}

	/* Update color buffers */
	for (i = 0; i < numRenderTargets; i += 1)
	{
		if (renderTargets[i].colorBuffer != NULL)
		{
			rb = (D3D12Renderbuffer*) renderTargets[i].colorBuffer;
			views[i] = rb->colorBuffer->handle->rtvDescriptorHandle;
		}
		else
		{
			tex = (D3D12Texture*) renderTargets[i].texture;
			if (tex->rtType == FNA3D_RENDERTARGET_TYPE_2D)
			{
				views[i] = tex->rtvDescriptorHandle;
			}

			/* FIXME*/
#if 0
			else if (tex->rtType == FNA3D_RENDERTARGET_TYPE_CUBE)
			{
				views[i] = tex->cube.rtViews[
					renderTargets[i].cube.face
				];
			}
#endif
		}
	}
	while (i < MAX_RENDERTARGET_BINDINGS)
	{
		views[i++].ptr = 0;
	}

	/* Update depth stencil buffer */
	renderer->depthStencilView.ptr = 0;
	if (depthStencilBuffer != NULL)
	{
		renderer->depthStencilView = ((D3D12Renderbuffer*)depthStencilBuffer)->depthBuffer->handle->dsvDescriptorHandle;
		pDepthStencilView = &renderer->depthStencilView;
	}

	/* Finally, set the render targets */
	ID3D12GraphicsCommandList_OMSetRenderTargets(
		renderer->currentCommandBufferContainer->commandList,
		numRenderTargets,
		views,
		0,
		pDepthStencilView
	);

	/* Remember color attachments */
	SDL_memcpy(renderer->colorViews, views, sizeof(views));
	renderer->numRenderTargets = numRenderTargets;
}

static void D3D12_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	SDL_assert(0 && "unimplemented");
}

/* Backbuffer Functions */

static void D3D12_INTERNAL_DisposeBackbuffer(D3D12Renderer *renderer)
{
	if (renderer->backbuffer.colorTexture != NULL)
	{
		D3D12_AddDisposeTexture(
			(FNA3D_Renderer*) renderer,
			(FNA3D_Texture*) renderer->backbuffer.colorTexture
		);
		renderer->backbuffer.colorTexture = NULL;
	}

	if (renderer->backbuffer.msaaResolveColorTexture != NULL)
	{
		D3D12_AddDisposeTexture(
			(FNA3D_Renderer*) renderer,
			(FNA3D_Texture*) renderer->backbuffer.msaaResolveColorTexture
		);
		renderer->backbuffer.msaaResolveColorTexture = NULL;
	}

	if (renderer->backbuffer.depthStencilTexture != NULL)
	{
		D3D12_AddDisposeTexture(
			(FNA3D_Renderer*) renderer,
			(FNA3D_Texture*) renderer->backbuffer.depthStencilTexture
		);
	}
}

static void D3D12_INTERNAL_CreateSwapChain(
	D3D12Renderer *renderer,
	void* windowHandle,
	FNA3D_SurfaceFormat format
) {
	IDXGIFactory2 *pParent;
	DXGI_SWAP_CHAIN_DESC1 swapchainDesc;
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc;
	IDXGISwapChain3 *swapchain;
	D3D12SwapchainData *swapchainData;
	HWND dxgiHandle;
	HRESULT res;

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo((SDL_Window*) windowHandle, &info);
	dxgiHandle = info.info.win.window;

	/* Initialize the swapchain descriptor */
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.BufferCount = 2;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.Flags = 0;
	swapchainDesc.Format = XNAToD3D_TextureFormat[format];
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapchainDesc.Stereo = FALSE;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.Width = 0;
	swapchainDesc.Height = 0;

	fullscreenDesc.RefreshRate.Numerator = 0;
	fullscreenDesc.RefreshRate.Denominator = 0;
	fullscreenDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	fullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	fullscreenDesc.Windowed = TRUE;

	/* Create the swapchain! */
	res = IDXGIFactory2_CreateSwapChainForHwnd(
		renderer->factory,
		(IUnknown*) renderer->commandQueue,
		dxgiHandle,
		&swapchainDesc,
		&fullscreenDesc,
		NULL,
		(IDXGISwapChain1**) &swapchain
	);
	ERROR_CHECK("Could not create swapchain")

	/*
	 * The swapchain's parent is a separate factory from the factory that
	 * we used to create the swapchain, and only that parent can be used to
	 * set the window association. Trying to set an association on our factory
	 * will silently fail and doesn't even verify arguments or return errors.
	 * See https://gamedev.net/forums/topic/634235-dxgidisabling-altenter/4999955/
	 */
	res = IDXGISwapChain3_GetParent(
		swapchain,
		&D3D_IID_IDXGIFactory2,
		(void**) &pParent
	);
	if (FAILED(res))
	{
		FNA3D_LogWarn(
			"Could not get swapchain parent! Error Code: %08X",
			res
		);
	}
	else
	{
		/* Disable DXGI window crap */
		res = IDXGIFactory2_MakeWindowAssociation(
			pParent,
			dxgiHandle,
			DXGI_MWA_NO_WINDOW_CHANGES
		);
		if (FAILED(res))
		{
			FNA3D_LogWarn(
				"MakeWindowAssociation failed! Error Code: %08X",
				res
			);
		}

		IDXGIFactory2_Release(pParent);
	}

	swapchainData = (D3D12SwapchainData*) SDL_malloc(sizeof(D3D12SwapchainData));
	swapchainData->swapchain = swapchain;
	swapchainData->windowHandle = windowHandle;
	swapchainData->swapchainViews[0].ptr = 0;
	swapchainData->swapchainViews[1].ptr = 0;
	swapchainData->resourceStates[0] = D3D12_RESOURCE_STATE_COMMON;
	swapchainData->resourceStates[1] = D3D12_RESOURCE_STATE_COMMON;
	swapchainData->resourceHandles[0] = NULL;
	swapchainData->resourceHandles[1] = NULL;
	SDL_SetWindowData(
		(SDL_Window*) windowHandle,
		WINDOW_SWAPCHAIN_DATA,
		swapchainData
	);
	if (renderer->swapchainDataCount >= renderer->swapchainDataCapacity)
	{
		renderer->swapchainDataCapacity *= 2;
		renderer->swapchainDatas = SDL_realloc(
			renderer->swapchainDatas,
			renderer->swapchainDataCapacity * sizeof(D3D12SwapchainData*)
		);
	}
	renderer->swapchainDatas[renderer->swapchainDataCount] = swapchainData;
	renderer->swapchainDataCount += 1;
}

static void D3D12_INTERNAL_UpdateSwapchainRT(
	D3D12Renderer *renderer,
	D3D12SwapchainData *swapchainData,
	DXGI_FORMAT format
) {
	HRESULT res;
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
	int32_t i;

	/* Create a render target view for the swapchain */
	rtvDesc.Format = (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
		? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
		: DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	for (i = 0; i < 2; i += 1)
	{
		res = IDXGISwapChain3_GetBuffer(
			swapchainData->swapchain,
			i,
			&D3D_IID_ID3D12Resource,
			(void**) &swapchainData->resourceHandles[i]
		);
		ERROR_CHECK_RETURN("Could not get buffer from swapchain", )

		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
			renderer->rtvDescriptorHeap,
			&swapchainData->swapchainViews[i]
		);
		swapchainData->swapchainViews[i].ptr += (
			renderer->rtvDescriptorIncrementSize *
			renderer->rtvDescriptorHeapIndex
		);
		renderer->rtvDescriptorHeapIndex += 1;

		ID3D12Device_CreateRenderTargetView(
			renderer->device,
			swapchainData->resourceHandles[i],
			&rtvDesc,
			swapchainData->swapchainViews[i]
		);
	}
}

static void D3D12_INTERNAL_CreateBackbuffer(
	D3D12Renderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	D3D12SwapchainData *swapchainData;
	HRESULT res;

	/* Dispose of the existing backbuffer in preparation for the new one. */
	D3D12_INTERNAL_DisposeBackbuffer(renderer);

	/* Create or update the swapchain */
	if (presentationParameters->deviceWindowHandle != NULL)
	{
		swapchainData = (D3D12SwapchainData*) SDL_GetWindowData(
			(SDL_Window*) presentationParameters->deviceWindowHandle,
			WINDOW_SWAPCHAIN_DATA
		);
		if (swapchainData == NULL)
		{
			D3D12_INTERNAL_CreateSwapChain(
				renderer,
				presentationParameters->deviceWindowHandle,
				FNA3D_SURFACEFORMAT_COLOR
			);
			swapchainData = (D3D12SwapchainData*) SDL_GetWindowData(
				(SDL_Window*) presentationParameters->deviceWindowHandle,
				WINDOW_SWAPCHAIN_DATA
			);
		}
		else
		{
			/* Release the existing descriptors */
			D3D12_INTERNAL_DestroyDescriptor(
				renderer,
				renderer->rtvDescriptorHeap,
				swapchainData->swapchainViews[0]
			);
			D3D12_INTERNAL_DestroyDescriptor(
				renderer,
				renderer->rtvDescriptorHeap,
				swapchainData->swapchainViews[1]
			);
			swapchainData->swapchainViews[0].ptr = 0;
			swapchainData->swapchainViews[1].ptr = 0;

			/* Release the existing resource handles */
			IDXGIResource_Release(swapchainData->resourceHandles[0]);
			IDXGIResource_Release(swapchainData->resourceHandles[1]);

			/* Resize the swapchain to the new window size */
			res = IDXGISwapChain3_ResizeBuffers(
				swapchainData->swapchain,
				0,			/* keep # of buffers the same */
				0,			/* get width from window */
				0,			/* get height from window */
				DXGI_FORMAT_UNKNOWN,	/* keep the old format */
				0
			);
			ERROR_CHECK_RETURN("Could not resize swapchain", );
		}
	}
	else
	{
		/* Nothing to update, skip everything involving this */
		swapchainData = NULL;
	}

	renderer->backbuffer.width = presentationParameters->backBufferWidth;
	renderer->backbuffer.height = presentationParameters->backBufferHeight;
	renderer->backbuffer.multiSampleCount = presentationParameters->multiSampleCount;

	/* FIXME: Do these actually need to be allocated on the heap? */
	renderer->backbuffer.colorTexture = (D3D12Texture*) SDL_malloc(
		sizeof(D3D12Texture)
	);

	if (!D3D12_INTERNAL_CreateTexture(
		renderer,
		presentationParameters->backBufferWidth,
		presentationParameters->backBufferHeight,
		1,
		0,
		1,
		0,
		SDL_max(1, renderer->backbuffer.multiSampleCount),
		1,
		XNAToD3D_TextureFormat[presentationParameters->backBufferFormat],
		renderer->backbuffer.colorTexture
	)) {
		FNA3D_LogError("Failed to create faux backbuffer color attachment");
		return;
	}

	renderer->backbuffer.msaaResolveColorTexture = NULL;
	if (renderer->backbuffer.multiSampleCount > 1)
	{
		renderer->backbuffer.msaaResolveColorTexture = (D3D12Texture*) SDL_malloc(
			sizeof(D3D12Texture)
		);

		if (!D3D12_INTERNAL_CreateTexture(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			1,
			0,
			1,
			0,
			1,
			1,
			XNAToD3D_TextureFormat[presentationParameters->backBufferFormat],
			renderer->backbuffer.msaaResolveColorTexture
		)) {
			FNA3D_LogError("Failed to create faux backbuffer multisample resolve color attachment");
			return;
		}
	}

	renderer->backbuffer.depthStencilTexture = NULL;
	if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		renderer->backbuffer.depthStencilTexture = (D3D12Texture*) SDL_malloc(
			sizeof(D3D12Texture)
		);

		if (!D3D12_INTERNAL_CreateTexture(
			renderer,
			presentationParameters->backBufferWidth,
			presentationParameters->backBufferHeight,
			1,
			0,
			1,
			1,
			SDL_max(1, renderer->backbuffer.multiSampleCount),
			1,
			XNAToD3D_DepthFormat[presentationParameters->depthStencilFormat],
			renderer->backbuffer.depthStencilTexture
		)) {
			FNA3D_LogError("Failed to create faux backbuffer depth stencil attachment");
			return;
		}
	}

	if (swapchainData != NULL)
	{
		D3D12_INTERNAL_UpdateSwapchainRT(
			renderer,
			swapchainData,
			XNAToD3D_TextureFormat[presentationParameters->backBufferFormat]
		);
	}

	/* This is the default render target */
	D3D12_SetRenderTargets(
		(FNA3D_Renderer*) renderer,
		NULL,
		0,
		NULL,
		FNA3D_DEPTHFORMAT_NONE,
		0
	);
}

static void D3D12_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;

	D3D12_INTERNAL_FlushCommands(renderer, 1);

	D3D12_INTERNAL_CreateBackbuffer(renderer, presentationParameters);

	/* FIXME: Is this necessary? This is how it's done in Vulkan... */
	D3D12_INTERNAL_FlushCommands(renderer, 1);
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
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	*w = renderer->backbuffer.width;
	*h = renderer->backbuffer.height;
}

static FNA3D_SurfaceFormat D3D12_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	return renderer->backbuffer.colorTexture->colorFormat;
}

static FNA3D_DepthFormat D3D12_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	if (renderer->backbuffer.depthStencilTexture == NULL)
	{
		return FNA3D_DEPTHFORMAT_NONE;
	}
	return renderer->backbuffer.depthStencilTexture->depthStencilFormat;
}

static int32_t D3D12_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	return renderer->backbuffer.multiSampleCount;
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
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12Texture *d3d12Texture = (D3D12Texture*) texture;
	uint32_t i;

	/* Unbind the texture if it's being used as an RT. */
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (renderer->colorViews[i].ptr == d3d12Texture->rtvDescriptorHandle.ptr)
		{
			renderer->colorViews[i].ptr = 0;
		}
	}

	/* FIXME */
#if 0
	for (i = 0; i < MAX_TOTAL_SAMPLERS; i += 1)
	{
		if (d3d12Texture == renderer->textures[i])
		{
			renderer->textures[i] = &NullTexture;
			renderer->textureNeedsUpdate[i] = 1;
		}
	}
#endif

	/* Queue texture for destruction */
	SDL_LockMutex(renderer->commandLock);
	if (renderer->currentCommandBufferContainer->texturesToDestroyCount + 1 >= renderer->currentCommandBufferContainer->texturesToDestroyCapacity)
	{
		renderer->currentCommandBufferContainer->texturesToDestroyCapacity *= 2;

		renderer->currentCommandBufferContainer->texturesToDestroy = SDL_realloc(
			renderer->currentCommandBufferContainer->texturesToDestroy,
			sizeof(D3D12Texture*) * renderer->currentCommandBufferContainer->texturesToDestroyCapacity
		);
	}
	renderer->currentCommandBufferContainer->texturesToDestroy[renderer->currentCommandBufferContainer->texturesToDestroyCount] = d3d12Texture;
	renderer->currentCommandBufferContainer->texturesToDestroyCount += 1;
	SDL_UnlockMutex(renderer->commandLock);
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
	D3D12Renderer *renderer = (D3D12Renderer*) driverData;
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS featureData;

	featureData.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	featureData.Format = XNAToD3D_TextureFormat[format];
	featureData.NumQualityLevels = 0;
	featureData.SampleCount = multiSampleCount;

	do
	{
		ID3D12Device_CheckFeatureSupport(
			renderer->device,
			D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
			&featureData,
			sizeof(featureData)
		);
		if (featureData.NumQualityLevels > 0)
		{
			break;
		}
		featureData.SampleCount >>= 1;
	} while (featureData.SampleCount > 0);
	return featureData.SampleCount;
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
	typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY)(UINT flags, const GUID* riid, void** ppFactory);
	PFN_CREATE_DXGI_FACTORY CreateDXGIFactoryFunc;
	PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceFunc;
	PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceFunc;
	IDXGIFactory6 *factory6;
	DXGI_ADAPTER_DESC1 adapterDesc;
	ID3D12Debug *debugInterface;
	ID3D12InfoQueue *infoQueue;
	D3D12_INFO_QUEUE_FILTER infoQueueFilter;
	D3D12_MESSAGE_ID infoQueueDenyMessages[] =
	{
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE
	};
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
		"CreateDXGIFactory2"
	);
	if (CreateDXGIFactoryFunc == NULL)
	{
		FNA3D_LogError("Could not load function CreateDXGIFactory2");
		return -1;
	}

	/* Create the factory */
	res = CreateDXGIFactoryFunc(
		debugMode,
		&D3D_IID_IDXGIFactory2,
		&renderer->factory
	);
	ERROR_CHECK_RETURN("Could not create DXGIFactory2", -1);

	/* Check for DXGIFactory6 support */
	res = IDXGIFactory2_QueryInterface(
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
		IDXGIFactory6_Release(factory6);
	}
	else
	{
		IDXGIFactory2_EnumAdapters1(
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
		/* Load D3D12GetDebugInterface... */
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
			res = D3D12GetDebugInterfaceFunc(&D3D_IID_ID3D12Debug, &debugInterface);
			if (FAILED(res))
			{
				FNA3D_LogWarn("Could not get D3D12 debug interface. Error code: 0x%08", res);
			}
			else
			{
				/* Enable the debug layer */
				ID3D12Debug_EnableDebugLayer(debugInterface);
				ID3D12Debug_Release(debugInterface);
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

	/* Silence unhelpful debug warnings */
	if (debugMode)
	{
		res = ID3D12Device_QueryInterface(renderer->device, &D3D_IID_ID3D12InfoQueue, &infoQueue);
		if (FAILED(res))
		{
			FNA3D_LogWarn("Could not get D3D12 debug info queue. Error code: 0x%08", res);
		}
		else
		{
			SDL_zero(infoQueueFilter);
			infoQueueFilter.DenyList.NumIDs = SDL_arraysize(infoQueueDenyMessages);
			infoQueueFilter.DenyList.pIDList = infoQueueDenyMessages;

			ID3D12InfoQueue_AddStorageFilterEntries(infoQueue, &infoQueueFilter);
			ID3D12InfoQueue_Release(infoQueue);
		}
	}

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
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc;
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
		&renderer->commandQueue
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

	D3D12_INTERNAL_BeginCommandBuffer(renderer);

	/* Create the SRV descriptor heap */
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descriptorHeapDesc.NodeMask = 0;
	descriptorHeapDesc.NumDescriptors = D3D12_INTERNAL_MAX_TEXTURE_COUNT;
	res = ID3D12Device_CreateDescriptorHeap(
		renderer->device,
		&descriptorHeapDesc,
		&D3D_IID_ID3D12DescriptorHeap,
		&renderer->srvDescriptorHeap
	);
	ERROR_CHECK_RETURN("Could not create SRV descriptor heap", NULL);

	renderer->srvDescriptorHeapIndex = 0;
	renderer->srvDescriptorIncrementSize = ID3D12Device_GetDescriptorHandleIncrementSize(
		renderer->device,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
	);

	/* Create the RTV descriptor heap */
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descriptorHeapDesc.NodeMask = 0;
	descriptorHeapDesc.NumDescriptors = D3D12_INTERNAL_MAX_RT_COUNT;
	res = ID3D12Device_CreateDescriptorHeap(
		renderer->device,
		&descriptorHeapDesc,
		&D3D_IID_ID3D12DescriptorHeap,
		&renderer->rtvDescriptorHeap
	);
	ERROR_CHECK_RETURN("Could not create RTV descriptor heap", NULL);

	renderer->rtvDescriptorHeapIndex = 0;
	renderer->rtvDescriptorIncrementSize = ID3D12Device_GetDescriptorHandleIncrementSize(
		renderer->device,
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV
	);

	/* Create the DSV descriptor heap */
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	descriptorHeapDesc.NodeMask = 0;
	descriptorHeapDesc.NumDescriptors = D3D12_INTERNAL_MAX_RT_COUNT;
	res = ID3D12Device_CreateDescriptorHeap(
		renderer->device,
		&descriptorHeapDesc,
		&D3D_IID_ID3D12DescriptorHeap,
		&renderer->dsvDescriptorHeap
	);
	ERROR_CHECK_RETURN("Could not create DSV descriptor heap", NULL);

	renderer->dsvDescriptorHeapIndex = 0;
	renderer->dsvDescriptorIncrementSize = ID3D12Device_GetDescriptorHandleIncrementSize(
		renderer->device,
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV
	);

	/* Initialize renderer members not covered by SDL_memset('\0') */
	renderer->debugMode = debugMode;

	/* Create initial faux-backbuffer */
	renderer->swapchainDataCapacity = 1;
	renderer->swapchainDataCount = 0;
	renderer->swapchainDatas = SDL_malloc(renderer->swapchainDataCapacity * sizeof(D3D12SwapchainData*));
	D3D12_INTERNAL_CreateBackbuffer(renderer, presentationParameters);

	/* FIXME: Create any pipeline resources required for the faux backbuffer */

	/* Create mutexes */
	renderer->commandLock = SDL_CreateMutex();
	renderer->disposeLock = SDL_CreateMutex();
	renderer->allocatorLock = SDL_CreateMutex();
	renderer->transferLock = SDL_CreateMutex();

	/* Create WaitIdle fence and event */
	res = ID3D12Device_CreateFence(
		renderer->device,
		0,
		D3D12_FENCE_FLAG_NONE,
		&D3D_IID_ID3D12Fence,
		&renderer->waitIdleFence
	);
	ERROR_CHECK_RETURN("Could not create WaitIdle fence", NULL);

	renderer->waitIdleFenceValue = 1;
	renderer->waitIdleEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (renderer->waitIdleEvent == NULL)
	{
		res = HRESULT_FROM_WIN32(GetLastError());
		ERROR_CHECK_RETURN("Could not create WaitIdle event", NULL);
	}

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
