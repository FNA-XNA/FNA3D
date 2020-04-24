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

typedef struct D3D11Backbuffer
{
	int32_t width;
	int32_t height;
	FNA3D_SurfaceFormat surfaceFormat;
	FNA3D_DepthFormat depthFormat;
	int32_t multiSampleCount;

	ID3D11Texture2D *colorBuffer;
	ID3D11RenderTargetView *colorView;

	ID3D11Texture2D *msaaColorBuffer;
	ID3D11RenderTargetView *msaaColorView;

	ID3D11Texture2D *depthStencilBuffer;
	ID3D11DepthStencilView *depthStencilView;
} D3D11Backbuffer;

typedef struct D3D11Renderer /* Cast FNA3D_Renderer* to this! */
{
	/* Persistent D3D11 Objects */
	ID3D11Device *device;
	ID3D11DeviceContext *context;
	IDXGIFactory1 *factory;
	IDXGISwapChain *swapchain;

	/* The Faux-Backbuffer */
	D3D11Backbuffer *backbuffer;
	uint8_t backbufferSizeChanged;
	ID3D11VertexShader *fauxBlitVS;
	ID3D11PixelShader *fauxBlitPS;
	ID3D11SamplerState *fauxBlitSampler;
	ID3D11ShaderResourceView *fauxBlitShaderResourceView;
	ID3D11Buffer *fauxBlitVertexBuffer;
	ID3D11Buffer *fauxBlitIndexBuffer;
	ID3D11InputLayout *fauxBlitLayout;
	ID3D11RasterizerState *fauxRasterizer;
	ID3D11BlendState *fauxBlendState;

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
	ID3D11RenderTargetView *swapchainRTView;
	ID3D11RenderTargetView *renderTargetViews[MAX_RENDERTARGET_BINDINGS];
	ID3D11DepthStencilView *depthStencilView;
	FNA3D_DepthFormat currentDepthFormat;
} D3D11Renderer;

/* VS2010 / DirectX SDK Fallback Defines */

#ifndef DXGI_FORMAT_B4G4R4A4_UNORM
#define DXGI_FORMAT_B4G4R4A4_UNORM (DXGI_FORMAT) 115
#endif

#ifndef D3D_FEATURE_LEVEL_11_1
#define D3D_FEATURE_LEVEL_11_1 (D3D_FEATURE_LEVEL) 0xb100
#endif

/* XNA->D3D11 Translation Arrays */

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

/* IID Imports from https://www.magnumdb.com/ */

static const IID D3D_IID_IDXGIFactory1 = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const IID D3D_IID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c } };

/* Function Pointers */

typedef HRESULT(WINAPI *PFN_D3DCOMPILE)(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO *pDefines,
    ID3DInclude *pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob **ppCode,
    ID3DBlob **ppErrorMsgs
);
static PFN_D3DCOMPILE D3DCompileFunc;

/* Faux-Backbuffer Blit Shader Sources */

static const char* FAUX_BLIT_VERTEX_SHADER =
	"void main(inout float4 pos : SV_POSITION, inout float2 texCoord : TEXCOORD0) \n"
	"{ pos.y *= -1; pos.zw = float2(0.0f, 1.0f); }";

