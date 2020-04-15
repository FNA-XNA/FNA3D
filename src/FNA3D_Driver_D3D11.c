/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020 Ethan Lee
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

#if FNA3D_DRIVER_D3D11

#include "FNA3D_Driver.h"
#include "FNA3D_PipelineCache.h"
#include "stb_ds.h"

#include <SDL.h>
#include <SDL_syswm.h>

#define D3D11_NO_HELPERS
#define CINTERFACE
#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>

/* Internal Structures */

typedef struct D3D11Texture /* Cast FNA3D_Texture* to this! */
{
	union
	{
		ID3D11Texture2D *h2D;
		ID3D11Texture3D *h3D;
	} handle;
	int32_t levelCount;
	uint8_t isRenderTarget;
} D3D11Texture;

typedef struct D3D11Renderbuffer /* Cast FNA3D_Renderbuffer* to this! */
{
	uint8_t filler;
} D3D11Renderbuffer;

typedef struct D3D11Buffer /* Cast FNA3D_Buffer* to this! */
{
	ID3D11Buffer *handle;
} D3D11Buffer;

typedef struct D3D11Effect /* Cast FNA3D_Effect* to this! */
{
	MOJOSHADER_effect *effect;
} D3D11Effect;

typedef struct D3D11Query /* Cast FNA3D_Query* to this! */
{
	uint8_t filler;
} D3D11Query;

typedef struct D3D11Renderer /* Cast FNA3D_Renderer* to this! */
{
	/* Persistent D3D11 Objects */
	ID3D11Device *device;
	ID3D11DeviceContext *context;
	void *factory; /* IDXGIFactory2*, not defined in DirectX SDK */

	/* Capabilities */
	uint32_t supportsDxt1;
	uint32_t supportsS3tc;
	int32_t maxMultiSampleCount;
	D3D_FEATURE_LEVEL featureLevel;

	/* Presentation */
	uint8_t syncInterval;

	/* Blend State */
	FNA3D_Color blendFactor;
	int32_t multiSampleMask;

	/* Depth Stencil State */
	int32_t stencilRef;

	/* Resource Caches */
	StateHashMap *blendStateCache;
	StateHashMap *depthStencilStateCache;
	StateHashMap *rasterizerStateCache;
	StateHashMap *samplerStateCache;

	/* Render Targets */
	int32_t numRenderTargets;
	ID3D11RenderTargetView *renderTargetViews[MAX_RENDERTARGET_BINDINGS];
	ID3D11DepthStencilView *depthStencilView;
	FNA3D_DepthFormat currentDepthFormat;
} D3D11Renderer;

/* XNA->D3D11 Translation Arrays */

#ifndef DXGI_FORMAT_B4G4R4A4_UNORM
#define DXGI_FORMAT_B4G4R4A4_UNORM (DXGI_FORMAT) 115
#endif

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
	"SV_POSITION",			/* VertexElementUsage.Position */
	"SV_TARGET",			/* VertexElementUsage.Color */
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
	DXGI_FORMAT_R16G16_FLOAT,	/* VertexElementFormat.Single */
	DXGI_FORMAT_R32G32_FLOAT,	/* VertexElementFormat.Vector2 */
	DXGI_FORMAT_R32G32B32_FLOAT,	/* VertexElementFormat.Vector3 */
	DXGI_FORMAT_R32G32B32A32_FLOAT,	/* VertexElementFormat.Vector4 */
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* VertexElementFormat.Color */
	DXGI_FORMAT_R8G8B8A8_UNORM,	/* VertexElementFormat.Byte4 */
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

static D3D11_BLEND XNAToD3D_BlendMode[] =
{
	D3D11_BLEND_ONE,		/* Blend.One */
	D3D11_BLEND_ZERO,		/* Blend.Zero */
	D3D11_BLEND_SRC_COLOR,		/* Blend.SourceColor */
	D3D11_BLEND_INV_SRC_COLOR,	/* Blend.InverseSourceColor */
	D3D11_BLEND_SRC_ALPHA,		/* Blend.SourceAlpha */
	D3D11_BLEND_INV_SRC_ALPHA,	/* Blend.InverseSourceAlpha */
	D3D11_BLEND_DEST_COLOR,		/* Blend.DestinationColor */
	D3D11_BLEND_INV_DEST_COLOR,	/* Blend.InverseDestinationColor */
	D3D11_BLEND_DEST_ALPHA,		/* Blend.DestinationAlpha */
	D3D11_BLEND_INV_DEST_ALPHA,	/* Blend.InverseDestinationAlpha */
	D3D11_BLEND_BLEND_FACTOR,	/* Blend.BlendFactor */
	D3D11_BLEND_INV_BLEND_FACTOR,	/* Blend.InverseBlendFactor */
	D3D11_BLEND_SRC_ALPHA_SAT	/* Blend.SourceAlphaSaturation */
};

static D3D11_BLEND_OP XNAToD3D_BlendOperation[] =
{
	D3D11_BLEND_OP_ADD,		/* BlendFunction.Add */
	D3D11_BLEND_OP_SUBTRACT,	/* BlendFunction.Subtract */
	D3D11_BLEND_OP_REV_SUBTRACT,	/* BlendFunction.ReverseSubtract */
	D3D11_BLEND_OP_MAX,		/* BlendFunction.Max */
	D3D11_BLEND_OP_MIN		/* BlendFunction.Min */
};

static D3D11_COMPARISON_FUNC XNAToD3D_CompareFunc[] =
{
	D3D11_COMPARISON_ALWAYS,	/* CompareFunction.Always */
	D3D11_COMPARISON_NEVER,		/* CompareFunction.Never */
	D3D11_COMPARISON_LESS,		/* CompareFunction.Less */
	D3D11_COMPARISON_LESS_EQUAL,	/* CompareFunction.LessEqual */
	D3D11_COMPARISON_EQUAL,		/* CompareFunction.Equal */
	D3D11_COMPARISON_GREATER_EQUAL,	/* CompareFunction.GreaterEqual */
	D3D11_COMPARISON_GREATER,	/* CompareFunction.Greater */
	D3D11_COMPARISON_NOT_EQUAL	/* CompareFunction.NotEqual */
};

static D3D11_STENCIL_OP XNAToD3D_StencilOp[] =
{
	D3D11_STENCIL_OP_KEEP,		/* StencilOperation.Keep */
	D3D11_STENCIL_OP_ZERO,		/* StencilOperation.Zero */
	D3D11_STENCIL_OP_REPLACE,	/* StencilOperation.Replace */
	D3D11_STENCIL_OP_INCR,		/* StencilOperation.Increment */
	D3D11_STENCIL_OP_DECR,		/* StencilOperation.Decrement */
	D3D11_STENCIL_OP_INCR_SAT,	/* StencilOperation.IncrementSaturation */
	D3D11_STENCIL_OP_DECR_SAT,	/* StencilOperation.DecrementSaturation */
	D3D11_STENCIL_OP_INVERT		/* StencilOperation.Invert */
};

static D3D11_FILL_MODE XNAToD3D_FillMode[] =
{
	D3D11_FILL_SOLID,		/* FillMode.Solid */
	D3D11_FILL_WIREFRAME		/* FillMode.WireFrame */
};

static float XNAToD3D_DepthBiasScale[] =
{
	0.0f,				/* DepthFormat.None */
	(float) ((1 << 16) - 1),	/* DepthFormat.Depth16 */
	(float) ((1 << 24) - 1),	/* DepthFormat.Depth24 */
	(float) ((1 << 24) - 1) 	/* DepthFormat.Depth24Stencil8 */
};

static D3D11_CULL_MODE XNAToD3D_CullMode[] =
{
	D3D11_CULL_NONE,		/* CullMode.None */
	D3D11_CULL_BACK,		/* CullMode.CullClockwiseFace */
	D3D11_CULL_FRONT 		/* CullMode.CullCounterClockwiseFace */
};

static D3D11_TEXTURE_ADDRESS_MODE XNAToD3D_Wrap[] =
{
	D3D11_TEXTURE_ADDRESS_WRAP,	/* TextureAddressMode.Wrap */
	D3D11_TEXTURE_ADDRESS_CLAMP,	/* TextureAddressMode.Clamp */
	D3D11_TEXTURE_ADDRESS_MIRROR	/* TextureAddressMode.Mirror */
};

static D3D11_FILTER XNAToD3D_Filter[] =
{
	D3D11_FILTER_MIN_MAG_MIP_LINEAR,		/* TextureFilter.Linear */
	D3D11_FILTER_MIN_MAG_MIP_POINT,			/* TextureFilter.Point */
	D3D11_FILTER_ANISOTROPIC,			/* TextureFilter.Anisotropic */
	D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,		/* TextureFilter.LinearMipPoint */
	D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,		/* TextureFilter.PointMipLinear */
	D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,	/* TextureFilter.MinLinearMagPointMipLinear */
	D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,		/* TextureFilter.MinLinearMagPointMipPoint */
	D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,		/* TextureFilter.MinPointMagLinearMipLinear */
	D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT	/* TextureFilter.MinPointMagLinearMipPoint */
};