const char* FAUX_BLIT_PIXEL_SHADER =
	"Texture2D Texture : register(t0); \n"
	"sampler TextureSampler : register(s0); \n"
	"float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET \n"
	"{ return Texture.Sample(TextureSampler, texcoord); }";

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
	D3D11Renderer *renderer = (D3D11Renderer*) driverData;
	const uint32_t vertexStride = 16;
	const uint32_t offsets[] = { 0 };
	float blendFactor[] = { 1.0f, 1.0f, 1.0f, 1.0f };

	/* Bind the backbuffer render target */
	ID3D11DeviceContext_OMSetRenderTargets(
		renderer->context,
		1,
		&renderer->swapchainRTView,
		NULL
	);

	/* Bind the vertex and index buffers */
	ID3D11DeviceContext_IASetVertexBuffers(
		renderer->context,
		0,
		1,
		&renderer->fauxBlitVertexBuffer,
		&vertexStride,
		offsets
	);
	ID3D11DeviceContext_IASetIndexBuffer(
		renderer->context,
		renderer->fauxBlitIndexBuffer,
		DXGI_FORMAT_R16_UINT,
		0
	);

	/* Set the rest of the pipeline state */
	ID3D11DeviceContext_OMSetBlendState(
		renderer->context,
		renderer->fauxBlendState,
		blendFactor,
		0xffffffff
	);
	ID3D11DeviceContext_RSSetState(
		renderer->context,
		renderer->fauxRasterizer
	);
	ID3D11DeviceContext_IASetInputLayout(
		renderer->context,
		renderer->fauxBlitLayout
	);
	ID3D11DeviceContext_VSSetShader(
		renderer->context,
		renderer->fauxBlitVS,
		NULL,
		0
	);
	ID3D11DeviceContext_PSSetShader(
		renderer->context,
		renderer->fauxBlitPS,
		NULL,
		0
	);
	ID3D11DeviceContext_PSSetShaderResources(
		renderer->context,
		0,
		1,
		&renderer->fauxBlitShaderResourceView
	);
	ID3D11DeviceContext_PSSetSamplers(
		renderer->context,
		0,
		1,
		&renderer->fauxBlitSampler
	);
	ID3D11DeviceContext_IASetPrimitiveTopology(
		renderer->context,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	/* Draw! */
	ID3D11DeviceContext_DrawIndexed(renderer->context, 6, 0, 0);

	/* Present! */
	IDXGISwapChain_Present(renderer->swapchain, renderer->syncInterval, 0);
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

static HWND GetHWND(SDL_Window *window)
{
	SDL_SysWMinfo info;
	SDL_GetWindowWMInfo((SDL_Window*) window, &info);
	SDL_VERSION(&info.version);
	return info.info.win.window;
}

static void CreateFramebuffer(
	D3D11Renderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	int32_t newWidth, newHeight;
	D3D11_TEXTURE2D_DESC colorBufferDesc;
	D3D11_RENDER_TARGET_VIEW_DESC colorViewDesc;
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderViewDesc;
	D3D11_TEXTURE2D_DESC depthStencilDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	DXGI_RATIONAL refreshRate = { 1, 60 }; /* FIXME: Get this from display mode. */
	DXGI_MODE_DESC swapchainBufferDesc;
	DXGI_SAMPLE_DESC swapchainSampleDesc = { 1, 0 }; /* FIXME: What should this be? */
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	D3D11_RENDER_TARGET_VIEW_DESC swapchainViewDesc;
	ID3D11Texture2D *swapchainTexture;

	#define BB renderer->backbuffer

	/* Update the backbuffer size */
	newWidth = presentationParameters->backBufferWidth;
	newHeight = presentationParameters->backBufferHeight;
	if (BB->width != newWidth || BB->height != newHeight)
	{
		renderer->backbufferSizeChanged = 1;
	}
	BB->width = newWidth;
	BB->height = newHeight;

	/* Update other presentation parameters */
	BB->surfaceFormat = presentationParameters->backBufferFormat;
	BB->depthFormat = presentationParameters->depthStencilFormat;
	BB->multiSampleCount = presentationParameters->multiSampleCount;

	/* Update color buffer to the new resolution */
	colorBufferDesc.Width = BB->width;
	colorBufferDesc.Height = BB->height;
	colorBufferDesc.MipLevels = 1;
	colorBufferDesc.ArraySize = 1;
	colorBufferDesc.Format = XNAToD3D_TextureFormat[BB->surfaceFormat];
	colorBufferDesc.SampleDesc.Count = 1;
	colorBufferDesc.SampleDesc.Quality = 0; /* FIXME: This should probably be different... */
	colorBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	colorBufferDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	colorBufferDesc.CPUAccessFlags = 0; /* GPU-private */
	colorBufferDesc.MiscFlags = 0;
	ID3D11Device_CreateTexture2D(
		renderer->device,
		&colorBufferDesc,
		NULL,
		&BB->colorBuffer
	);

	/* Update color buffer view */
	colorViewDesc.Format = colorBufferDesc.Format;
	colorViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	colorViewDesc.Texture2D.MipSlice = 0;
	ID3D11Device_CreateRenderTargetView(
		renderer->device,
		(ID3D11Resource*) BB->colorBuffer,
		&colorViewDesc,
		&BB->colorView
	);

	/* Update shader resource view */
	shaderViewDesc.Format = colorBufferDesc.Format;
	shaderViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderViewDesc.Texture2D.MipLevels = 1;
	shaderViewDesc.Texture2D.MostDetailedMip = 0;
	ID3D11Device_CreateShaderResourceView(
		renderer->device,
		(ID3D11Resource*) BB->colorBuffer,
		&shaderViewDesc,
		&renderer->fauxBlitShaderResourceView
	);

	/* Update the multisample color buffer, if applicable */
	if (BB->multiSampleCount > 0)
	{
		/* FIXME: No idea if this works. */

		colorBufferDesc.SampleDesc.Count = BB->multiSampleCount;
		ID3D11Device_CreateTexture2D(
			renderer->device,
			&colorBufferDesc,
			NULL,
			&BB->msaaColorBuffer
		);

		/* Update the MSAA view */
		ID3D11Device_CreateRenderTargetView(
			renderer->device,
			(ID3D11Resource*) BB->msaaColorBuffer,
			&colorViewDesc,
			&BB->msaaColorView
		);
	}

	/* Update the depth/stencil buffer, if applicable */
	if (BB->depthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		depthStencilDesc.Width = BB->width;
		depthStencilDesc.Height = BB->height;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.ArraySize = 1;
		depthStencilDesc.Format = XNAToD3D_DepthFormat[BB->depthFormat];
		depthStencilDesc.SampleDesc.Count = BB->multiSampleCount;
		depthStencilDesc.SampleDesc.Quality = 0; /* FIXME: This should probably be different... */
		depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthStencilDesc.CPUAccessFlags = 0; /* GPU-private */
		depthStencilDesc.MiscFlags = 0;
		ID3D11Device_CreateTexture2D(
			renderer->device,
			&depthStencilDesc,
			NULL,
			&BB->depthStencilBuffer
		);

		/* Update the depth-stencil view */
		depthStencilViewDesc.Format = depthStencilDesc.Format;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Flags = 0; /* read/write capable */
		depthStencilViewDesc.Texture2D.MipSlice = 0;
		ID3D11Device_CreateDepthStencilView(
			renderer->device,
			(ID3D11Resource*) BB->depthStencilBuffer,
			&depthStencilViewDesc,
			&BB->depthStencilView
		);
	}

	/* Do we need to create the swapchain? */
	if (renderer->swapchain == NULL)
	{
		/* Initialize swapchain buffer descriptor */
		swapchainBufferDesc.Width = BB->width;
		swapchainBufferDesc.Height = BB->height;
		swapchainBufferDesc.RefreshRate = refreshRate;
		swapchainBufferDesc.Format = colorBufferDesc.Format;
		swapchainBufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapchainBufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		/* Create the swapchain! */
		swapchainDesc.BufferDesc = swapchainBufferDesc;
		swapchainDesc.SampleDesc = swapchainSampleDesc;
		swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchainDesc.BufferCount = 3;
		swapchainDesc.OutputWindow = GetHWND(
			(SDL_Window*) presentationParameters->deviceWindowHandle
		);
		swapchainDesc.Windowed = 1;
		swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; /* FIXME: What do we want? */
		swapchainDesc.Flags = 0; /* FIXME: ??? */
		IDXGIFactory1_CreateSwapChain(
			renderer->factory,
			(IUnknown*) renderer->device,
			&swapchainDesc,
			&renderer->swapchain
		);
	}
	else
	{
		/* Resize the swapchain to the new window size */
		IDXGISwapChain_ResizeBuffers(
			renderer->swapchain,
			0,			/* keep # of buffers the same */
			0,			/* get width from window */
			0,			/* get height from window */
			DXGI_FORMAT_UNKNOWN,	/* keep the old format */
			0
		);
	}

	/* Create a render target view for the swapchain */
	swapchainViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	swapchainViewDesc.Texture2D.MipSlice = 0;
	IDXGISwapChain_GetBuffer(
		renderer->swapchain,
		0,
		&D3D_IID_ID3D11Texture2D,
		(void**) &swapchainTexture
	);
	ID3D11Device_CreateRenderTargetView(
		renderer->device,
		(ID3D11Resource*) swapchainTexture,
		&swapchainViewDesc,
		&renderer->swapchainRTView
	);
	ID3D11Texture2D_Release(swapchainTexture); /* Cleanup is required for any GetBuffer call! */
	swapchainTexture = NULL;

	/* This is the default render target */
	D3D11_SetRenderTargets(
		(FNA3D_Renderer*) renderer,
		NULL,
		0,
		NULL,
		FNA3D_DEPTHFORMAT_NONE
	);
}

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
	/* FIXME: This doesn't work at all and just returns 0, 0! */
	RECT clientRect;
	GetClientRect(GetHWND((SDL_Window*) window), &clientRect);
	*x = (clientRect.right - clientRect.left);
	*y = (clientRect.bottom - clientRect.top);
}