static D3D_PRIMITIVE_TOPOLOGY XNAToD3D_Primitive[] =
{
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,	/* PrimitiveType.TriangleList */
	D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,	/* PrimitiveType.TriangleStrip */
	D3D_PRIMITIVE_TOPOLOGY_LINELIST,	/* PrimitiveType.LineList */
	D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,	/* PrimitiveType.LineStrip */
	D3D_PRIMITIVE_TOPOLOGY_POINTLIST	/* PrimitiveType.PointListEXT */
};

/* IID Imports */

static const GUID D3D_IID_IDXGIFactory2 = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };

/* Texture Helper Functions */

static inline int32_t BytesPerRow(
	int32_t width,
	FNA3D_SurfaceFormat format
) {
	int32_t blocksPerRow = width;

	if (	format == FNA3D_SURFACEFORMAT_DXT1 ||
		format == FNA3D_SURFACEFORMAT_DXT3 ||
		format == FNA3D_SURFACEFORMAT_DXT5	)
	{
		blocksPerRow = (width + 3) / 4;
	}

	return blocksPerRow * Texture_GetFormatSize(format);
}

static inline int32_t BytesPerDepthSlice(
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format
) {
	int32_t blocksPerRow = width;
	int32_t blocksPerColumn = height;

	if (	format == FNA3D_SURFACEFORMAT_DXT1 ||
		format == FNA3D_SURFACEFORMAT_DXT3 ||
		format == FNA3D_SURFACEFORMAT_DXT5	)
	{
		blocksPerRow = (width + 3) / 4;
		blocksPerColumn = (height + 3) / 4;
	}

	return blocksPerRow * blocksPerColumn * Texture_GetFormatSize(format);
}

/* Pipeline State Object Caching */

static ID3D11BlendState* FetchBlendState(
	D3D11Renderer *renderer,
	FNA3D_BlendState *state
) {
	StateHash hash;
	D3D11_BLEND_DESC desc;
	ID3D11BlendState *result;

	/* Can we just reuse an existing state? */
	hash = GetBlendStateHash(*state);
	result = hmget(renderer->blendStateCache, hash);
	if (result != NULL)
	{
		/* The state is already cached! */
		return result;
	}

	/* We need to make a new blend state... */
	desc.AlphaToCoverageEnable = 0;
	desc.IndependentBlendEnable = 0;
	desc.RenderTarget[0].BlendEnable = !(
		state->colorSourceBlend == FNA3D_BLEND_ONE &&
		state->colorDestinationBlend == FNA3D_BLEND_ZERO &&
		state->alphaSourceBlend == FNA3D_BLEND_ONE &&
		state->alphaDestinationBlend == FNA3D_BLEND_ZERO
	);
	desc.RenderTarget[0].BlendOp = XNAToD3D_BlendOperation[
		state->colorBlendFunction
	];
	desc.RenderTarget[0].BlendOpAlpha = XNAToD3D_BlendOperation[
		state->alphaBlendFunction
	];
	desc.RenderTarget[0].DestBlend = XNAToD3D_BlendMode[
		state->colorDestinationBlend
	];
	desc.RenderTarget[0].DestBlendAlpha = XNAToD3D_BlendMode[
		state->alphaDestinationBlend
	];
	desc.RenderTarget[0].RenderTargetWriteMask = (
		(uint32_t) state->colorWriteEnable
	);
	/* FIXME: For colorWriteEnable1/2/3, we'll need
	 * to loop over all render target descriptors
	 * and apply the same state, except for the mask.
	 * Ugh. -caleb
	 */
	desc.RenderTarget[0].SrcBlend = XNAToD3D_BlendMode[
		state->colorSourceBlend
	];
	desc.RenderTarget[0].SrcBlendAlpha = XNAToD3D_BlendMode[
		state->alphaSourceBlend
	];

	/* Bake the state! */
	ID3D11Device_CreateBlendState(
		renderer->device,
		&desc,
		&result
	);
	hmput(renderer->blendStateCache, hash, result);

	/* Return the state! */
	return result;
}

static ID3D11DepthStencilState* FetchDepthStencilState(
	D3D11Renderer *renderer,
	FNA3D_DepthStencilState *state
) {
	StateHash hash;
	D3D11_DEPTH_STENCIL_DESC desc;
	D3D11_DEPTH_STENCILOP_DESC front, back;
	ID3D11DepthStencilState *result;

	/* Can we just reuse an existing state? */
	hash = GetDepthStencilStateHash(*state);
	result = hmget(renderer->depthStencilStateCache, hash);
	if (result != NULL)
	{
		/* The state is already cached! */
		return result;
	}

	/* We have to make a new depth stencil state... */
	desc.DepthEnable = state->depthBufferEnable;
	desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	desc.DepthFunc = XNAToD3D_CompareFunc[
		state->depthBufferFunction
	];
	desc.StencilEnable = state->stencilEnable;
	desc.StencilReadMask = (uint8_t) state->stencilMask;
	desc.StencilWriteMask = (uint8_t) state->stencilWriteMask;
	front.StencilDepthFailOp = XNAToD3D_StencilOp[
		state->stencilDepthBufferFail
	];
	front.StencilFailOp = XNAToD3D_StencilOp[
		state->stencilFail
	];
	front.StencilFunc = XNAToD3D_CompareFunc[
		state->stencilFunction
	];
	front.StencilPassOp = XNAToD3D_StencilOp[
		state->stencilPass
	];
	if (state->twoSidedStencilMode)
	{
		back.StencilDepthFailOp = XNAToD3D_StencilOp[
			state->ccwStencilDepthBufferFail
		];
		back.StencilFailOp = XNAToD3D_StencilOp[
			state->ccwStencilFail
		];
		back.StencilFunc = XNAToD3D_CompareFunc[
			state->ccwStencilFunction
		];
		back.StencilPassOp = XNAToD3D_StencilOp[
			state->ccwStencilPass
		];
	}
	else
	{
		back = front;
	}
	desc.FrontFace = front;
	desc.BackFace = back;

	/* Bake the state! */
	ID3D11Device_CreateDepthStencilState(
		renderer->device,
		&desc,
		&result
	);
	hmput(renderer->depthStencilStateCache, hash, result);

	/* Return the state! */
	return result;
}

static ID3D11RasterizerState* FetchRasterizerState(
	D3D11Renderer *renderer,
	FNA3D_RasterizerState *state
) {
	StateHash hash;
	D3D11_RASTERIZER_DESC desc;
	ID3D11RasterizerState *result;

	/* Can we just reuse an existing state? */
	hash = GetRasterizerStateHash(*state);
	result = hmget(renderer->rasterizerStateCache, hash);
	if (result != NULL)
	{
		/* The state is already cached! */
		return result;
	}

	/* We have to make a new rasterizer state... */
	desc.AntialiasedLineEnable = 0;
	desc.CullMode = XNAToD3D_CullMode[state->cullMode];
	desc.DepthBias = state->depthBias * XNAToD3D_DepthBiasScale[
		renderer->currentDepthFormat
	];
	desc.DepthBiasClamp = D3D11_FLOAT32_MAX;
	desc.DepthClipEnable = 1;
	desc.FillMode = XNAToD3D_FillMode[state->fillMode];
	desc.FrontCounterClockwise = 1;
	desc.MultisampleEnable = state->multiSampleAntiAlias;
	desc.ScissorEnable = state->scissorTestEnable;
	desc.SlopeScaledDepthBias = state->slopeScaleDepthBias;

	/* Bake the state! */
	ID3D11Device_CreateRasterizerState(
		renderer->device,
		&desc,
		&result
	);
	hmput(renderer->rasterizerStateCache, hash, result);

	/* Return the state! */
	return result;
}

static ID3D11SamplerState* FetchSamplerState(
	D3D11Renderer *renderer,
	FNA3D_SamplerState *state
) {
	StateHash hash;
	D3D11_SAMPLER_DESC desc;
	ID3D11SamplerState *result;

	/* Can we just reuse an existing state? */
	hash = GetSamplerStateHash(*state);
	result = hmget(renderer->samplerStateCache, hash);
	if (result != NULL)
	{
		/* The state is already cached! */
		return result;
	}

	/* We have to make a new sampler state... */
	desc.AddressU = XNAToD3D_Wrap[state->addressU];
	desc.AddressV = XNAToD3D_Wrap[state->addressV];
	desc.AddressW = XNAToD3D_Wrap[state->addressW];
	desc.BorderColor[0] = 1.0f;
	desc.BorderColor[1] = 1.0f;
	desc.BorderColor[2] = 1.0f;
	desc.BorderColor[3] = 1.0f;
	desc.ComparisonFunc = D3D11_COMPARISON_NEVER; /* FIXME: What should this be? */
	desc.Filter = XNAToD3D_Filter[state->filter];
	desc.MaxAnisotropy = (uint32_t) state->maxAnisotropy;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	desc.MinLOD = (float) state->maxMipLevel;
	desc.MipLODBias = state->mipMapLevelOfDetailBias;

	/* Bake the state! */
	ID3D11Device_CreateSamplerState(
		renderer->device,
		&desc,
		&result
	);
	hmput(renderer->samplerStateCache, hash, result);

	/* Return the state! */
	return result;
}

/* Renderer Implementation */

/* Quit */