static void InitializeFauxBackbuffer(
	D3D11Renderer *renderer,
	uint8_t scaleNearest
) {
	ID3DBlob *blob;
	D3D11_INPUT_ELEMENT_DESC ePosition;
	D3D11_INPUT_ELEMENT_DESC eTexcoord;
	D3D11_INPUT_ELEMENT_DESC elements[2];
	D3D11_SAMPLER_DESC samplerDesc;
	D3D11_BUFFER_DESC vbufDesc;
	uint16_t indices[] =
	{
		0, 1, 3,
		1, 2, 3
	};
	D3D11_SUBRESOURCE_DATA indicesData;
	D3D11_BUFFER_DESC ibufDesc;
	D3D11_RASTERIZER_DESC rastDesc;
	D3D11_BLEND_DESC blendDesc;
	HRESULT res;

	/* Create and compile the vertex shader */
	res = D3DCompileFunc(
		FAUX_BLIT_VERTEX_SHADER, SDL_strlen(FAUX_BLIT_VERTEX_SHADER),
		"Faux-Backbuffer Blit Vertex Shader", NULL, NULL,
		"main", "vs_4_0", 0, 0, &blob, &blob
	);
	ID3D11Device_CreateVertexShader(
		renderer->device,
		ID3D10Blob_GetBufferPointer(blob),
		ID3D10Blob_GetBufferSize(blob),
		NULL,
		&renderer->fauxBlitVS
	);

	/* Create the vertex shader input layout */
	ePosition.SemanticName = "SV_POSITION";
	ePosition.SemanticIndex = 0;
	ePosition.Format = DXGI_FORMAT_R32G32_FLOAT;
	ePosition.InputSlot = 0;
	ePosition.AlignedByteOffset = 0;
	ePosition.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	ePosition.InstanceDataStepRate = 0;

	eTexcoord.SemanticName = "TEXCOORD";
	eTexcoord.SemanticIndex = 0;
	eTexcoord.Format = DXGI_FORMAT_R32G32_FLOAT;
	eTexcoord.InputSlot = 0;
	eTexcoord.AlignedByteOffset = sizeof(float) * 2;
	eTexcoord.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	eTexcoord.InstanceDataStepRate = 0;

	elements[0] = ePosition;
	elements[1] = eTexcoord;
	ID3D11Device_CreateInputLayout(
		renderer->device,
		elements,
		2,
		ID3D10Blob_GetBufferPointer(blob),
		ID3D10Blob_GetBufferSize(blob),
		&renderer->fauxBlitLayout
	);

	/* Create and compile the pixel shader */
	D3DCompileFunc(
		FAUX_BLIT_PIXEL_SHADER, SDL_strlen(FAUX_BLIT_PIXEL_SHADER),
		"Faux-Backbuffer Blit Pixel Shader", NULL, NULL,
		"main", "ps_4_0", 0, 0, &blob, &blob
	);
	ID3D11Device_CreatePixelShader(
		renderer->device,
		ID3D10Blob_GetBufferPointer(blob),
		ID3D10Blob_GetBufferSize(blob),
		NULL,
		&renderer->fauxBlitPS
	);

	/* Create the faux backbuffer sampler state */
	samplerDesc.Filter = (
		scaleNearest ?
			D3D11_FILTER_MIN_MAG_MIP_POINT :
			D3D11_FILTER_MIN_MAG_MIP_LINEAR
	);
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = 0;
	ID3D11Device_CreateSamplerState(
		renderer->device,
		&samplerDesc,
		&renderer->fauxBlitSampler
	);

	/* Create the faux backbuffer vertex buffer */
	vbufDesc.ByteWidth = 16 * sizeof(float);
	vbufDesc.Usage = D3D11_USAGE_DEFAULT;
	vbufDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbufDesc.MiscFlags = 0;
	vbufDesc.StructureByteStride = 0;
	ID3D11Device_CreateBuffer(
		renderer->device,
		&vbufDesc,
		NULL,
		&renderer->fauxBlitVertexBuffer
	);

	/* Initialize faux backbuffer index data */
	indicesData.pSysMem = &indices[0];
	indicesData.SysMemPitch = 0;
	indicesData.SysMemSlicePitch = 0;

	/* Create the faux backbuffer index buffer */
	ibufDesc.ByteWidth = 6 * sizeof(uint16_t);
	ibufDesc.Usage = D3D11_USAGE_IMMUTABLE;
	ibufDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibufDesc.CPUAccessFlags = 0;
	ibufDesc.MiscFlags = 0;
	ibufDesc.StructureByteStride = 0;
	ID3D11Device_CreateBuffer(
		renderer->device,
		&ibufDesc,
		&indicesData,
		&renderer->fauxBlitIndexBuffer
	);

	/* Create the faux backbuffer rasterizer state */
	rastDesc.AntialiasedLineEnable = 0;
	rastDesc.CullMode = D3D11_CULL_NONE;
	rastDesc.DepthBias = 0;
	rastDesc.DepthBiasClamp = 0;
	rastDesc.DepthClipEnable = 1;
	rastDesc.FillMode = D3D11_FILL_SOLID;
	rastDesc.FrontCounterClockwise = 0;
	rastDesc.MultisampleEnable = 0;
	rastDesc.ScissorEnable = 0;
	rastDesc.SlopeScaledDepthBias = 0;
	ID3D11Device_CreateRasterizerState(
		renderer->device,
		&rastDesc,
		&renderer->fauxRasterizer
	);

	/* Create the faux backbuffer blend state */
	SDL_memset(&blendDesc, '\0', sizeof(D3D11_BLEND_DESC));
	blendDesc.AlphaToCoverageEnable = 0;
	blendDesc.IndependentBlendEnable = 0;
	blendDesc.RenderTarget[0].BlendEnable = 0;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	ID3D11Device_CreateBlendState(
		renderer->device,
		&blendDesc,
		&renderer->fauxBlendState
	);
}

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
	module = SDL_LoadObject("dxgi.dll");
	SDL_assert(module != NULL);
	CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY) SDL_LoadFunction(
		module,
		"CreateDXGIFactory1"
	);
	SDL_assert(CreateDXGIFactoryFunc != NULL);

	module = SDL_LoadObject("d3d11.dll");
	SDL_assert(module != NULL);
	D3D11CreateDeviceFunc = (PFN_D3D11_CREATE_DEVICE) SDL_LoadFunction(
		module,
		"D3D11CreateDevice"
	);
	SDL_assert(D3D11CreateDeviceFunc != NULL);

	module = SDL_LoadObject("d3dcompiler_47.dll");
	SDL_assert(module != NULL);
	D3DCompileFunc = (PFN_D3DCOMPILE) SDL_LoadFunction(
		module,
		"D3DCompile"
	);
	SDL_assert(D3DCompileFunc != NULL);

	/* Create the DXGIFactory */
	ret = CreateDXGIFactoryFunc(
		&D3D_IID_IDXGIFactory1,
		(void**) &renderer->factory
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

	/* Create and initialize the faux-backbuffer */
	renderer->backbuffer = (D3D11Backbuffer*) SDL_malloc(
		sizeof(D3D11Backbuffer)
	);
	SDL_memset(renderer->backbuffer, '\0', sizeof(D3D11Backbuffer));
	CreateFramebuffer(renderer, presentationParameters);
	InitializeFauxBackbuffer(
		renderer,
		SDL_GetHintBoolean("FNA3D_BACKBUFFER_SCALE_NEAREST", SDL_FALSE)
	);

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