static void D3D11_DestroyDevice(FNA3D_Device *device)
{
	D3D11Renderer* renderer = (D3D11Renderer*) device->driverData;
	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void D3D11_BeginFrame(FNA3D_Renderer *driverData)
{
	/* TODO */
}

static void D3D11_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	/* TODO */
}

static void D3D11_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	if (	presentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
		presentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		renderer->syncInterval = 1;
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_TWO)
	{
		renderer->syncInterval = 2;
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_IMMEDIATE)
	{
		renderer->syncInterval = 0;
	}
	else
	{
		FNA3D_LogError(
			"Unrecognized PresentInterval: %d",
			presentInterval
		);
	}
}

/* Drawing */

static void D3D11_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	int32_t i;
	uint32_t dsClearFlags;
	float clearColor[4] = {color->x, color->y, color->z, color->w};

	/* Clear color? */
	if (options & FNA3D_CLEAROPTIONS_TARGET)
	{
		for (i = 0; i < renderer->numRenderTargets; i += 1)
		{
			/* Clear! */
			ID3D11DeviceContext_ClearRenderTargetView(
				renderer->context,
				renderer->renderTargetViews[i],
				clearColor
			);
		}
	}

	/* Clear depth/stencil? */
	dsClearFlags = 0;
	if (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER)
	{
		dsClearFlags |= D3D11_CLEAR_DEPTH;
	}
	if (options & FNA3D_CLEAROPTIONS_STENCIL)
	{
		dsClearFlags |= D3D11_CLEAR_STENCIL;
	}
	if (dsClearFlags != 0 && renderer->depthStencilView != NULL)
	{
		/* Clear! */
		ID3D11DeviceContext_ClearDepthStencilView(
			renderer->context,
			renderer->depthStencilView,
			dsClearFlags,
			depth,
			(uint8_t) stencil
		);
	}
}

static void D3D11_DrawIndexedPrimitives(
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
	/* FIXME: Needs testing! */
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;

	/* Bind index buffer */
	ID3D11DeviceContext_IASetIndexBuffer(
		renderer->context,
		((D3D11Buffer*) indices)->handle,
		XNAToD3D_IndexType[indexElementSize],
		startIndex * IndexSize(indexElementSize)
	);

	/* Set up draw state */
	ID3D11DeviceContext_IASetPrimitiveTopology(
		renderer->context,
		XNAToD3D_Primitive[primitiveType]
	);

	/* Draw! */
	ID3D11DeviceContext_DrawIndexed(
		renderer->context,
		PrimitiveVerts(primitiveType, primitiveCount),
		(uint32_t) startIndex, /* FIXME: Is this right? */
		baseVertex
	);
}

static void D3D11_DrawInstancedPrimitives(
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
	/* FIXME: Needs testing! */
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;

	/* Bind index buffer */
	ID3D11DeviceContext_IASetIndexBuffer(
		renderer->context,
		((D3D11Buffer*) indices)->handle,
		XNAToD3D_IndexType[indexElementSize],
		startIndex * IndexSize(indexElementSize)
	);

	/* Set up draw state */
	ID3D11DeviceContext_IASetPrimitiveTopology(
		renderer->context,
		XNAToD3D_Primitive[primitiveType]
	);

	/* Draw! */
	ID3D11DeviceContext_DrawIndexedInstanced(
		renderer->context,
		PrimitiveVerts(primitiveType, primitiveCount),
		instanceCount,
		(uint32_t) startIndex, /* FIXME: Is this right? */
		baseVertex,
		0
	);
}

static void D3D11_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	/* FIXME: Needs testing! */
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;

	/* Bind draw state */
	ID3D11DeviceContext_IASetPrimitiveTopology(
		renderer->context,
		XNAToD3D_Primitive[primitiveType]
	);

	/* Draw! */
	ID3D11DeviceContext_Draw(
		renderer->context,
		(uint32_t) PrimitiveVerts(primitiveType, primitiveCount),
		(uint32_t) vertexStart
	);
}

static void D3D11_DrawUserIndexedPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t numVertices,
	void* indexData,
	int32_t indexOffset,
	FNA3D_IndexElementSize indexElementSize,
	int32_t primitiveCount
) {
	/* TODO */
}

static void D3D11_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	/* TODO */
}

/* Mutable Render States */

static void D3D11_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11_VIEWPORT vp =
	{
		(float) viewport->x,
		(float) viewport->y,
		(float) viewport->w,
		(float) viewport->h,
		viewport->minDepth,
		viewport->maxDepth
	};
	ID3D11DeviceContext_RSSetViewports(
		renderer->context,
		1,
		&vp
	);
}

static void D3D11_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11_RECT rect =
	{
		scissor->x,
		scissor->x + scissor->w,
		scissor->y,
		scissor->y + scissor->h
	};
	ID3D11DeviceContext_RSSetScissorRects(
		renderer->context,
		1,
		&rect
	);
}

static void D3D11_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	SDL_memcpy(blendFactor, &renderer->blendFactor, sizeof(FNA3D_Color));
}

static void D3D11_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	SDL_memcpy(&renderer->blendFactor, blendFactor, sizeof(FNA3D_Color));
}

static int32_t D3D11_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	return renderer->multiSampleMask;
}

static void D3D11_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	renderer->multiSampleMask = mask;
}

static int32_t D3D11_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	return renderer->stencilRef;
}

static void D3D11_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	renderer->stencilRef = ref;
}

/* Immutable Render States */

static void D3D11_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	float blendFactor[] =
	{
		renderer->blendFactor.r / 255.0f,
		renderer->blendFactor.g / 255.0f,
		renderer->blendFactor.b / 255.0f,
		renderer->blendFactor.a / 255.0f
	};
	ID3D11DeviceContext_OMSetBlendState(
		renderer->context,
		FetchBlendState(renderer, blendState),
		blendFactor,
		(uint32_t) renderer->multiSampleMask
	);
}

static void D3D11_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	ID3D11DeviceContext_OMSetDepthStencilState(
		renderer->context,
		FetchDepthStencilState(renderer, depthStencilState),
		(uint32_t) renderer->stencilRef
	);
}

static void D3D11_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	ID3D11DeviceContext_RSSetState(
		renderer->context,
		FetchRasterizerState(renderer, rasterizerState)
	);
}

static void D3D11_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;

	/* TODO */
	/* We need to bind all samplers at once !*/
}

/* Vertex State */

static void D3D11_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	/* TODO */
}

static void D3D11_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* vertexData,
	int32_t vertexOffset
) {
	/* TODO */
}

/* Render Targets */

static void D3D11_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat
) {
	/* TODO */
}

static void D3D11_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	/* TODO */
}

/* Backbuffer Functions */

static void D3D11_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
}

static void D3D11_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

static void D3D11_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	/* TODO */
}

static FNA3D_SurfaceFormat D3D11_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	/* TODO */
	return FNA3D_SURFACEFORMAT_COLOR;
}

static FNA3D_DepthFormat D3D11_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	/* TODO */
	return FNA3D_DEPTHFORMAT_NONE;
}

static int32_t D3D11_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	/* TODO */
	return 0;
}

/* Textures */

static FNA3D_Texture* D3D11_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *result = (D3D11Texture*) SDL_malloc(sizeof(D3D11Texture));
	DXGI_SAMPLE_DESC sampleDesc = {1, 0};
	D3D11_TEXTURE2D_DESC desc;

	/* Initialize descriptor */
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = levelCount;
	desc.ArraySize = 1;
	desc.Format = XNAToD3D_TextureFormat[format];
	desc.SampleDesc = sampleDesc;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	if (isRenderTarget)
	{
		/* FIXME: Apparently it's faster to specify
		 * a single bind flag. What can we do here?
		 */
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
	}

	/* Create the texture */
	ID3D11Device_CreateTexture2D(
		renderer->device,
		&desc,
		NULL,
		&result->handle.h2D
	);
	result->levelCount = levelCount;
	result->isRenderTarget = isRenderTarget;
	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* D3D11_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *result = (D3D11Texture*) SDL_malloc(sizeof(D3D11Texture));
	D3D11_TEXTURE3D_DESC desc;

	/* Initialize descriptor */
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	desc.MipLevels = levelCount;
	desc.Format = XNAToD3D_TextureFormat[format];
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	/* Create the texture */
	ID3D11Device_CreateTexture3D(
		renderer->device,
		&desc,
		NULL,
		&result->handle.h3D
	);
	result->levelCount = levelCount;
	result->isRenderTarget = 0;
	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* D3D11_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *result = (D3D11Texture*) SDL_malloc(sizeof(D3D11Texture));
	DXGI_SAMPLE_DESC sampleDesc = {1, 0};
	D3D11_TEXTURE2D_DESC desc;
	desc.Width = size;
	desc.Height = size;
	desc.MipLevels = levelCount;
	desc.ArraySize = 6;
	desc.Format = XNAToD3D_TextureFormat[format];
	desc.SampleDesc = sampleDesc;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	if (isRenderTarget)
	{
		/* FIXME: Apparently it's faster to specify
		 * a single bind flag. What can we do here?
		 */
		desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
	}

	/* Create the texture */
	ID3D11Device_CreateTexture2D(
		renderer->device,
		&desc,
		NULL,
		&result->handle.h2D
	);
	result->levelCount = levelCount;
	result->isRenderTarget = isRenderTarget;
	return (FNA3D_Texture*) result;
}

static void D3D11_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	/* TODO */
}

/* FIXME: Supposedly this is already included
 * in d3d11.h, but I'm not seeing it. -caleb
 */
static inline uint32_t CalcSubresource(
	uint32_t mipLevel,
	uint32_t arraySlice,
	uint32_t numLevels
) {
	return mipLevel + (arraySlice * numLevels);
}

static void D3D11_SetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *d3dTexture = (D3D11Texture*) texture;
	D3D11_BOX dstBox = {x, y, 0, x + w, y + h, 1};

	ID3D11DeviceContext_UpdateSubresource(
		renderer->context,
		(ID3D11Resource*) d3dTexture->handle.h2D,
		CalcSubresource(level, 0, d3dTexture->levelCount),
		&dstBox,
		data,
		BytesPerRow(w, format),
		BytesPerDepthSlice(w, h, format)
	);
}

static void D3D11_SetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
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
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *d3dTexture = (D3D11Texture*) texture;
	D3D11_BOX dstBox = {x, y, z, x + w, y + h, z + d};

	ID3D11DeviceContext_UpdateSubresource(
		renderer->context,
		(ID3D11Resource*) d3dTexture->handle.h3D,
		CalcSubresource(level, 0, d3dTexture->levelCount),
		&dstBox,
		data,
		BytesPerRow(w, format),
		BytesPerDepthSlice(w, h, format)
	);
}

static void D3D11_SetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	D3D11Texture *d3dTexture = (D3D11Texture*) texture;
	D3D11_BOX dstBox = {x, y, 0, x + w, y + h, 1};

	ID3D11DeviceContext_UpdateSubresource(
		renderer->context,
		(ID3D11Resource*) d3dTexture->handle.h2D,
		CalcSubresource(
			level,
			cubeMapFace,
			d3dTexture->levelCount
		),
		&dstBox,
		data,
		BytesPerRow(w, format),
		BytesPerDepthSlice(w, h, format)
	);
}

static void D3D11_SetTextureDataYUV(
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
	/* TODO */
}

static void D3D11_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

static void D3D11_GetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
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
	/* TODO */
}

static void D3D11_GetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

/* Renderbuffers */

static FNA3D_Renderbuffer* D3D11_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	/* TODO */
	return NULL;
}

static FNA3D_Renderbuffer* D3D11_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	/* TODO */
	return NULL;
}

static void D3D11_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	/* TODO */
}

/* Vertex Buffers */

static FNA3D_Buffer* D3D11_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	/* TODO */
	return NULL;
}

static void D3D11_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

static void D3D11_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

static void D3D11_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	/* TODO */
}

/* Index Buffers */

static FNA3D_Buffer* D3D11_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	/* TODO */
	return NULL;
}

static void D3D11_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

static void D3D11_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

static void D3D11_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	/* TODO */
}

/* Effects */

static void D3D11_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	/* TODO */
	*effect = NULL;
	*effectData = NULL;
}

static void D3D11_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	/* TODO */
	*effect = NULL;
	*effectData = NULL;
}

static void D3D11_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

static void D3D11_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	/* TODO */
}

static void D3D11_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

static void D3D11_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

static void D3D11_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

/* Queries */

static FNA3D_Query* D3D11_CreateQuery(FNA3D_Renderer *driverData)
{
	/* TODO */
	return NULL;
}

static void D3D11_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	/* TODO */
}

static void D3D11_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

static void D3D11_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* TODO */
}

static uint8_t D3D11_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	/* TODO */
	return 1;
}

static int32_t D3D11_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	/* TODO */
	return 0;
}

/* Feature Queries */

static uint8_t D3D11_SupportsDXT1(FNA3D_Renderer *driverData)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	return renderer->supportsDxt1;
}

static uint8_t D3D11_SupportsS3TC(FNA3D_Renderer *driverData)
{
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	return renderer->supportsS3tc;
}

static uint8_t D3D11_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t D3D11_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

static int32_t D3D11_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	return D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
}

static int32_t D3D11_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	/* 8x MSAA is guaranteed for all
	 * surface formats except Vector4.
	 * FIXME: Can we check if the actual limit is higher?
	 */
	return 8;
}

/* Debugging */

static void D3D11_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	/* TODO: Something like this? */
/*
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	wchar_t wString[1024];

	MultiByteToWideChar(CP_ACP, 0, text, -1, &wString[0], 1024);
	renderer->debugAnnotation->lpVtbl->SetMarker(
		renderer->debugAnnotation,
		wString
	);
*/
}

/* Driver */

static uint8_t D3D11_PrepareWindowAttributes(uint32_t *flags)
{
	const char *osVersion = SDL_GetPlatform();
	if (	(strcmp(osVersion, "Windows") != 0) &&
		(strcmp(osVersion, "WinRT") != 0)	)
	{
		/* Windows / Xbox is required for DirectX! */
		return 0;
	}

	/* FIXME: Check for DirectX 11 support! */

	/* No window flags required */
	SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
	return 1;
}

static void D3D11_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	SDL_SysWMinfo info;
	RECT clientRect;

	SDL_GetWindowWMInfo((SDL_Window*) window, &info);
	SDL_VERSION(&info.version);

	GetClientRect(info.info.win.window, &clientRect);
	*x = (clientRect.right - clientRect.left);
	*y = (clientRect.bottom - clientRect.top);
}

/* DirectX SDK doesn't have this feature level */
#ifndef D3D_FEATURE_LEVEL_11_1
#define D3D_FEATURE_LEVEL_11_1 (D3D_FEATURE_LEVEL) 0xb100
#endif

static FNA3D_Device* D3D11_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	FNA3D_Device *result;
	D3D11Renderer *renderer;
	void* module;
	typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(const GUID *riid, void **ppFactory);
	PFN_D3D11_CREATE_DEVICE D3D11CreateDeviceFunc;
	PFN_CREATE_DXGI_FACTORY CreateDXGIFactoryFunc;
	D3D_FEATURE_LEVEL levels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	uint32_t flags, supportsDxt3, supportsDxt5;
	HRESULT ret;

	/* Allocate and zero out the renderer */
	renderer = (D3D11Renderer*) SDL_malloc(
		sizeof(D3D11Renderer)
	);
	SDL_memset(renderer, '\0', sizeof(renderer));

	/* Load function pointers */
	/* FIXME: On WinRT, use CreateDXGIFactory1
	 * and D3D11CreateDevice directly.
	 */
	module = SDL_LoadObject("dxgi.dll");
	SDL_assert(module != NULL);
	CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY) SDL_LoadFunction(
		module,
		"CreateDXGIFactory"
	);
	SDL_assert(CreateDXGIFactoryFunc != NULL);

	module = SDL_LoadObject("d3d11.dll");
	SDL_assert(module != NULL);
	D3D11CreateDeviceFunc = (PFN_D3D11_CREATE_DEVICE) SDL_LoadFunction(
		module,
		"D3D11CreateDevice"
	);
	SDL_assert(D3D11CreateDeviceFunc != NULL);

	/* Create the DXGIFactory */
	ret = CreateDXGIFactoryFunc(
		&D3D_IID_IDXGIFactory2,
		&renderer->factory
	);
	if (ret < 0)
	{
		FNA3D_LogError(
			"Could not create DXGIFactory! Error code: %x",
			ret
		);
		return NULL;
	}

	/* Create the D3D11Device */
	flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	if (debugMode)
	{
		flags |= D3D11_CREATE_DEVICE_DEBUG;
	}
	ret = D3D11CreateDeviceFunc(
		NULL, /* FIXME: Do we need to use a non-default adapter? */
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		flags,
		levels,
		SDL_arraysize(levels),
		D3D11_SDK_VERSION,
		&renderer->device,
		&renderer->featureLevel,
		&renderer->context
	);
	if (ret < 0)
	{
		FNA3D_LogError(
			"Could not create D3D11Device! Error code: %x",
			ret
		);
		return NULL;
	}

	/* Determine DXT/S3TC support */
	ID3D11Device_CheckFormatSupport(
		renderer->device,
		XNAToD3D_TextureFormat[FNA3D_SURFACEFORMAT_DXT1],
		&renderer->supportsDxt1
	);
	ID3D11Device_CheckFormatSupport(
		renderer->device,
		XNAToD3D_TextureFormat[FNA3D_SURFACEFORMAT_DXT3],
		&supportsDxt3
	);
	ID3D11Device_CheckFormatSupport(
		renderer->device,
		XNAToD3D_TextureFormat[FNA3D_SURFACEFORMAT_DXT5],
		&supportsDxt5
	);
	renderer->supportsS3tc = (supportsDxt3 || supportsDxt5);

	/* Initialize state object caches */
	hmdefault(renderer->blendStateCache, NULL);
	hmdefault(renderer->depthStencilStateCache, NULL);
	hmdefault(renderer->rasterizerStateCache, NULL);
	hmdefault(renderer->samplerStateCache, NULL);

	/* Create and return the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	result->driverData = (FNA3D_Renderer*) renderer;
	ASSIGN_DRIVER(D3D11)
	return result;

	/* TODO */
}

FNA3D_Driver D3D11Driver = {
	"D3D11",
	D3D11_PrepareWindowAttributes,
	D3D11_GetDrawableSize,
	D3D11_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_D3D11 */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
