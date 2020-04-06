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

#if FNA3D_DRIVER_METAL

#include "FNA3D_Driver_Metal.h"
#include "FNA3D_PipelineCache.h"
#include "stb_ds.h"

/* Internal Structures */

typedef struct MetalTexture MetalTexture;
typedef struct MetalRenderbuffer MetalRenderbuffer;
typedef struct MetalBuffer MetalBuffer;
typedef struct MetalEffect MetalEffect;
typedef struct MetalQuery MetalQuery;
typedef struct PipelineHashMap PipelineHashMap;

struct MetalTexture /* Cast from FNA3D_Texture* */
{
	MTLTexture *handle;
	uint8_t hasMipmaps;
	int32_t width;
	int32_t height;
	uint8_t isPrivate;
	FNA3D_SurfaceFormat format;
	FNA3D_TextureAddressMode wrapS;
	FNA3D_TextureAddressMode wrapT;
	FNA3D_TextureAddressMode wrapR;
	FNA3D_TextureFilter filter;
	float anisotropy;
	int32_t maxMipmapLevel;
	float lodBias;
	MetalTexture *next; /* linked list */
};

static MetalTexture NullTexture =
{
	NULL,
	0,
	0,
	0,
	0,
	FNA3D_SURFACEFORMAT_COLOR,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREFILTER_LINEAR,
	0.0f,
	0,
	0.0f,
	NULL
};

struct MetalBuffer /* Cast from FNA3D_Buffer* */
{
	MTLBuffer *handle;
	void* contents;
	int32_t size;
	int32_t internalOffset;
	int32_t internalBufferSize;
	int32_t prevDataLength;
	int32_t prevInternalOffset;
	FNA3D_BufferUsage usage;
	uint8_t boundThisFrame;
	MetalBuffer *next; /* linked list */
};

struct MetalRenderbuffer /* Cast from FNA3D_Renderbuffer* */
{
	MTLTexture *handle;
	MTLTexture *multiSampleHandle;
	MTLPixelFormat pixelFormat;
	int32_t multiSampleCount;
};

struct MetalEffect /* Cast from FNA3D_Effect* */
{
	MOJOSHADER_effect *effect;
	MOJOSHADER_mtlEffect *mtlEffect;
};

struct MetalQuery /* Cast from FNA3D_Query* */
{
	MTLBuffer *handle;
};

typedef struct MetalBackbuffer
{
	int32_t width;
	int32_t height;
	FNA3D_SurfaceFormat surfaceFormat;
	FNA3D_DepthFormat depthFormat;
	int32_t multiSampleCount;

	MTLTexture *colorBuffer;
	MTLTexture *multiSampleColorBuffer;
	MTLTexture *depthStencilBuffer;
} MetalBackbuffer;

typedef struct MetalRenderer /* Cast from FNA3D_Renderer* */
{
	/* Associated FNA3D_Device */
	FNA3D_Device *parentDevice;

	/* The Faux-Backbuffer */
	MetalBackbuffer *backbuffer;
	MTLSamplerMinMagFilter backbufferScaleMode;
	uint8_t backbufferSizeChanged;
	FNA3D_Rect backbufferDestBounds;
	MTLBuffer *backbufferDrawBuffer;
	MTLSamplerState *backbufferSamplerState;
	MTLRenderPipelineState *backbufferPipeline;

	/* Capabilities */
	uint8_t isMac;
	uint8_t supportsS3tc;
	uint8_t supportsDxt1;
	uint8_t supportsOcclusionQueries;
	uint8_t maxMultiSampleCount;

	/* Basic Metal Objects */
	SDL_MetalView view;
	CAMetalLayer *layer;
	MTLDevice *device;
	MTLCommandQueue *queue;

	/* Active Metal State */
	MTLCommandBuffer *commandBuffer;
	MTLRenderCommandEncoder *renderCommandEncoder;
	MTLBuffer *currentVisibilityBuffer;
	MTLVertexDescriptor *currentVertexDescriptor;
	uint8_t needNewRenderPass;
	uint8_t frameInProgress;

	/* Frame Tracking */
	/* FIXME:
	 * In theory, double- or even triple-buffering could
	 * significantly help performance by reducing CPU idle
	 * time. The trade-off is that buffer synchronization
	 * becomes much more complicated and error-prone.
	 *
	 * I've attempted a few implementations of multi-
	 * buffering, but they all had serious issues and
	 * typically performed worse than single buffering.
	 *
	 * I'm leaving these variables here in case any brave
	 * souls want to attempt a multi-buffer implementation.
	 * This could be a huge win for performance, but it'll
	 * take someone smarter than me to figure this out. ;)
	 *
	 * -caleb
	 */
	uint8_t maxFramesInFlight;
	SDL_sem *frameSemaphore;

	/* Autorelease Pool */
	NSAutoreleasePool *pool;

	/* Blend State */
	FNA3D_Color blendColor;
	int32_t multiSampleMask;
	FNA3D_BlendState blendState;
	MTLRenderPipelineState *ldPipelineState;

	/* Stencil State */
	int32_t stencilRef;

	/* Rasterizer State */
	uint8_t scissorTestEnable;
	FNA3D_CullMode cullFrontFace;
	FNA3D_FillMode fillMode;
	float depthBias;
	float slopeScaleDepthBias;
	uint8_t multiSampleEnable;

	/* Viewport State */
	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;
	int32_t currentAttachmentWidth;
	int32_t currentAttachmentHeight;

	/* Textures */
	MetalTexture *textures[MAX_TEXTURE_SAMPLERS];
	MTLSamplerState *samplers[MAX_TEXTURE_SAMPLERS];
	uint8_t textureNeedsUpdate[MAX_TEXTURE_SAMPLERS];
	uint8_t samplerNeedsUpdate[MAX_TEXTURE_SAMPLERS];
	MetalTexture *transientTextures;

	/* Depth Stencil State */
	FNA3D_DepthStencilState depthStencilState;
	MTLDepthStencilState *defaultDepthStencilState;
	MTLDepthStencilState *ldDepthStencilState;
	MTLPixelFormat D16Format;
	MTLPixelFormat D24Format;
	MTLPixelFormat D24S8Format;

	/* Buffer Binding Cache */
	MetalBuffer *buffers;
	MetalBuffer *userVertexBuffer;
	MetalBuffer *userIndexBuffer;
	int32_t userVertexStride;

	/* Some vertex declarations may have overlapping attributes :/ */
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];

	MTLBuffer *ldVertUniformBuffer;
	MTLBuffer *ldFragUniformBuffer;
	int32_t ldVertUniformOffset;
	int32_t ldFragUniformOffset;
	MTLBuffer *ldVertexBuffers[MAX_BOUND_VERTEX_BUFFERS];
	int32_t ldVertexBufferOffsets[MAX_BOUND_VERTEX_BUFFERS];

	/* Render Targets */
	MTLTexture *currentAttachments[MAX_RENDERTARGET_BINDINGS];
	MTLPixelFormat currentColorFormats[MAX_RENDERTARGET_BINDINGS];
	MTLTexture *currentMSAttachments[MAX_RENDERTARGET_BINDINGS];
	FNA3D_CubeMapFace currentAttachmentSlices[MAX_RENDERTARGET_BINDINGS];
	MTLTexture *currentDepthStencilBuffer;
	FNA3D_DepthFormat currentDepthFormat;
	int32_t currentSampleCount;

	/* Clear Cache */
	FNA3D_Vec4 clearColor;
	float clearDepth;
	int32_t clearStencil;
	uint8_t shouldClearColor;
	uint8_t shouldClearDepth;
	uint8_t shouldClearStencil;

	/* Pipeline State Object Caches */
	UInt64HashMap *vertexDescriptorCache;
	PipelineHashMap *pipelineStateCache;
	StateHashMap *depthStencilStateCache;
	StateHashMap *samplerStateCache;

	/* MojoShader Interop */
	MOJOSHADER_mtlEffect *currentEffect;
	MOJOSHADER_mtlShaderState currentShaderState;
	MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;
	MOJOSHADER_mtlEffect *prevEffect;
	MOJOSHADER_mtlShaderState prevShaderState;
} MetalRenderer;

/* XNA->Metal Translation Arrays */

static MTLPixelFormat XNAToMTL_TextureFormat[] =
{
	MTLPixelFormatRGBA8Unorm,	/* SurfaceFormat.Color */
#if defined(__IPHONEOS__) || defined(__TVOS__)
	MTLPixelFormatB5G6R5Unorm,	/* SurfaceFormat.Bgr565 */
	MTLPixelFormatBGR5A1Unorm,	/* SurfaceFormat.Bgra5551 */
	MTLPixelFormatABGR4Unorm,	/* SurfaceFormat.Bgra4444 */
#else
	MTLPixelFormatBGRA8Unorm,	/* SurfaceFormat.Bgr565 */
	MTLPixelFormatBGRA8Unorm,	/* SurfaceFormat.Bgra5551 */
	MTLPixelFormatBGRA8Unorm,	/* SurfaceFormat.Bgra4444 */
#endif
	MTLPixelFormatBC1RGBA,		/* SurfaceFormat.Dxt1 */
	MTLPixelFormatBC2RGBA,		/* SurfaceFormat.Dxt3 */
	MTLPixelFormatBC3RGBA,		/* SurfaceFormat.Dxt5 */
	MTLPixelFormatRG8Snorm, 	/* SurfaceFormat.NormalizedByte2 */
	MTLPixelFormatRG16Snorm,	/* SurfaceFormat.NormalizedByte4 */
	MTLPixelFormatRGB10A2Unorm,	/* SurfaceFormat.Rgba1010102 */
	MTLPixelFormatRG16Unorm,	/* SurfaceFormat.Rg32 */
	MTLPixelFormatRGBA16Unorm,	/* SurfaceFormat.Rgba64 */
	MTLPixelFormatA8Unorm,		/* SurfaceFormat.Alpha8 */
	MTLPixelFormatR32Float,		/* SurfaceFormat.Single */
	MTLPixelFormatRG32Float,	/* SurfaceFormat.Vector2 */
	MTLPixelFormatRGBA32Float,	/* SurfaceFormat.Vector4 */
	MTLPixelFormatR16Float,		/* SurfaceFormat.HalfSingle */
	MTLPixelFormatRG16Float,	/* SurfaceFormat.HalfVector2 */
	MTLPixelFormatRGBA16Float,	/* SurfaceFormat.HalfVector4 */
	MTLPixelFormatRGBA16Float,	/* SurfaceFormat.HdrBlendable */
	MTLPixelFormatBGRA8Unorm,	/* SurfaceFormat.ColorBgraEXT */
};

static MTLPixelFormat XNAToMTL_DepthFormat(
	MetalRenderer *renderer,
	FNA3D_DepthFormat format
) {
	switch (format)
	{
		case FNA3D_DEPTHFORMAT_D16:	return renderer->D16Format;
		case FNA3D_DEPTHFORMAT_D24:	return renderer->D24Format;
		case FNA3D_DEPTHFORMAT_D24S8:	return renderer->D24S8Format;
		case FNA3D_DEPTHFORMAT_NONE:	return MTLPixelFormatInvalid;
	}
}

static MOJOSHADER_usage XNAToMTL_VertexAttribUsage[] =
{
	MOJOSHADER_USAGE_POSITION,	/* VertexElementUsage.Position */
	MOJOSHADER_USAGE_COLOR,		/* VertexElementUsage.Color */
	MOJOSHADER_USAGE_TEXCOORD,	/* VertexElementUsage.TextureCoordinate */
	MOJOSHADER_USAGE_NORMAL,	/* VertexElementUsage.Normal */
	MOJOSHADER_USAGE_BINORMAL,	/* VertexElementUsage.Binormal */
	MOJOSHADER_USAGE_TANGENT,	/* VertexElementUsage.Tangent */
	MOJOSHADER_USAGE_BLENDINDICES,	/* VertexElementUsage.BlendIndices */
	MOJOSHADER_USAGE_BLENDWEIGHT,	/* VertexElementUsage.BlendWeight */
	MOJOSHADER_USAGE_DEPTH,		/* VertexElementUsage.Depth */
	MOJOSHADER_USAGE_FOG,		/* VertexElementUsage.Fog */
	MOJOSHADER_USAGE_POINTSIZE,	/* VertexElementUsage.PointSize */
	MOJOSHADER_USAGE_SAMPLE,	/* VertexElementUsage.Sample */
	MOJOSHADER_USAGE_TESSFACTOR	/* VertexElementUsage.TessellateFactor */
};

static MTLVertexFormat XNAToMTL_VertexAttribType[] =
{
	MTLVertexFormatFloat,			/* VertexElementFormat.Single */
	MTLVertexFormatFloat2,			/* VertexElementFormat.Vector2 */
	MTLVertexFormatFloat3,			/* VertexElementFormat.Vector3 */
	MTLVertexFormatFloat4,			/* VertexElementFormat.Vector4 */
	MTLVertexFormatUChar4Normalized,	/* VertexElementFormat.Color */
	MTLVertexFormatUChar4,			/* VertexElementFormat.Byte4 */
	MTLVertexFormatShort2,			/* VertexElementFormat.Short2 */
	MTLVertexFormatShort4,			/* VertexElementFormat.Short4 */
	MTLVertexFormatShort2Normalized,	/* VertexElementFormat.NormalizedShort2 */
	MTLVertexFormatShort4Normalized,	/* VertexElementFormat.NormalizedShort4 */
	MTLVertexFormatHalf2,			/* VertexElementFormat.HalfVector2 */
	MTLVertexFormatHalf4			/* VertexElementFormat.HalfVector4 */
};

static MTLIndexType XNAToMTL_IndexType[] =
{
	MTLIndexTypeUInt16,	/* IndexElementSize.SixteenBits */
	MTLIndexTypeUInt32	/* IndexElementSize.ThirtyTwoBits */
};

static int32_t XNAToMTL_IndexSize[] =
{
	2,	/* IndexElementSize.SixteenBits */
	4	/* IndexElementSize.ThirtyTwoBits */
};

static MTLBlendFactor XNAToMTL_BlendMode[] =
{
	MTLBlendFactorOne,			/* Blend.One */
	MTLBlendFactorZero,			/* Blend.Zero */
	MTLBlendFactorSourceColor,		/* Blend.SourceColor */
	MTLBlendFactorOneMinusSourceColor,	/* Blend.InverseSourceColor */
	MTLBlendFactorSourceAlpha,		/* Blend.SourceAlpha */
	MTLBlendFactorOneMinusSourceAlpha,	/* Blend.InverseSourceAlpha */
	MTLBlendFactorDestinationColor, 	/* Blend.DestinationColor */
	MTLBlendFactorOneMinusDestinationColor,	/* Blend.InverseDestinationColor */
	MTLBlendFactorDestinationAlpha, 	/* Blend.DestinationAlpha */
	MTLBlendFactorOneMinusDestinationAlpha,	/* Blend.InverseDestinationAlpha */
	MTLBlendFactorBlendColor,		/* Blend.BlendFactor */
	MTLBlendFactorOneMinusBlendColor,	/* Blend.InverseBlendFactor */
	MTLBlendFactorSourceAlphaSaturated	/* Blend.SourceAlphaSaturation */
};

static MTLBlendOperation XNAToMTL_BlendOperation[] =
{
	MTLBlendOperationAdd,			/* BlendFunction.Add */
	MTLBlendOperationSubtract,		/* BlendFunction.Subtract */
	MTLBlendOperationReverseSubtract,	/* BlendFunction.ReverseSubtract */
	MTLBlendOperationMax,			/* BlendFunction.Max */
	MTLBlendOperationMin			/* BlendFunction.Min */
};

static int32_t XNAToMTL_ColorWriteMask(FNA3D_ColorWriteChannels channels)
{
	if (channels == FNA3D_COLORWRITECHANNELS_NONE)
	{
		return 0x0;
	}
	if (channels == FNA3D_COLORWRITECHANNELS_ALL)
	{
		return 0xf;
	}

	int ret = 0;
	if ((channels & FNA3D_COLORWRITECHANNELS_RED) != 0)
	{
		ret |= (0x1 << 3);
	}
	if ((channels & FNA3D_COLORWRITECHANNELS_GREEN) != 0)
	{
		ret |= (0x1 << 2);
	}
	if ((channels & FNA3D_COLORWRITECHANNELS_BLUE) != 0)
	{
		ret |= (0x1 << 1);
	}
	if ((channels & FNA3D_COLORWRITECHANNELS_ALPHA) != 0)
	{
		ret |= (0x1 << 0);
	}
	return ret;
}

static MTLCompareFunction XNAToMTL_CompareFunc[] =
{
	MTLCompareFunctionAlways,	/* CompareFunction.Always */
	MTLCompareFunctionNever,	/* CompareFunction.Never */
	MTLCompareFunctionLess, 	/* CompareFunction.Less */
	MTLCompareFunctionLessEqual,	/* CompareFunction.LessEqual */
	MTLCompareFunctionEqual,	/* CompareFunction.Equal */
	MTLCompareFunctionGreaterEqual,	/* CompareFunction.GreaterEqual */
	MTLCompareFunctionGreater,	/* CompareFunction.Greater */
	MTLCompareFunctionNotEqual	/* CompareFunction.NotEqual */
};

static MTLStencilOperation XNAToMTL_StencilOp[] =
{
	MTLStencilOperationKeep,		/* StencilOperation.Keep */
	MTLStencilOperationZero,		/* StencilOperation.Zero */
	MTLStencilOperationReplace,		/* StencilOperation.Replace */
	MTLStencilOperationIncrementWrap,	/* StencilOperation.Increment */
	MTLStencilOperationDecrementWrap,	/* StencilOperation.Decrement */
	MTLStencilOperationIncrementClamp,	/* StencilOperation.IncrementSaturation */
	MTLStencilOperationDecrementClamp,	/* StencilOperation.DecrementSaturation */
	MTLStencilOperationInvert		/* StencilOperation.Invert */
};

static MTLTriangleFillMode XNAToMTL_FillMode[] =
{
	MTLTriangleFillModeFill,	/* FillMode.Solid */
	MTLTriangleFillModeLines,	/* FillMode.WireFrame */
};

static float XNAToMTL_DepthBiasScale(MTLPixelFormat format)
{
	switch (format)
	{
		case MTLPixelFormatDepth16Unorm:
			return (float) ((1 << 16) - 1);

		case MTLPixelFormatDepth24UnormStencil8:
			return (float) ((1 << 24) - 1);

		case MTLPixelFormatDepth32Float:
		case MTLPixelFormatDepth32FloatStencil8:
			return (float) ((1 << 23) - 1);

		default:
			return 0.0f;
	}

	SDL_assert(0 && "Invalid depth pixel format!");
}

static MTLCullMode XNAToMTL_CullingEnabled[] =
{
	MTLCullModeNone,		/* CullMode.None */
	MTLCullModeFront,		/* CullMode.Front */
	MTLCullModeBack 		/* CullMode.Back */
};

static MTLSamplerAddressMode XNAToMTL_Wrap[] =
{
	MTLSamplerAddressModeRepeat,		/* TextureAddressMode.Wrap */
	MTLSamplerAddressModeClampToEdge,	/* TextureAddressMode.Clamp */
	MTLSamplerAddressModeMirrorRepeat	/* TextureAddressMode.Mirror */
};

static MTLSamplerMinMagFilter XNAToMTL_MagFilter[] =
{
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.Linear */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.Point */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.Anisotropic */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.LinearMipPoint */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.PointMipLinear */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.MinLinearMagPointMipLinear */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.MinLinearMagPointMipPoint */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.MinPointMagLinearMipLinear */
	MTLSamplerMinMagFilterLinear	/* TextureFilter.MinPointMagLinearMipPoint */
};

static MTLSamplerMipFilter XNAToMTL_MipFilter[] =
{
	MTLSamplerMipFilterLinear,	/* TextureFilter.Linear */
	MTLSamplerMipFilterNearest,	/* TextureFilter.Point */
	MTLSamplerMipFilterLinear,	/* TextureFilter.Anisotropic */
	MTLSamplerMipFilterNearest,	/* TextureFilter.LinearMipPoint */
	MTLSamplerMipFilterLinear,	/* TextureFilter.PointMipLinear */
	MTLSamplerMipFilterLinear,	/* TextureFilter.MinLinearMagPointMipLinear */
	MTLSamplerMipFilterNearest,	/* TextureFilter.MinLinearMagPointMipPoint */
	MTLSamplerMipFilterLinear,	/* TextureFilter.MinPointMagLinearMipLinear */
	MTLSamplerMipFilterNearest	/* TextureFilter.MinPointMagLinearMipPoint */
};

static MTLSamplerMinMagFilter XNAToMTL_MinFilter[] =
{
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.Linear */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.Point */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.Anisotropic */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.LinearMipPoint */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.PointMipLinear */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.MinLinearMagPointMipLinear */
	MTLSamplerMinMagFilterLinear,	/* TextureFilter.MinLinearMagPointMipPoint */
	MTLSamplerMinMagFilterNearest,	/* TextureFilter.MinPointMagLinearMipLinear */
	MTLSamplerMinMagFilterNearest	/* TextureFilter.MinPointMagLinearMipPoint */
};

static MTLPrimitiveType XNAToMTL_Primitive[] =
{
	MTLPrimitiveTypeTriangle,	/* PrimitiveType.TriangleList */
	MTLPrimitiveTypeTriangleStrip,	/* PrimitiveType.TriangleStrip */
	MTLPrimitiveTypeLine,		/* PrimitiveType.LineList */
	MTLPrimitiveTypeLineStrip,	/* PrimitiveType.LineStrip */
	MTLPrimitiveTypePoint		/* PrimitiveType.PointListEXT */
};

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

static inline int32_t BytesPerImage(
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

static inline int32_t ClosestMSAAPower(int32_t value)
{
	/* Checking for the highest power of two _after_ than the given int:
	 * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
	 * Take result, divide by 2, get the highest power of two _before_!
	 * -flibit
	 */
	if (value == 1)
	{
		/* ... Except for 1, which is invalid for MSAA -flibit */
		return 0;
	}
	int result = value - 1;
	result |= result >> 1;
	result |= result >> 2;
	result |= result >> 4;
	result |= result >> 8;
	result |= result >> 16;
	result += 1;
	if (result == value)
	{
		return result;
	}
	return result >> 1;
}

static inline int32_t GetCompatibleSampleCount(
	MetalRenderer *renderer,
	int32_t sampleCount
) {
	/* If the device does not support the requested
	 * multisample count, halve it until we find a
	 * value that is supported.
	 */
	while (	sampleCount > 0 &&
		!mtlDeviceSupportsSampleCount(renderer->device, sampleCount))
	{
		sampleCount = ClosestMSAAPower(sampleCount / 2);
	}
	return sampleCount;
}

static MetalTexture* CreateTexture(
	MetalRenderer *renderer,
	MTLTexture *texture,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	MetalTexture *result = SDL_malloc(sizeof(MetalTexture));
	result->handle = texture;
	result->width = width;
	result->height = height;
	result->format = format;
	result->hasMipmaps = levelCount > 1;
	result->isPrivate = isRenderTarget;
	result->wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->filter = FNA3D_TEXTUREFILTER_LINEAR;
	result->anisotropy = 4.0f;
	result->maxMipmapLevel = 0;
	result->lodBias = 0.0f;
	result->next = NULL;
	return result;
}

/* Render Command Encoder Functions */

static void SetEncoderStencilReferenceValue(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetStencilReferenceValue(
			renderer->renderCommandEncoder,
			renderer->stencilRef
		);
	}
}

static void SetEncoderBlendColor(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetBlendColor(
			renderer->renderCommandEncoder,
			renderer->blendColor.r / 255.0f,
			renderer->blendColor.g / 255.0f,
			renderer->blendColor.b / 255.0f,
			renderer->blendColor.a / 255.0f
		);
	}
}

static void SetEncoderViewport(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetViewport(
			renderer->renderCommandEncoder,
			renderer->viewport.x,
			renderer->viewport.y,
			renderer->viewport.w,
			renderer->viewport.h,
			(double) renderer->viewport.minDepth,
			(double) renderer->viewport.maxDepth
		);
	}
}

static void SetEncoderScissorRect(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		if (!renderer->scissorTestEnable)
		{
			/* Set to the default scissor rect */
			mtlSetScissorRect(
				renderer->renderCommandEncoder,
				0,
				0,
				renderer->currentAttachmentWidth,
				renderer->currentAttachmentHeight
			);
		}
		else
		{
			mtlSetScissorRect(
				renderer->renderCommandEncoder,
				renderer->scissorRect.x,
				renderer->scissorRect.y,
				renderer->scissorRect.w,
				renderer->scissorRect.h
			);
		}
	}
}

static void SetEncoderCullMode(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetCullMode(
			renderer->renderCommandEncoder,
			XNAToMTL_CullingEnabled[renderer->cullFrontFace]
		);
	}
}

static void SetEncoderFillMode(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetTriangleFillMode(
			renderer->renderCommandEncoder,
			XNAToMTL_FillMode[renderer->fillMode]
		);
	}
}

static void SetEncoderDepthBias(MetalRenderer *renderer)
{
	if (	renderer->renderCommandEncoder != NULL &&
		!renderer->needNewRenderPass			)
	{
		mtlSetDepthBias(
			renderer->renderCommandEncoder,
			renderer->depthBias,
			renderer->slopeScaleDepthBias,
			0.0f /* no clamp */
		);
	}
}

static void EndPass(MetalRenderer *renderer)
{
	if (renderer->renderCommandEncoder != NULL)
	{
		mtlEndEncoding(renderer->renderCommandEncoder);
		renderer->renderCommandEncoder = NULL;
	}
}

static void METAL_BeginFrame(FNA3D_Renderer *driverData);
static void UpdateRenderPass(MetalRenderer *renderer)
{
	MTLRenderPassDescriptor *passDesc;
	MTLRenderPassColorAttachmentDescriptor *colorAttachment;
	MTLRenderPassDepthAttachmentDescriptor *depthAttachment;
	MTLRenderPassStencilAttachmentDescriptor *stencilAttachment;
	int32_t i;

	if (!renderer->needNewRenderPass)
	{
		/* Nothing for us to do! */
		return;
	}

	/* Normally the frame begins in BeginDraw(),
	 * but some games perform drawing outside
	 * of the Draw method (e.g. initializing
	 * render targets in LoadContent). This call
	 * ensures that we catch any unexpected draws.
	 * -caleb
	 */
	METAL_BeginFrame((FNA3D_Renderer*) renderer);

	/* Wrap up rendering with the old encoder */
	EndPass(renderer);

	/* Generate the descriptor */
	passDesc = mtlMakeRenderPassDescriptor();

	/* Bind color attachments */
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (renderer->currentAttachments[i] == NULL)
		{
			continue;
		}

		colorAttachment = mtlGetColorAttachment(passDesc, i);
		mtlSetAttachmentTexture(
			colorAttachment,
			renderer->currentAttachments[i]
		);
		mtlSetAttachmentSlice(
			colorAttachment,
			renderer->currentAttachmentSlices[i]
		);

		/* Multisample? */
		if (renderer->currentSampleCount > 0)
		{
			mtlSetAttachmentTexture(
				colorAttachment,
				renderer->currentMSAttachments[i]
			);
			mtlSetAttachmentSlice(
				colorAttachment,
				0
			);
			mtlSetAttachmentResolveTexture(
				colorAttachment,
				renderer->currentAttachments[i]
			);
			mtlSetAttachmentStoreAction(
				colorAttachment,
				MTLStoreActionMultisampleResolve
			);
			mtlSetAttachmentResolveSlice(
				colorAttachment,
				renderer->currentAttachmentSlices[i]
			);
		}

		/* Clear color */
		if (renderer->shouldClearColor)
		{
			mtlSetAttachmentLoadAction(
				colorAttachment,
				MTLLoadActionClear
			);
			mtlSetAttachmentClearColor(
				colorAttachment,
				renderer->clearColor.x,
				renderer->clearColor.y,
				renderer->clearColor.z,
				renderer->clearColor.w
			);
		}
		else
		{
			mtlSetAttachmentLoadAction(
				colorAttachment,
				MTLLoadActionLoad
			);
		}
	}

	/* Bind depth attachment */
	if (renderer->currentDepthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		depthAttachment = mtlGetDepthAttachment(passDesc);
		mtlSetAttachmentTexture(
			depthAttachment,
			renderer->currentDepthStencilBuffer
		);
		mtlSetAttachmentStoreAction(
			depthAttachment,
			MTLStoreActionStore
		);

		/* Clear? */
		if (renderer->shouldClearDepth)
		{
			mtlSetAttachmentLoadAction(
				depthAttachment,
				MTLLoadActionClear
			);
			mtlSetAttachmentClearDepth(
				depthAttachment,
				renderer->clearDepth
			);
		}
		else
		{
			mtlSetAttachmentLoadAction(
				depthAttachment,
				MTLLoadActionLoad
			);
		}
	}

	/* Bind stencil attachment */
	if (renderer->currentDepthFormat == FNA3D_DEPTHFORMAT_D24S8)
	{
		stencilAttachment = mtlGetStencilAttachment(passDesc);
		mtlSetAttachmentTexture(
			stencilAttachment,
			renderer->currentDepthStencilBuffer
		);
		mtlSetAttachmentStoreAction(
			stencilAttachment,
			MTLStoreActionStore
		);

		// Clear?
		if (renderer->shouldClearStencil)
		{
			mtlSetAttachmentLoadAction(
				stencilAttachment,
				MTLLoadActionClear
			);
			mtlSetAttachmentClearStencil(
				stencilAttachment,
				renderer->clearStencil
			);
		}
		else
		{
			mtlSetAttachmentLoadAction(
				stencilAttachment,
				MTLLoadActionLoad
			);
		}
	}

	/* Get attachment size */
	renderer->currentAttachmentWidth = mtlGetTextureWidth(
		renderer->currentAttachments[0]
	);
	renderer->currentAttachmentHeight = mtlGetTextureHeight(
		renderer->currentAttachments[0]
	);

	/* Attach the visibility buffer, if needed */
	if (renderer->currentVisibilityBuffer != NULL)
	{
		mtlSetVisibilityResultBuffer(
			passDesc,
			renderer->currentVisibilityBuffer
		);
	}

	/* Make a new encoder */
	renderer->renderCommandEncoder = mtlMakeRenderCommandEncoder(
		renderer->commandBuffer,
		passDesc
	);

	/* Reset the flags */
	renderer->needNewRenderPass = 0;
	renderer->shouldClearColor = 0;
	renderer->shouldClearDepth = 0;
	renderer->shouldClearStencil = 0;

	/* Apply the dynamic state */
	SetEncoderViewport(renderer);
	SetEncoderScissorRect(renderer);
	SetEncoderBlendColor(renderer);
	SetEncoderStencilReferenceValue(renderer);
	SetEncoderCullMode(renderer);
	SetEncoderFillMode(renderer);
	SetEncoderDepthBias(renderer);

	/* Start visibility buffer counting */
	if (renderer->currentVisibilityBuffer != NULL)
	{
		mtlSetVisibilityResultMode(
			renderer->renderCommandEncoder,
			MTLVisibilityResultModeCounting,
			0
		);
	}

	/* Reset the bindings */
	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		if (renderer->textures[i] != &NullTexture)
		{
			renderer->textureNeedsUpdate[i] = 1;
		}
		if (renderer->samplers[i] != NULL)
		{
			renderer->samplerNeedsUpdate[i] = 1;
		}
	}
	renderer->ldDepthStencilState = NULL;
	renderer->ldFragUniformBuffer = NULL;
	renderer->ldFragUniformOffset = 0;
	renderer->ldVertUniformBuffer = NULL;
	renderer->ldVertUniformOffset = 0;
	renderer->ldPipelineState = NULL;
	for (i = 0; i < MAX_BOUND_VERTEX_BUFFERS; i += 1)
	{
		renderer->ldVertexBuffers[i] = NULL;
		renderer->ldVertexBufferOffsets[i] = 0;
	}
}

/* Pipeline Stall Function */

static void Stall(MetalRenderer *renderer)
{
	MetalBuffer *buf;

	EndPass(renderer);
	mtlCommitCommandBuffer(renderer->commandBuffer);
	mtlWaitUntilCompleted(renderer->commandBuffer);

	renderer->commandBuffer = mtlMakeCommandBuffer(renderer->queue);
	renderer->needNewRenderPass = 1;
	/* FIXME: If maxFramesInFlight > 1, reset the frame semaphore! */

	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}
}

/* Buffer Helper Functions */

static void CreateBackingBuffer(
	MetalRenderer *renderer,
	MetalBuffer *buffer,
	int32_t prevSize
) {
	MTLBuffer *oldBuffer = buffer->handle;
	MTLBuffer *oldContents = buffer->contents;

	buffer->handle = mtlNewBuffer(
		renderer->device,
		buffer->internalBufferSize,
		buffer->usage == FNA3D_BUFFERUSAGE_WRITEONLY ?
			MTLResourceOptionsCPUCacheModeWriteCombined :
			MTLResourceOptionsCPUCacheModeDefaultCache
	);
	buffer->contents = mtlGetBufferContents(buffer->handle);

	/* Copy over data from the old buffer */
	if (oldBuffer != NULL)
	{
		SDL_memcpy(
			buffer->contents,
			oldContents,
			sizeof(prevSize)
		);
		objc_release(oldBuffer);
	}
}

static MetalBuffer* CreateBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_BufferUsage usage,
	int32_t size
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBuffer *result, *curr;

	/* Allocate the buffer */
	result = SDL_malloc(sizeof(MetalBuffer));
	SDL_memset(result, '\0', sizeof(MetalBuffer));

	/* Set up the buffer */
	result->usage = usage;
	result->size = size;
	result->internalBufferSize = size;
	CreateBackingBuffer(renderer, result, -1);

	LinkedList_Add(renderer->buffers, result, curr);
	return result;
}

static void DestroyBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBuffer *mtlBuffer, *curr, *prev;

	mtlBuffer = (MetalBuffer*) buffer;
	LinkedList_Remove(
		renderer->buffers,
		mtlBuffer,
		curr,
		prev
	);
	objc_release(mtlBuffer->handle);
	mtlBuffer->handle = NULL;
	SDL_free(mtlBuffer);
}

static void SetBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBuffer *mtlBuffer = (MetalBuffer*) buffer;
	uint8_t *contentsPtr;
	int32_t sizeRequired, prevSize;

	/* Handle overwrites */
	if (mtlBuffer->boundThisFrame)
	{
		if (options == FNA3D_SETDATAOPTIONS_NONE)
		{
			Stall(renderer);
			mtlBuffer->boundThisFrame = 1;
		}
		else if (options == FNA3D_SETDATAOPTIONS_DISCARD)
		{
			mtlBuffer->internalOffset += mtlBuffer->size;
			sizeRequired = mtlBuffer->internalOffset + dataLength;
			if (sizeRequired > mtlBuffer->internalBufferSize)
			{
				/* Expand! */
				prevSize = mtlBuffer->internalBufferSize;
				mtlBuffer->internalBufferSize *= 2;
				CreateBackingBuffer(
					renderer,
					mtlBuffer,
					prevSize
				);
			}
		}
	}

	/* Copy previous contents, if needed */
	contentsPtr = (uint8_t*) mtlBuffer->contents;
	if (	dataLength < mtlBuffer->size &&
		mtlBuffer->prevInternalOffset != mtlBuffer->internalOffset)
	{
		SDL_memcpy(
			contentsPtr + mtlBuffer->internalOffset,
			contentsPtr + mtlBuffer->prevInternalOffset,
			mtlBuffer->size
		);
	}

	/* Copy the data into the buffer */
	SDL_memcpy(
		contentsPtr + mtlBuffer->internalOffset + offsetInBytes,
		data,
		dataLength
	);

	mtlBuffer->prevInternalOffset = mtlBuffer->internalOffset;
}

static void SetUserBufferData(
	MetalRenderer *renderer,
	MetalBuffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	int32_t sizeRequired, prevSize;

	buffer->internalOffset += buffer->prevDataLength;
	sizeRequired = buffer->internalOffset + dataLength;
	if (sizeRequired > buffer->internalBufferSize)
	{
		/* Expand! */
		prevSize = buffer->internalBufferSize;
		buffer->internalBufferSize = SDL_max(
			buffer->internalBufferSize * 2,
			buffer->internalBufferSize + dataLength
		);
		CreateBackingBuffer(renderer, buffer, prevSize);
	}

	/* Copy the data into the buffer */
	SDL_memcpy(
		(uint8_t*) buffer->contents + buffer->internalOffset,
		(uint8_t*) data + offsetInBytes,
		dataLength
	);

	buffer->prevDataLength = dataLength;
}

static void BindUserVertexBuffer(
	MetalRenderer *renderer,
	void* vertexData,
	int32_t vertexCount,
	int32_t vertexOffset
) {
	int32_t len, offset;
	MTLBuffer *handle;

	/* Update the buffer contents */
	len = vertexCount * renderer->userVertexStride;
	if (renderer->userVertexBuffer == NULL)
	{
		renderer->userVertexBuffer = CreateBuffer(
			(FNA3D_Renderer*) renderer,
			FNA3D_BUFFERUSAGE_WRITEONLY,
			len
		);
	}
	SetUserBufferData(
		renderer,
		renderer->userVertexBuffer,
		vertexOffset * renderer->userVertexStride,
		vertexData,
		len
	);

	/* Bind the buffer */
	offset = renderer->userVertexBuffer->internalOffset;
	handle = renderer->userVertexBuffer->handle;
	if (renderer->ldVertexBuffers[0] != handle)
	{
		mtlSetVertexBuffer(
			renderer->renderCommandEncoder,
			handle,
			offset,
			0
		);
		renderer->ldVertexBuffers[0] = handle;
		renderer->ldVertexBufferOffsets[0] = offset;
	}
	else if (renderer->ldVertexBufferOffsets[0] != offset)
	{
		mtlSetVertexBufferOffset(
			renderer->renderCommandEncoder,
			offset,
			0
		);
		renderer->ldVertexBufferOffsets[0] = offset;
	}
}

/* Pipeline State Object Creation / Retrieval */

typedef struct PipelineHash
{
	uint64_t a;
	uint64_t b;
	uint64_t c;
	uint64_t d;
} PipelineHash;

struct PipelineHashMap
{
	PipelineHash key;
	MTLRenderPipelineState *value;
};

static int32_t GetBlendStateHashCode(FNA3D_BlendState blendState)
{
	StateHash hash = GetBlendStateHash(blendState);
	return (
		(hash.a ^ (hash.a >> 32)) +
		(hash.b ^ (hash.b >> 32))
	);
}

static int32_t HashPixelFormat(MTLPixelFormat format)
{
	switch (format)
	{
		case MTLPixelFormatInvalid:
			return 0;
		case MTLPixelFormatR16Float:
			return 1;
		case MTLPixelFormatR32Float:
			return 2;
		case MTLPixelFormatRG16Float:
			return 3;
		case MTLPixelFormatRG16Snorm:
			return 4;
		case MTLPixelFormatRG16Unorm:
			return 5;
		case MTLPixelFormatRG32Float:
			return 6;
		case MTLPixelFormatRG8Snorm:
			return 7;
		case MTLPixelFormatRGB10A2Unorm:
			return 8;
		case MTLPixelFormatRGBA16Float:
			return 9;
		case MTLPixelFormatRGBA16Unorm:
			return 10;
		case MTLPixelFormatRGBA32Float:
			return 11;
		case MTLPixelFormatRGBA8Unorm:
			return 12;
		case MTLPixelFormatA8Unorm:
			return 13;
		case MTLPixelFormatABGR4Unorm:
			return 14;
		case MTLPixelFormatB5G6R5Unorm:
			return 15;
		case MTLPixelFormatBC1RGBA:
			return 16;
		case MTLPixelFormatBC2RGBA:
			return 17;
		case MTLPixelFormatBC3RGBA:
			return 18;
		case MTLPixelFormatBGR5A1Unorm:
			return 19;
		case MTLPixelFormatBGRA8Unorm:
			return 20;
		default:
			SDL_assert(0 && "Invalid pixel format!");
	}

	/* This should never happen! */
	return 0;
}

static PipelineHash GetPipelineHash(MetalRenderer *renderer)
{
	PipelineHash result;
	int32_t packedProperties = (
		  renderer->currentSampleCount << 22
		| renderer->currentDepthFormat << 20
		| HashPixelFormat(renderer->currentColorFormats[3]) << 15
		| HashPixelFormat(renderer->currentColorFormats[2]) << 10
		| HashPixelFormat(renderer->currentColorFormats[1]) << 5
		| HashPixelFormat(renderer->currentColorFormats[0])
	);
	result.a = (uint64_t) renderer->currentShaderState.vertexShader;
	result.b = (uint64_t) renderer->currentShaderState.fragmentShader;
	result.c = (uint64_t) renderer->currentVertexDescriptor;
	result.d = (
		(uint64_t) GetBlendStateHashCode(renderer->blendState) << 32 |
		(uint64_t) packedProperties
	);
	return result;
}

static MTLRenderPipelineState* FetchRenderPipeline(MetalRenderer *renderer)
{
	PipelineHash hash = GetPipelineHash(renderer);
	MTLRenderPipelineDescriptor *pipelineDesc;
	MTLFunction *vertHandle;
	MTLFunction *fragHandle;
	uint8_t alphaBlendEnable;
	int32_t i;
	MTLRenderPipelineColorAttachmentDescriptor *colorAttachment;
	MTLRenderPipelineState *result;

	/* Can we just reuse an existing pipeline? */
	result = hmget(renderer->pipelineStateCache, hash);
	if (result != NULL)
	{
		/* We already have this state cached! */
		return result;
	}

	/* We have to make a new pipeline... */
	pipelineDesc = mtlNewRenderPipelineDescriptor();
	vertHandle = MOJOSHADER_mtlGetFunctionHandle(
		renderer->currentShaderState.vertexShader
	);
	fragHandle = MOJOSHADER_mtlGetFunctionHandle(
		renderer->currentShaderState.fragmentShader
	);
	mtlSetPipelineVertexFunction(
		pipelineDesc,
		vertHandle
	);
	mtlSetPipelineFragmentFunction(
		pipelineDesc,
		fragHandle
	);
	mtlSetPipelineVertexDescriptor(
		pipelineDesc,
		renderer->currentVertexDescriptor
	);
	mtlSetDepthAttachmentPixelFormat(
		pipelineDesc,
		XNAToMTL_DepthFormat(renderer, renderer->currentDepthFormat)
	);
	if (renderer->currentDepthFormat == FNA3D_DEPTHFORMAT_D24S8)
	{
		mtlSetStencilAttachmentPixelFormat(
			pipelineDesc,
			XNAToMTL_DepthFormat(renderer, renderer->currentDepthFormat)
		);
	}
	mtlSetPipelineSampleCount(
		pipelineDesc,
		SDL_max(1, renderer->currentSampleCount)
	);

	/* Apply the blend state */
	alphaBlendEnable = !(
		renderer->blendState.colorSourceBlend == FNA3D_BLEND_ONE &&
		renderer->blendState.colorDestinationBlend == FNA3D_BLEND_ZERO &&
		renderer->blendState.alphaSourceBlend == FNA3D_BLEND_ONE &&
		renderer->blendState.alphaDestinationBlend == FNA3D_BLEND_ZERO
	);
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (renderer->currentAttachments[i] == NULL)
		{
			/* There's no attachment bound at this index. */
			continue;
		}

		colorAttachment = mtlGetColorAttachment(
			pipelineDesc,
			i
		);
		mtlSetAttachmentPixelFormat(
			colorAttachment,
			renderer->currentColorFormats[i]
		);
		mtlSetAttachmentBlendingEnabled(
			colorAttachment,
			alphaBlendEnable
		);
		if (alphaBlendEnable)
		{
			mtlSetAttachmentSourceRGBBlendFactor(
				colorAttachment,
				XNAToMTL_BlendMode[
					renderer->blendState.colorSourceBlend
				]
			);
			mtlSetAttachmentDestinationRGBBlendFactor(
				colorAttachment,
				XNAToMTL_BlendMode[
					renderer->blendState.colorDestinationBlend
				]
			);
			mtlSetAttachmentSourceAlphaBlendFactor(
				colorAttachment,
				XNAToMTL_BlendMode[
					renderer->blendState.alphaSourceBlend
				]
			);
			mtlSetAttachmentDestinationAlphaBlendFactor(
				colorAttachment,
				XNAToMTL_BlendMode[
					renderer->blendState.alphaDestinationBlend
				]
			);
			mtlSetAttachmentRGBBlendOperation(
				colorAttachment,
				XNAToMTL_BlendOperation[
					renderer->blendState.colorBlendFunction
				]
			);
			mtlSetAttachmentAlphaBlendOperation(
				colorAttachment,
				XNAToMTL_BlendOperation[
					renderer->blendState.alphaBlendFunction
				]
			);
		}

		/* FIXME: So how exactly do we factor in
		 * COLORWRITEENABLE for buffer 0? Do we just assume that
		 * the default is just buffer 0, and all other calls
		 * update the other write masks?
		 */
		if (i == 0)
		{
			mtlSetAttachmentWriteMask(
				colorAttachment,
				XNAToMTL_ColorWriteMask(
					renderer->blendState.colorWriteEnable
				)
			);
		}
		else if (i == 1)
		{
			mtlSetAttachmentWriteMask(
				mtlGetColorAttachment(pipelineDesc, 1),
				XNAToMTL_ColorWriteMask(
					renderer->blendState.colorWriteEnable1
				)
			);
		}
		else if (i == 2)
		{
			mtlSetAttachmentWriteMask(
				mtlGetColorAttachment(pipelineDesc, 2),
				XNAToMTL_ColorWriteMask(
					renderer->blendState.colorWriteEnable2
				)
			);
		}
		else if (i == 3)
		{
			mtlSetAttachmentWriteMask(
				mtlGetColorAttachment(pipelineDesc, 3),
				XNAToMTL_ColorWriteMask(
					renderer->blendState.colorWriteEnable3
				)
			);
		}
	}

	/* Bake the render pipeline! */
	result = mtlNewRenderPipelineState(
		renderer->device,
		pipelineDesc
	);
	hmput(renderer->pipelineStateCache, hash, result);

	/* Clean up */
	objc_release(pipelineDesc);
	objc_release(vertHandle);
	objc_release(fragHandle);

	/* Return the pipeline! */
	return result;
}

static MTLDepthStencilState* FetchDepthStencilState(MetalRenderer *renderer)
{
	StateHash hash;
	MTLDepthStencilState *state;
	MTLDepthStencilDescriptor *dsDesc;
	MTLStencilDescriptor *front, *back;
	uint8_t zEnable, sEnable, zFormat;

	/* Just use the default depth-stencil state
	 * if depth and stencil testing are disabled,
	 * or if there is no bound depth attachment.
	 * This wards off Metal validation errors.
	 * -caleb
	 */
	zEnable = renderer->depthStencilState.depthBufferEnable;
	sEnable = renderer->depthStencilState.stencilEnable;
	zFormat = (renderer->currentDepthFormat != FNA3D_DEPTHFORMAT_NONE);
	if ((!zEnable && !sEnable) || (!zFormat))
	{
		return renderer->defaultDepthStencilState;
	}

	/* Can we just reuse an existing state? */
	hash = GetDepthStencilStateHash(renderer->depthStencilState);
	state = hmget(renderer->depthStencilStateCache, hash);
	if (state != NULL)
	{
		/* This state has already been cached! */
		return state;
	}

	/* We have to make a new DepthStencilState... */
	dsDesc = mtlNewDepthStencilDescriptor();
	if (zEnable)
	{
		mtlSetDepthCompareFunction(
			dsDesc,
			XNAToMTL_CompareFunc[
				renderer->depthStencilState.depthBufferFunction
			]
		);
		mtlSetDepthWriteEnabled(
			dsDesc,
			renderer->depthStencilState.depthBufferWriteEnable
		);
	}

	/* Create stencil descriptors */
	front = NULL;
	back = NULL;

	if (sEnable)
	{
		front = mtlNewStencilDescriptor();
		mtlSetStencilFailureOperation(
			front,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.stencilFail
			]
		);
		mtlSetDepthFailureOperation(
			front,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.stencilDepthBufferFail
			]
		);
		mtlSetDepthStencilPassOperation(
			front,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.stencilPass
			]
		);
		mtlSetStencilCompareFunction(
			front,
			XNAToMTL_CompareFunc[
				renderer->depthStencilState.stencilFunction
			]
		);
		mtlSetStencilReadMask(
			front,
			(uint32_t) renderer->depthStencilState.stencilMask
		);
		mtlSetStencilWriteMask(
			front,
			(uint32_t) renderer->depthStencilState.stencilWriteMask
		);

		if (!renderer->depthStencilState.twoSidedStencilMode)
		{
			back = front;
		}
	}

	if (front != back)
	{
		back = mtlNewStencilDescriptor();
		mtlSetStencilFailureOperation(
			back,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.ccwStencilFail
			]
		);
		mtlSetDepthFailureOperation(
			back,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.ccwStencilDepthBufferFail
			]
		);
		mtlSetDepthStencilPassOperation(
			back,
			XNAToMTL_StencilOp[
				renderer->depthStencilState.ccwStencilPass
			]
		);
		mtlSetStencilCompareFunction(
			back,
			XNAToMTL_CompareFunc[
				renderer->depthStencilState.ccwStencilFunction
			]
		);
		mtlSetStencilReadMask(
			back,
			(uint32_t) renderer->depthStencilState.stencilMask
		);
		mtlSetStencilWriteMask(
			back,
			(uint32_t) renderer->depthStencilState.stencilWriteMask
		);
	}

	mtlSetFrontFaceStencil(
		dsDesc,
		front
	);
	mtlSetBackFaceStencil(
		dsDesc,
		back
	);

	/* Bake the state! */
	state = mtlNewDepthStencilState(
		renderer->device,
		dsDesc
	);
	hmput(renderer->depthStencilStateCache, hash, state);

	/* Clean up */
	objc_release(dsDesc);

	/* Return the state! */
	return state;
}

static MTLSamplerState* FetchSamplerState(
	MetalRenderer *renderer,
	FNA3D_SamplerState *samplerState,
	uint8_t hasMipmaps
) {
	StateHash hash;
	MTLSamplerState *state;
	MTLSamplerDescriptor *desc;

	/* Can we reuse an existing state? */
	hash = GetSamplerStateHash(*samplerState);
	state = hmget(renderer->samplerStateCache, hash);
	if (state != NULL)
	{
		/* This state has already been cached! */
		return state;
	}

	/* We have to make a new sampler state... */
	desc = mtlNewSamplerDescriptor();

	mtlSetSampler_sAddressMode(
		desc,
		XNAToMTL_Wrap[samplerState->addressU]
	);
	mtlSetSampler_tAddressMode(
		desc,
		XNAToMTL_Wrap[samplerState->addressV]
	);
	mtlSetSampler_rAddressMode(
		desc,
		XNAToMTL_Wrap[samplerState->addressW]
	);
	mtlSetSamplerMagFilter(
		desc,
		XNAToMTL_MagFilter[samplerState->filter]
	);
	mtlSetSamplerMinFilter(
		desc,
		XNAToMTL_MinFilter[samplerState->filter]
	);
	if (hasMipmaps)
	{
		mtlSetSamplerMipFilter(
			desc,
			XNAToMTL_MipFilter[samplerState->filter]
		);
	}
	mtlSetSamplerLodMinClamp(
		desc,
		samplerState->maxMipLevel
	);
	mtlSetSamplerMaxAnisotropy(
		desc,
		samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC ?
			SDL_max(1, samplerState->maxAnisotropy) :
			1
	);

	/* FIXME:
	 * The only way to set lod bias in metal is via the MSL
	 * bias() function in a shader. So we can't do:
	 *
	 * 	mtlSetSamplerLodBias(
	 *		samplerDesc,
	 *		samplerState.MipMapLevelOfDetailBias
	 *	);
	 *
	 * What should we do instead?
	 *
	 * -caleb
	 */

	/* Bake the state! */
	state = mtlNewSamplerState(
		renderer->device,
		desc
	);
	hmput(renderer->samplerStateCache, hash, state);

	/* Clean up */
	objc_release(desc);

	/* Return the state! */
	return state;
}

static MTLTexture* FetchTransientTexture(
	MetalRenderer *renderer,
	MetalTexture *fromTexture
) {
	MTLTextureDescriptor *desc;
	MetalTexture *result, *curr;

	/* Can we just reuse an existing texture? */
	curr = renderer->transientTextures;
	while (curr != NULL)
	{
		if (	curr->format == fromTexture->format &&
			curr->width == fromTexture->width &&
			curr->height == fromTexture->height &&
			curr->hasMipmaps == fromTexture->hasMipmaps	)
		{
			mtlSetPurgeableState(
				curr->handle,
				MTLPurgeableStateNonVolatile
			);
			return curr->handle;
		}
		curr = curr->next;
	}

	/* We have to make a new texture... */
	desc = mtlMakeTexture2DDescriptor(
		XNAToMTL_TextureFormat[fromTexture->format],
		fromTexture->width,
		fromTexture->height,
		fromTexture->hasMipmaps
	);
	result = CreateTexture(
		renderer,
		mtlNewTexture(renderer->device, desc),
		fromTexture->format,
		fromTexture->width,
		fromTexture->height,
		fromTexture->hasMipmaps ? 2 : 0,
		0
	);
	LinkedList_Add(renderer->transientTextures, result, curr);
	return result->handle;
}

static MTLVertexDescriptor* FetchVertexBufferBindingsDescriptor(
	MetalRenderer *renderer,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings
) {
	uint64_t hash;
	int32_t i, j, k, usage, index, attribLoc;
	FNA3D_VertexDeclaration vertexDeclaration;
	FNA3D_VertexElement element;
	MTLVertexAttributeDescriptor *attrib;
	MTLVertexBufferLayoutDescriptor *layout;
	MTLVertexDescriptor *result;

	/* Can we just reuse an existing descriptor? */
	hash = GetVertexBufferBindingsHash(
		bindings,
		numBindings,
		renderer->currentShaderState.vertexShader
	);
	result = hmget(renderer->vertexDescriptorCache, hash);
	if (result != NULL)
	{
		/* This descriptor has already been cached! */
		return result;
	}

	/* We have to make a new vertex descriptor... */
	result = mtlMakeVertexDescriptor();
	objc_retain(result);

	/* There's this weird case where you can have overlapping
	 * vertex usage/index combinations. It seems like the first
	 * attrib gets priority, so whenever a duplicate attribute
	 * exists, give it the next available index. If that fails, we
	 * have to crash :/
	 * -flibit
	 */
	SDL_memset(renderer->attrUse, '\0', sizeof(renderer->attrUse));
	for (i = 0; i < numBindings; i += 1)
	{
		/* Describe vertex attributes */
		vertexDeclaration = bindings[i].vertexDeclaration;
		for (j = 0; j < vertexDeclaration.elementCount; j += 1)
		{
			element = vertexDeclaration.elements[j];
			usage = element.vertexElementUsage;
			index = element.usageIndex;
			if (renderer->attrUse[usage][index])
			{
				index = -1;
				for (k = 0; k < 16; k += 1)
				{
					if (!renderer->attrUse[usage][k])
					{
						index = k;
						break;
					}
				}
				if (index < 0)
				{
					FNA3D_LogError(
						"Vertex usage collision!"
					);
				}
			}
			renderer->attrUse[usage][index] = 1;
			attribLoc = MOJOSHADER_mtlGetVertexAttribLocation(
				renderer->currentShaderState.vertexShader,
				XNAToMTL_VertexAttribUsage[usage],
				index
			);
			if (attribLoc == -1)
			{
				/* Stream not in use! */
				continue;
			}
			attrib = mtlGetVertexAttributeDescriptor(
				result,
				attribLoc
			);
			mtlSetVertexAttributeFormat(
				attrib,
				XNAToMTL_VertexAttribType[element.vertexElementFormat]
			);
			mtlSetVertexAttributeOffset(
				attrib,
				element.offset
			);
			mtlSetVertexAttributeBufferIndex(
				attrib,
				i
			);
		}

		/* Describe vertex buffer layout */
		layout = mtlGetVertexBufferLayoutDescriptor(
			result,
			i
		);
		mtlSetVertexBufferLayoutStride(
			layout,
			vertexDeclaration.vertexStride
		);
		if (bindings[i].instanceFrequency > 0)
		{
			mtlSetVertexBufferLayoutStepFunction(
				layout,
				MTLVertexStepFunctionPerInstance
			);
			mtlSetVertexBufferLayoutStepRate(
				layout,
				bindings[i].instanceFrequency
			);
		}
	}

	hmput(renderer->vertexDescriptorCache, hash, result);
	return result;
}

static MTLVertexDescriptor* FetchVertexDeclarationDescriptor(
	MetalRenderer *renderer,
	FNA3D_VertexDeclaration *vertexDeclaration,
	int32_t vertexOffset
) {
	uint64_t hash;
	int32_t i, j, usage, index, attribLoc;
	FNA3D_VertexElement element;
	MTLVertexAttributeDescriptor *attrib;
	MTLVertexBufferLayoutDescriptor *layout;
	MTLVertexDescriptor *result;

	/* Can we just reuse an existing descriptor? */
	hash = GetVertexDeclarationHash(
		*vertexDeclaration,
		renderer->currentShaderState.vertexShader
	);
	result = hmget(renderer->vertexDescriptorCache, hash);
	if (result != NULL)
	{
		/* This descriptor has already been cached! */
		return result;
	}

	/* We have to make a new vertex descriptor... */
	result = mtlMakeVertexDescriptor();
	objc_retain(result);

	/* There's this weird case where you can have overlapping
	 * vertex usage/index combinations. It seems like the first
	 * attrib gets priority, so whenever a duplicate attribute
	 * exists, give it the next available index. If that fails, we
	 * have to crash :/
	 * -flibit
	 */
	SDL_memset(renderer->attrUse, '\0', sizeof(renderer->attrUse));
	for (i = 0; i < vertexDeclaration->elementCount; i += 1)
	{
		element = vertexDeclaration->elements[i];
		usage = element.vertexElementUsage;
		index = element.usageIndex;
		if (renderer->attrUse[usage][index])
		{
			index = -1;
			for (j = 0; j < 16; j += 1)
			{
				if (!renderer->attrUse[usage][j])
				{
					index = j;
					break;
				}
			}
			if (index < 0)
			{
				FNA3D_LogError(
					"Vertex usage collision!"
				);
			}
		}
		renderer->attrUse[usage][index] = 1;
		attribLoc = MOJOSHADER_mtlGetVertexAttribLocation(
			renderer->currentShaderState.vertexShader,
			XNAToMTL_VertexAttribUsage[usage],
			index
		);
		if (attribLoc == -1)
		{
			/* Stream not in use! */
			continue;
		}
		attrib = mtlGetVertexAttributeDescriptor(
			result,
			attribLoc
		);
		mtlSetVertexAttributeFormat(
			attrib,
			XNAToMTL_VertexAttribType[element.vertexElementFormat]
		);
		mtlSetVertexAttributeOffset(
			attrib,
			element.offset
		);
		mtlSetVertexAttributeBufferIndex(
			attrib,
			0
		);
	}

	/* Describe vertex buffer layout */
	layout = mtlGetVertexBufferLayoutDescriptor(
		result,
		0
	);
	mtlSetVertexBufferLayoutStride(
		layout,
		vertexDeclaration->vertexStride
	);

	hmput(renderer->vertexDescriptorCache, hash, result);
	return result;
}

/* Renderer Implementation */

/* Quit */

static void DestroyFramebuffer(MetalRenderer *renderer);
static void METAL_DestroyDevice(FNA3D_Device *device)
{
	MetalRenderer *renderer = (MetalRenderer*) device->driverData;
	int32_t i;
	MetalTexture *tex, *next;

	/* Stop rendering */
	EndPass(renderer);

	/* Release vertex descriptors */
	for (i = 0; i < hmlen(renderer->vertexDescriptorCache); i += 1)
	{
		objc_release(renderer->vertexDescriptorCache[i].value);
	}
	hmfree(renderer->vertexDescriptorCache);

	/* Release depth stencil states */
	for (i = 0; i < hmlen(renderer->depthStencilStateCache); i += 1)
	{
		objc_release(renderer->depthStencilStateCache[i].value);
	}
	hmfree(renderer->depthStencilStateCache);

	/* Release pipeline states */
	for (i = 0; i < hmlen(renderer->pipelineStateCache); i += 1)
	{
		objc_release(renderer->pipelineStateCache[i].value);
	}
	hmfree(renderer->pipelineStateCache);

	/* Release sampler states */
	for (i = 0; i < hmlen(renderer->samplerStateCache); i += 1)
	{
		objc_release(renderer->samplerStateCache[i].value);
	}
	hmfree(renderer->samplerStateCache);

	/* Release transient textures */
	tex = renderer->transientTextures;
	while (tex != NULL)
	{
		next = tex->next;
		objc_release(tex->handle);
		SDL_free(tex);
		tex = next;
	}
	renderer->transientTextures = NULL;

	/* Destroy the backbuffer */
	DestroyFramebuffer(renderer);
	SDL_free(renderer->backbuffer);

	/* Destroy the view */
	SDL_Metal_DestroyView(renderer->view);

	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void METAL_BeginFrame(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (renderer->frameInProgress)
	{
		return;
	}

	/* Wait for command buffers to complete... */
	SDL_SemWait(renderer->frameSemaphore);

	/* The cycle begins anew! */
	renderer->frameInProgress = 1;
	renderer->pool = objc_autoreleasePoolPush();
	renderer->commandBuffer = mtlMakeCommandBuffer(renderer->queue);
}

static void BlitFramebuffer(
	MetalRenderer *renderer,
	MTLTexture *srcTex,
	FNA3D_Rect srcRect,
	MTLTexture *dstTex,
	FNA3D_Rect dstRect,
	int32_t drawableWidth,
	int32_t drawableHeight
) {
	float sx, sy, sw, sh;
	MTLRenderPassDescriptor *pass;
	MTLRenderCommandEncoder *rce;

	if (	srcRect.w == 0 ||
		srcRect.h == 0 ||
		dstRect.w == 0 ||
		dstRect.h == 0		)
	{
		/* Enjoy that bright red window! */
		return;
	}

	/* Update cached vertex buffer if needed */
	if (	renderer->backbufferSizeChanged ||
		renderer->backbufferDestBounds.x != dstRect.x ||
		renderer->backbufferDestBounds.y != dstRect.y ||
		renderer->backbufferDestBounds.w != dstRect.w ||
		renderer->backbufferDestBounds.h != dstRect.h	)
	{
		renderer->backbufferDestBounds = dstRect;
		renderer->backbufferSizeChanged = 0;

		/* Scale the coordinates to (-1, 1) */
		sx = -1 + (dstRect.x / (float) drawableWidth);
		sy = -1 + (dstRect.y / (float) drawableHeight);
		sw = (dstRect.w / (float) drawableWidth) * 2;
		sh = (dstRect.h / (float) drawableHeight) * 2;

		float data[] =
		{
			sx, sy,			0, 0,
			sx + sw, sy,		1, 0,
			sx + sw, sy + sh,	1, 1,
			sx, sy + sh,		0, 1
		};
		SDL_memcpy(
			mtlGetBufferContents(renderer->backbufferDrawBuffer),
			data,
			sizeof(data)
		);
	}

	/* Render the source texture to the destination texture */
	pass = mtlMakeRenderPassDescriptor();
	mtlSetAttachmentTexture(
		mtlGetColorAttachment(pass, 0),
		dstTex
	);
	rce = mtlMakeRenderCommandEncoder(
		renderer->commandBuffer,
		pass
	);
	mtlSetRenderPipelineState(rce, renderer->backbufferPipeline);
	mtlSetVertexBuffer(rce, renderer->backbufferDrawBuffer, 0, 0);
	mtlSetFragmentTexture(rce, srcTex, 0);
	mtlSetFragmentSamplerState(rce, renderer->backbufferSamplerState, 0);
	mtlDrawIndexedPrimitives(
		rce,
		MTLPrimitiveTypeTriangle,
		6,
		MTLIndexTypeUInt16,
		renderer->backbufferDrawBuffer,
		16 * sizeof(float),
		1
	);
	mtlEndEncoding(rce);
}

static void METAL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
);
static void METAL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	FNA3D_Rect srcRect, dstRect;
	CGSize drawableSize;
	MTLDrawable *drawable;
	MetalBuffer *buf;

	/* Just in case Present() is called
	 * before any rendering happens...
	 */
	METAL_BeginFrame(driverData);

	/* Bind the backbuffer and finalize rendering */
	METAL_SetRenderTargets(
		driverData,
		NULL,
		0,
		NULL,
		FNA3D_DEPTHFORMAT_NONE
	);
	EndPass(renderer);

	/* Get the drawable size */
	drawableSize = mtlGetDrawableSize(renderer->layer);

	/* Determine the regions to present */
	if (sourceRectangle != NULL)
	{
		srcRect.x = sourceRectangle->x;
		srcRect.y = sourceRectangle->y;
		srcRect.w = sourceRectangle->w;
		srcRect.h = sourceRectangle->h;
	}
	else
	{
		srcRect.x = 0;
		srcRect.y = 0;
		srcRect.w = renderer->backbuffer->width;
		srcRect.h = renderer->backbuffer->height;
	}
	if (destinationRectangle != NULL)
	{
		dstRect.x = destinationRectangle->x;
		dstRect.y = destinationRectangle->y;
		dstRect.w = destinationRectangle->w;
		dstRect.h = destinationRectangle->h;
	}
	else
	{
		dstRect.x = 0;
		dstRect.y = 0;
		dstRect.w = (int32_t) drawableSize.width;
		dstRect.h = (int32_t) drawableSize.height;
	}

	/* Get the next drawable */
	drawable = mtlNextDrawable(renderer->layer);

	/* "Blit" the backbuffer to the drawable */
	BlitFramebuffer(
		renderer,
		renderer->currentAttachments[0],
		srcRect,
		mtlGetTextureFromDrawable(drawable),
		dstRect,
		(int32_t) drawableSize.width,
		(int32_t) drawableSize.height
	);

	/* Commit the command buffer for presentation */
	mtlPresentDrawable(renderer->commandBuffer, drawable);
	mtlAddCompletedHandler(
		renderer->commandBuffer,
		^(MTLCommandBuffer* cb) {
			SDL_SemPost(renderer->frameSemaphore);
		}
	);
	mtlCommitCommandBuffer(renderer->commandBuffer);

	/* Release allocations from the past frame */
	objc_autoreleasePoolPop(renderer->pool);

	/* Reset buffers */
	buf = renderer->buffers;
	while (buf != NULL)
	{
		buf->internalOffset = 0;
		buf->boundThisFrame = 0;
		buf->prevDataLength = 0;
		buf = buf->next;
	}
	MOJOSHADER_mtlEndFrame();

	/* We're done here. */
	renderer->frameInProgress = 0;
}

static void METAL_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;

	/* Toggling vsync is only supported on macOS 10.13+ */
	if (!RespondsToSelector(renderer->layer, selDisplaySyncEnabled))
	{
		FNA3D_LogWarn(
			"Cannot set presentation interval! "
			"Only vsync is supported."
		);
		return;
	}

	if (	presentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
		presentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		mtlSetDisplaySyncEnabled(renderer->layer, 1);
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_IMMEDIATE)
	{
		mtlSetDisplaySyncEnabled(renderer->layer, 0);
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_TWO)
	{
		/* FIXME:
		 * There is no built-in support for
		 * present-every-other-frame in Metal.
		 * We could work around this, but do
		 * any games actually use this mode...?
		 * -caleb
		 */
		mtlSetDisplaySyncEnabled(renderer->layer, 1);
	}
	else
	{
		SDL_assert(0 && "Unrecognized PresentInterval!");
	}
}

/* Drawing */

static void METAL_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	uint8_t clearTarget = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;
	uint8_t clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) == FNA3D_CLEAROPTIONS_DEPTHBUFFER;
	uint8_t clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) == FNA3D_CLEAROPTIONS_STENCIL;

	if (clearTarget)
	{
		SDL_memcpy(&renderer->clearColor, color, sizeof(FNA3D_Vec4));
		renderer->shouldClearColor = 1;
	}
	if (clearDepth)
	{
		renderer->clearDepth = depth;
		renderer->shouldClearDepth = 1;
	}
	if (clearStencil)
	{
		renderer->clearStencil = stencil;
		renderer->shouldClearStencil = 1;
	}

	renderer->needNewRenderPass |= clearTarget | clearDepth | clearStencil;
}

static void METAL_DrawInstancedPrimitives(
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
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBuffer *indexBuffer = (MetalBuffer*) indices;
	int32_t totalIndexOffset;

	indexBuffer->boundThisFrame = 1;
	totalIndexOffset = (
		(startIndex * XNAToMTL_IndexSize[indexElementSize]) +
		indexBuffer->internalOffset
	);
	mtlDrawIndexedPrimitives(
		renderer->renderCommandEncoder,
		XNAToMTL_Primitive[primitiveType],
		PrimitiveVerts(primitiveType, primitiveCount),
		XNAToMTL_IndexType[indexElementSize],
		indexBuffer->handle,
		totalIndexOffset,
		instanceCount
	);
}

static void METAL_DrawIndexedPrimitives(
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
	METAL_DrawInstancedPrimitives(
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

static void METAL_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	mtlDrawPrimitives(
		renderer->renderCommandEncoder,
		XNAToMTL_Primitive[primitiveType],
		vertexStart,
		PrimitiveVerts(primitiveType, primitiveCount)
	);
}

static void METAL_DrawUserIndexedPrimitives(
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
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	int32_t numIndices, indexSize, len;

	/* Bind the vertex buffer */
	BindUserVertexBuffer(
		renderer,
		vertexData,
		numVertices,
		vertexOffset
	);

	/* Prepare the index buffer */
	numIndices = PrimitiveVerts(primitiveType, primitiveCount);
	indexSize = XNAToMTL_IndexSize[indexElementSize];
	len = numIndices * indexSize;
	if (renderer->userIndexBuffer == NULL)
	{
		renderer->userIndexBuffer = CreateBuffer(
			driverData,
			FNA3D_BUFFERUSAGE_WRITEONLY,
			len
		);
	}
	SetUserBufferData(
		renderer,
		renderer->userIndexBuffer,
		indexOffset * indexSize,
		indexData,
		len
	);

	/* Draw! */
	mtlDrawIndexedPrimitives(
		renderer->renderCommandEncoder,
		XNAToMTL_Primitive[primitiveType],
		numIndices,
		XNAToMTL_IndexType[indexElementSize],
		renderer->userIndexBuffer->handle,
		renderer->userIndexBuffer->internalOffset,
		1
	);
}

static void METAL_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;

	/* Bind the vertex buffer */
	int32_t numVerts = PrimitiveVerts(
		primitiveType,
		primitiveCount
	);
	BindUserVertexBuffer(
		renderer,
		vertexData,
		numVerts,
		vertexOffset
	);

	/* Draw! */
	mtlDrawPrimitives(
		renderer->renderCommandEncoder,
		XNAToMTL_Primitive[primitiveType],
		0,
		numVerts
	);
}

/* Mutable Render States */

static void METAL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	FNA3D_Viewport vp = *viewport;

	if (	vp.x != renderer->viewport.x ||
		vp.y != renderer->viewport.y ||
		vp.w != renderer->viewport.w ||
		vp.h != renderer->viewport.h ||
		vp.minDepth != renderer->viewport.minDepth ||
		vp.maxDepth != renderer->viewport.maxDepth	)
	{
		renderer->viewport = vp;
		SetEncoderViewport(renderer); /* Dynamic state! */
	}
}

static void METAL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (	scissor->x != renderer->scissorRect.x ||
		scissor->y != renderer->scissorRect.y ||
		scissor->w != renderer->scissorRect.w ||
		scissor->h != renderer->scissorRect.h	)
	{
		renderer->scissorRect = *scissor;
		SetEncoderScissorRect(renderer); /* Dynamic state! */
	}
}

static void METAL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	SDL_memcpy(blendFactor, &renderer->blendColor, sizeof(FNA3D_Color));
}

static void METAL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (	renderer->blendColor.r != blendFactor->r ||
		renderer->blendColor.g != blendFactor->g ||
		renderer->blendColor.b != blendFactor->b ||
		renderer->blendColor.a != blendFactor->a	)
	{
		renderer->blendColor.r = blendFactor->r;
		renderer->blendColor.g = blendFactor->g;
		renderer->blendColor.b = blendFactor->b;
		renderer->blendColor.a = blendFactor->a;
		SetEncoderBlendColor(renderer);
	}
}

static int32_t METAL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	return renderer->multiSampleMask;
}

static void METAL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	renderer->multiSampleMask = mask;
	/* FIXME: Metal does not support multisample masks. Workarounds...? */
}

static int32_t METAL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	return renderer->stencilRef;
}

static void METAL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (renderer->stencilRef != ref)
	{
		renderer->stencilRef = ref;
		SetEncoderStencilReferenceValue(renderer);
	}
}

/* Immutable Render States */

static void METAL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	SDL_memcpy(
		&renderer->blendState,
		blendState,
		sizeof(FNA3D_BlendState)
	);
	METAL_SetBlendFactor(
		driverData,
		&blendState->blendFactor
	); /* Dynamic state! */
}

static void METAL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	SDL_memcpy(
		&renderer->depthStencilState,
		depthStencilState,
		sizeof(FNA3D_DepthStencilState)
	);
	METAL_SetReferenceStencil(
		driverData,
		depthStencilState->referenceStencil
	); /* Dynamic state! */
}

static void METAL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != renderer->scissorTestEnable)
	{
		renderer->scissorTestEnable = rasterizerState->scissorTestEnable;
		SetEncoderScissorRect(renderer); /* Dynamic state! */
	}

	if (rasterizerState->cullMode != renderer->cullFrontFace)
	{
		renderer->cullFrontFace = rasterizerState->cullMode;
		SetEncoderCullMode(renderer); /* Dynamic state! */
	}

	if (rasterizerState->fillMode != renderer->fillMode)
	{
		renderer->fillMode = rasterizerState->fillMode;
		SetEncoderFillMode(renderer); /* Dynamic state! */
	}

	realDepthBias = rasterizerState->depthBias * XNAToMTL_DepthBiasScale(
		XNAToMTL_DepthFormat(renderer, renderer->currentDepthFormat)
	);
	if (	realDepthBias != renderer->depthBias ||
		rasterizerState->slopeScaleDepthBias != renderer->slopeScaleDepthBias)
	{
		renderer->depthBias = realDepthBias;
		renderer->slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
		SetEncoderDepthBias(renderer); /* Dynamic state! */
	}

	if (rasterizerState->multiSampleAntiAlias != renderer->multiSampleEnable)
	{
		renderer->multiSampleEnable = rasterizerState->multiSampleAntiAlias;
		/* FIXME: Metal does not support toggling MSAA. Workarounds...? */
	}
}

static void METAL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	MTLSamplerState *mtlSamplerState;

	if (texture == NULL)
	{
		if (renderer->textures[index] != &NullTexture)
		{
			renderer->textures[index] = &NullTexture;
			renderer->textureNeedsUpdate[index] = 1;
		}
		if (renderer->samplers[index] == NULL)
		{
			/* Some shaders require non-null samplers
			 * even if they aren't actually used.
			 * -caleb
			 */
			renderer->samplers[index] = FetchSamplerState(
				renderer,
				sampler,
				0
			);
			renderer->samplerNeedsUpdate[index] = 1;
		}
		return;
	}

	if (	mtlTexture == renderer->textures[index] &&
		sampler->addressU == mtlTexture->wrapS &&
		sampler->addressV == mtlTexture->wrapT &&
		sampler->addressW == mtlTexture->wrapR &&
		sampler->filter == mtlTexture->filter &&
		sampler->maxAnisotropy == mtlTexture->anisotropy &&
		sampler->maxMipLevel == mtlTexture->maxMipmapLevel &&
		sampler->mipMapLevelOfDetailBias == mtlTexture->lodBias	)
	{
		/* Nothing's changing, forget it. */
		return;
	}

	/* Bind the correct texture */
	if (mtlTexture != renderer->textures[index])
	{
		renderer->textures[index] = mtlTexture;
		renderer->textureNeedsUpdate[index] = 1;
	}

	/* Update the texture sampler info */
	mtlTexture->wrapS = sampler->addressU;
	mtlTexture->wrapT = sampler->addressV;
	mtlTexture->wrapR = sampler->addressW;
	mtlTexture->filter = sampler->filter;
	mtlTexture->anisotropy = sampler->maxAnisotropy;
	mtlTexture->maxMipmapLevel = sampler->maxMipLevel;
	mtlTexture->lodBias = sampler->mipMapLevelOfDetailBias;

	/* Update the sampler state, if needed */
	mtlSamplerState = FetchSamplerState(
		renderer,
		sampler,
		mtlTexture->hasMipmaps
	);
	if (mtlSamplerState != renderer->samplers[index])
	{
		renderer->samplers[index] = mtlSamplerState;
		renderer->samplerNeedsUpdate[index] = 1;
	}
}

/* Vertex State */

static void BindResources(MetalRenderer *renderer)
{
	int32_t i;
	MTLBuffer *vUniform, *fUniform;
	int32_t vOff, fOff;
	MTLDepthStencilState *depthStencilState;
	MTLRenderPipelineState *pipelineState;

	/* Bind textures and their sampler states */
	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		if (renderer->textureNeedsUpdate[i])
		{
			mtlSetFragmentTexture(
				renderer->renderCommandEncoder,
				renderer->textures[i]->handle,
				i
			);
			renderer->textureNeedsUpdate[i] = 0;
		}
		if (renderer->samplerNeedsUpdate[i])
		{
			mtlSetFragmentSamplerState(
				renderer->renderCommandEncoder,
				renderer->samplers[i],
				i
			);
			renderer->samplerNeedsUpdate[i] = 0;
		}
	}

	/* In MojoShader output, the uniform register is always 16 */
	#define UNIFORM_REG 16

	/* Bind the uniform buffers */
	vUniform = renderer->currentShaderState.vertexUniformBuffer;
	vOff = renderer->currentShaderState.vertexUniformOffset;
	if (vUniform != renderer->ldVertUniformBuffer)
	{
		mtlSetVertexBuffer(
			renderer->renderCommandEncoder,
			vUniform,
			vOff,
			UNIFORM_REG
		);
		renderer->ldVertUniformBuffer = vUniform;
		renderer->ldVertUniformOffset = vOff;
	}
	else if (vOff != renderer->ldVertUniformOffset)
	{
		mtlSetVertexBufferOffset(
			renderer->renderCommandEncoder,
			vOff,
			UNIFORM_REG
		);
		renderer->ldVertUniformOffset = vOff;
	}

	fUniform = renderer->currentShaderState.fragmentUniformBuffer;
	fOff = renderer->currentShaderState.fragmentUniformOffset;
	if (fUniform != renderer->ldFragUniformBuffer)
	{
		mtlSetFragmentBuffer(
			renderer->renderCommandEncoder,
			fUniform,
			fOff,
			UNIFORM_REG
		);
		renderer->ldFragUniformBuffer = fUniform;
		renderer->ldFragUniformOffset = fOff;
	}
	else if (fOff != renderer->ldFragUniformOffset)
	{
		mtlSetFragmentBufferOffset(
			renderer->renderCommandEncoder,
			fOff,
			UNIFORM_REG
		);
		renderer->ldFragUniformOffset = fOff;
	}

	#undef UNIFORM_REG

	/* Bind the depth-stencil state */
	depthStencilState = FetchDepthStencilState(renderer);
	if (depthStencilState != renderer->ldDepthStencilState)
	{
		mtlSetDepthStencilState(
			renderer->renderCommandEncoder,
			depthStencilState
		);
		renderer->ldDepthStencilState = depthStencilState;
	}

	/* Finally, bind the pipeline state */
	pipelineState = FetchRenderPipeline(renderer);
	if (pipelineState != renderer->ldPipelineState)
	{
		mtlSetRenderPipelineState(
			renderer->renderCommandEncoder,
			pipelineState
		);
		renderer->ldPipelineState = pipelineState;
	}
}

static void METAL_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBuffer *vertexBuffer;
	int32_t i, offset;

	/* Translate the bindings array into a descriptor */
	renderer->currentVertexDescriptor = FetchVertexBufferBindingsDescriptor(
		renderer,
		bindings,
		numBindings
	);

	/* Prepare for rendering */
	UpdateRenderPass(renderer);
	BindResources(renderer);

	/* Bind the vertex buffers */
	for (i = 0; i < numBindings; i += 1)
	{
		vertexBuffer = (MetalBuffer*) bindings[i].vertexBuffer;
		if (vertexBuffer == NULL)
		{
			continue;
		}

		offset = vertexBuffer->internalOffset + (
			(bindings[i].vertexOffset + baseVertex) *
			bindings[i].vertexDeclaration.vertexStride
		);

		vertexBuffer->boundThisFrame = 1;
		if (renderer->ldVertexBuffers[i] != vertexBuffer->handle)
		{
			mtlSetVertexBuffer(
				renderer->renderCommandEncoder,
				vertexBuffer->handle,
				offset,
				i
			);
			renderer->ldVertexBuffers[i] = vertexBuffer->handle;
			renderer->ldVertexBufferOffsets[i] = offset;
		}
		else if (renderer->ldVertexBufferOffsets[i] != offset)
		{
			mtlSetVertexBufferOffset(
				renderer->renderCommandEncoder,
				offset,
				i
			);
			renderer->ldVertexBufferOffsets[i] = offset;
		}
	}
}

static void METAL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;

	/* Translate the declaration into a descriptor */
	renderer->currentVertexDescriptor = FetchVertexDeclarationDescriptor(
		renderer,
		vertexDeclaration,
		vertexOffset
	);
	renderer->userVertexStride = vertexDeclaration->vertexStride;

	/* Prepare for rendering */
	UpdateRenderPass(renderer);
	BindResources(renderer);

	/* The rest happens in DrawUser[Indexed]Primitives. */
}

/* Render Targets */

static void METAL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalBackbuffer *bb;
	MetalRenderbuffer *rb;
	MetalTexture *tex;
	int32_t i;

	/* Perform any pending clears before switching render targets */
	if (	renderer->shouldClearColor ||
		renderer->shouldClearDepth ||
		renderer->shouldClearStencil	)
	{
		UpdateRenderPass(renderer);
	}

	/* Force an update to the render pass */
	renderer->needNewRenderPass = 1;

	/* Reset attachments */
	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		renderer->currentAttachments[i] = NULL;
		renderer->currentColorFormats[i] = MTLPixelFormatInvalid;
		renderer->currentMSAttachments[i] = NULL;
		renderer->currentAttachmentSlices[i] = 0;
	}
	renderer->currentDepthStencilBuffer = NULL;
	renderer->currentDepthFormat = FNA3D_DEPTHFORMAT_NONE;
	renderer->currentSampleCount = 0;

	/* Bind the backbuffer, if applicable */
	if (renderTargets == NULL)
	{
		bb = renderer->backbuffer;
		renderer->currentAttachments[0] = bb->colorBuffer;
		renderer->currentColorFormats[0] = XNAToMTL_TextureFormat[
			bb->surfaceFormat
		];
		renderer->currentDepthStencilBuffer = bb->depthStencilBuffer;
		renderer->currentDepthFormat = bb->depthFormat;
		renderer->currentSampleCount = bb->multiSampleCount;
		renderer->currentMSAttachments[0] = bb->multiSampleColorBuffer;
		renderer->currentAttachmentSlices[0] = 0;
		return;
	}

	/* Update color buffers */
	for (i = 0; i < numRenderTargets; i += 1)
	{
		renderer->currentAttachmentSlices[i] = renderTargets[i].cubeMapFace;
		if (renderTargets[i].colorBuffer != NULL)
		{
			rb = (MetalRenderbuffer*) renderTargets[i].colorBuffer;
			renderer->currentAttachments[i] = rb->handle;
			renderer->currentColorFormats[i] = rb->pixelFormat;
			renderer->currentSampleCount = rb->multiSampleCount;
			renderer->currentMSAttachments[i] = rb->multiSampleHandle;
		}
		else
		{
			tex = (MetalTexture*) renderTargets[i].texture;
			renderer->currentAttachments[i] = tex->handle;
			renderer->currentColorFormats[i] = XNAToMTL_TextureFormat[
				tex->format
			];
			renderer->currentSampleCount = 0;
		}
	}

	/* Update depth stencil buffer */
	renderer->currentDepthStencilBuffer = (
		renderbuffer == NULL ?
			NULL :
			((MetalRenderbuffer*) renderbuffer)->handle
	);
	renderer->currentDepthFormat = (
		renderbuffer == NULL ?
			FNA3D_DEPTHFORMAT_NONE :
			depthFormat
	);
}

static void METAL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *texture = (MetalTexture*) target->texture;
	MTLBlitCommandEncoder *blit;

	/* The target is resolved at the end of each render pass. */

	/* If the target has mipmaps, regenerate them now. */
	if (target->levelCount > 1)
	{
		blit = mtlMakeBlitCommandEncoder(renderer->commandBuffer);
		mtlGenerateMipmapsForTexture(
			blit,
			texture->handle
		);
		mtlEndEncoding(blit);

		renderer->needNewRenderPass = 1;
	}
}

/* Backbuffer Functions */

static void CreateFramebuffer(
	MetalRenderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
	int32_t newWidth, newHeight;
	MTLTextureDescriptor *colorBufferDesc;
	MTLTextureDescriptor *depthStencilBufferDesc;

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
	BB->multiSampleCount = GetCompatibleSampleCount(
		renderer,
		presentationParameters->multiSampleCount
	);

	/* Update color buffer to the new resolution */
	colorBufferDesc = mtlMakeTexture2DDescriptor(
		XNAToMTL_TextureFormat[BB->surfaceFormat],
		BB->width,
		BB->height,
		0
	);
	mtlSetStorageMode(colorBufferDesc, MTLStorageModePrivate);
	mtlSetTextureUsage(
		colorBufferDesc,
		MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead
	);
	BB->colorBuffer = mtlNewTexture(renderer->device, colorBufferDesc);
	if (BB->multiSampleCount > 0)
	{
		mtlSetTextureType(colorBufferDesc, MTLTextureType2DMultisample);
		mtlSetTextureSampleCount(colorBufferDesc, BB->multiSampleCount);
		mtlSetTextureUsage(colorBufferDesc, MTLTextureUsageRenderTarget);
		BB->multiSampleColorBuffer = mtlNewTexture(
			renderer->device,
			colorBufferDesc
		);
	}

	/* Update the depth/stencil buffer, if applicable */
	if (BB->depthFormat != FNA3D_DEPTHFORMAT_NONE)
	{
		depthStencilBufferDesc = mtlMakeTexture2DDescriptor(
			XNAToMTL_DepthFormat(renderer, BB->depthFormat),
			BB->width,
			BB->height,
			0
		);
		mtlSetStorageMode(depthStencilBufferDesc, MTLStorageModePrivate);
		mtlSetTextureUsage(depthStencilBufferDesc, MTLTextureUsageRenderTarget);
		if (BB->multiSampleCount > 0)
		{
			mtlSetTextureType(
				depthStencilBufferDesc,
				MTLTextureType2DMultisample
			);
			mtlSetTextureSampleCount(
				depthStencilBufferDesc,
				BB->multiSampleCount
			);
		}
		BB->depthStencilBuffer = mtlNewTexture(
			renderer->device,
			depthStencilBufferDesc
		);
	}

	#undef BB

	/* This is the default render target */
	METAL_SetRenderTargets(
		(FNA3D_Renderer*) renderer,
		NULL,
		0,
		NULL,
		FNA3D_DEPTHFORMAT_NONE
	);
}

static void DestroyFramebuffer(MetalRenderer *renderer)
{
	objc_release(renderer->backbuffer->colorBuffer);
	renderer->backbuffer->colorBuffer = NULL;

	objc_release(renderer->backbuffer->multiSampleColorBuffer);
	renderer->backbuffer->multiSampleColorBuffer = NULL;

	objc_release(renderer->backbuffer->depthStencilBuffer);
	renderer->backbuffer->depthStencilBuffer = NULL;
}

static void METAL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	DestroyFramebuffer(renderer);
	CreateFramebuffer(
		renderer,
		presentationParameters
	);
}

static void METAL_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t textureWidth,
	int32_t textureHeight,
	int32_t level,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
);
static void METAL_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	void* data,
	int32_t dataLen,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture backbufferTexture;

	/* FIXME: Right now we're expecting one of the following:
	 * - byte[]
	 * - int[]
	 * - uint[]
	 * - Color[]
	 * Anything else will freak out because we're using
	 * color backbuffers. Maybe check this out when adding
	 * support for more backbuffer types!
	 * -flibit
	 */

	if (startIndex > 0 || elementCount != (dataLen / elementSizeInBytes))
	{
		FNA3D_LogError(
			"ReadBackbuffer startIndex/elementCount combination unimplemented!"
		);
		return;
	}

	/* Create a pseudo-texture we can feed to GetTextureData2D.
	 * These are the only members we need to initialize.
	 * -caleb
	 */
	backbufferTexture.width = renderer->backbuffer->width;
	backbufferTexture.height = renderer->backbuffer->height;
	backbufferTexture.format = renderer->backbuffer->surfaceFormat;
	backbufferTexture.hasMipmaps = 0;
	backbufferTexture.isPrivate = 1;

	METAL_GetTextureData2D(
		driverData,
		(FNA3D_Texture*) &backbufferTexture,
		renderer->backbuffer->surfaceFormat,
		renderer->backbuffer->width,
		renderer->backbuffer->height,
		0,
		x,
		y,
		w,
		h,
		data,
		0,
		dataLen,
		1
	);
}

static void METAL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	*w = renderer->backbuffer->width;
	*h = renderer->backbuffer->height;
}

static FNA3D_SurfaceFormat METAL_GetBackbufferSurfaceFormat(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	return renderer->backbuffer->surfaceFormat;
}

static FNA3D_DepthFormat METAL_GetBackbufferDepthFormat(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	return renderer->backbuffer->depthFormat;
}

static int32_t METAL_GetBackbufferMultiSampleCount(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	return renderer->backbuffer->multiSampleCount;
}

/* Textures */

static FNA3D_Texture* METAL_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MTLTextureDescriptor *desc = mtlMakeTexture2DDescriptor(
		XNAToMTL_TextureFormat[format],
		width,
		height,
		levelCount > 1
	);

	if (isRenderTarget)
	{
		mtlSetStorageMode(desc, MTLStorageModePrivate);
		mtlSetTextureUsage(
			desc,
			MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead
		);
	}

	return (FNA3D_Texture*) CreateTexture(
		renderer,
		mtlNewTexture(renderer->device, desc),
		format,
		width,
		height,
		levelCount,
		isRenderTarget
	);
}

static FNA3D_Texture* METAL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MTLTextureDescriptor *desc = mtlMakeTexture2DDescriptor(
		XNAToMTL_TextureFormat[format],
		width,
		height,
		levelCount > 1
	);

	/* Make it 3D! */
	mtlSetTextureDepth(desc, depth);
	mtlSetTextureType(desc, MTLTextureType3DTexture);

	return (FNA3D_Texture*) CreateTexture(
		renderer,
		mtlNewTexture(renderer->device, desc),
		format,
		width,
		height,
		levelCount,
		0
	);
}

static FNA3D_Texture* METAL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MTLTextureDescriptor *desc = mtlMakeTextureCubeDescriptor(
		XNAToMTL_TextureFormat[format],
		size,
		levelCount > 1
	);

	if (isRenderTarget)
	{
		mtlSetStorageMode(desc, MTLStorageModePrivate);
		mtlSetTextureUsage(
			desc,
			MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead
		);
	}

	return (FNA3D_Texture*) CreateTexture(
		renderer,
		mtlNewTexture(renderer->device, desc),
		format,
		size,
		size,
		levelCount,
		isRenderTarget
	);
}

static void METAL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	int32_t i;

	for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
	{
		if (mtlTexture->handle == renderer->currentAttachments[i])
		{
			renderer->currentAttachments[i] = NULL;
		}
	}
	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		if (mtlTexture->handle == renderer->textures[i]->handle)
		{
			renderer->textures[i] = &NullTexture;
			renderer->textureNeedsUpdate[i] = 1;
		}
	}

	objc_release(mtlTexture->handle);
	mtlTexture->handle = NULL;

	SDL_free(mtlTexture);
}

static void METAL_SetTextureData2D(
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
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	MTLTexture *handle = mtlTexture->handle;
	MTLBlitCommandEncoder *blit;

	MTLOrigin origin = {x, y, 0};
	MTLSize size = {w, h, 1};
	MTLRegion region = {origin, size};

	if (mtlTexture->isPrivate)
	{
		/* We need an active command buffer */
		METAL_BeginFrame(driverData);

		/* Fetch a CPU-accessible texture */
		handle = FetchTransientTexture(renderer, mtlTexture);
	}

	/* Write the data */
	mtlReplaceRegion(
		handle,
		region,
		level,
		0,
		data,
		BytesPerRow(w, format),
		0
	);

	/* Blit the temp texture to the actual texture */
	if (mtlTexture->isPrivate)
	{
		/* End the render pass */
		EndPass(renderer);

		/* Blit! */
		blit = mtlMakeBlitCommandEncoder(renderer->commandBuffer);
		mtlBlitTextureToTexture(
			blit,
			handle,
			0,
			level,
			origin,
			size,
			mtlTexture->handle,
			0,
			level,
			origin
		);

		/* Submit the blit command to the GPU and wait... */
		mtlEndEncoding(blit);
		Stall(renderer);

		/* We're done with the temp texture */
		mtlSetPurgeableState(
			handle,
			MTLPurgeableStateEmpty
		);
	}
}

static void METAL_SetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t level,
	int32_t left,
	int32_t top,
	int32_t right,
	int32_t bottom,
	int32_t front,
	int32_t back,
	void* data,
	int32_t dataLength
) {
	int32_t w = right - left;
	int32_t h = bottom - top;
	int32_t d = back - front;

	MTLOrigin origin = {left, top, front};
	MTLSize size = {w, h, d};
	MTLRegion region = {origin, size};

	mtlReplaceRegion(
		((MetalTexture*) texture)->handle,
		region,
		level,
		0,
		data,
		BytesPerRow(w, format),
		BytesPerImage(w, h, format)
	);
}

static void METAL_SetTextureDataCube(
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
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	MTLTexture *handle = mtlTexture->handle;
	MTLBlitCommandEncoder *blit;

	MTLOrigin origin = {x, y, 0};
	MTLSize size = {w, h, 1};
	MTLRegion region = {origin, size};
	int32_t slice = cubeMapFace;

	if (mtlTexture->isPrivate)
	{
		/* We need an active command buffer */
		METAL_BeginFrame(driverData);

		/* Fetch a CPU-accessible texture */
		handle = FetchTransientTexture(renderer, mtlTexture);

		/* Transient textures have no slices */
		slice = 0;
	}

	/* Write the data */
	mtlReplaceRegion(
		handle,
		region,
		level,
		slice,
		data,
		BytesPerRow(w, format),
		0
	);

	/* Blit the temp texture to the actual texture */
	if (mtlTexture->isPrivate)
	{
		/* End the render pass */
		EndPass(renderer);

		/* Blit! */
		blit = mtlMakeBlitCommandEncoder(renderer->commandBuffer);
		mtlBlitTextureToTexture(
			blit,
			handle,
			slice,
			level,
			origin,
			size,
			mtlTexture->handle,
			cubeMapFace,
			level,
			origin
		);

		/* Submit the blit command to the GPU and wait... */
		mtlEndEncoding(blit);
		Stall(renderer);

		/* We're done with the temp texture */
		mtlSetPurgeableState(
			handle,
			MTLPurgeableStateEmpty
		);
	}
}

static void METAL_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
	uint8_t* dataPtr = (uint8_t*) ptr;
	MTLOrigin origin = {0, 0, 0};
	MTLSize sizeY = {w, h, 1};
	MTLSize sizeUV = {w / 2, h / 2, 1};
	MTLRegion regionY = {origin, sizeY};
	MTLRegion regionUV = {origin, sizeUV};

	mtlReplaceRegion(
		((MetalTexture*) y)->handle,
		regionY,
		0,
		0,
		dataPtr,
		w,
		0
	);
	dataPtr += w * h;

	mtlReplaceRegion(
		((MetalTexture*) u)->handle,
		regionUV,
		0,
		0,
		dataPtr,
		w / 2,
		0
	);
	dataPtr += (w / 2) * (h / 2);

	mtlReplaceRegion(
		((MetalTexture*) v)->handle,
		regionUV,
		0,
		0,
		dataPtr,
		w / 2,
		0
	);
}

static void METAL_GetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t textureWidth,
	int32_t textureHeight,
	int32_t level,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	MTLTexture *handle = mtlTexture->handle;
	MTLBlitCommandEncoder *blit;

	MTLOrigin origin = {x, y, 0};
	MTLSize size = {w, h, 1};
	MTLRegion region = {origin, size};

	if (mtlTexture->isPrivate)
	{
		/* We need an active command buffer */
		METAL_BeginFrame(driverData);

		/* Fetch a CPU-accessible texture */
		handle = FetchTransientTexture(renderer, mtlTexture);

		/* End the render pass */
		EndPass(renderer);

		/* Blit the actual texture to a CPU-accessible texture */
		blit = mtlMakeBlitCommandEncoder(renderer->commandBuffer);
		mtlBlitTextureToTexture(
			blit,
			mtlTexture->handle,
			0,
			level,
			origin,
			size,
			handle,
			0,
			level,
			origin
		);

		/* Managed resources require explicit synchronization */
		if (renderer->isMac)
		{
			mtlSynchronizeResource(blit, handle);
		}

		/* Submit the blit command to the GPU and wait... */
		mtlEndEncoding(blit);
		Stall(renderer);
	}

	mtlGetTextureBytes(
		handle,
		data,
		BytesPerRow(w, format),
		0,
		region,
		level,
		0
	);

	if (mtlTexture->isPrivate)
	{
		/* We're done with the temp texture */
		mtlSetPurgeableState(
			handle,
			MTLPurgeableStateEmpty
		);
	}
}

static void METAL_GetTextureData3D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t left,
	int32_t top,
	int32_t front,
	int32_t right,
	int32_t bottom,
	int32_t back,
	int32_t level,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	int32_t w = right - left;
	int32_t h = bottom - top;
	int32_t d = back - front;

	MTLOrigin origin = {left, top, right};
	MTLSize size = {w, h, d};
	MTLRegion region = {origin, size};

	mtlGetTextureBytes(
		((MetalTexture*) texture)->handle,
		data,
		BytesPerRow(w, format),
		BytesPerImage(w, h, format),
		region,
		level,
		0
	);
}

static void METAL_GetTextureDataCube(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t textureSize,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalTexture *mtlTexture = (MetalTexture*) texture;
	MTLTexture *handle = mtlTexture->handle;
	MTLBlitCommandEncoder *blit;

	MTLOrigin origin = {x, y, 0};
	MTLSize size = {w, h, 1};
	MTLRegion region = {origin, size};
	int32_t slice = cubeMapFace;

	if (mtlTexture->isPrivate)
	{
		/* We need an active command buffer */
		METAL_BeginFrame(driverData);

		/* Fetch a CPU-accessible texture */
		handle = FetchTransientTexture(renderer, mtlTexture);

		/* Transient textures have no slices */
		slice = 0;

		/* End the render pass */
		EndPass(renderer);

		/* Blit the actual texture to a CPU-accessible texture */
		blit = mtlMakeBlitCommandEncoder(renderer->commandBuffer);
		mtlBlitTextureToTexture(
			blit,
			mtlTexture->handle,
			cubeMapFace,
			level,
			origin,
			size,
			handle,
			slice,
			level,
			origin
		);

		/* Managed resources require explicit synchronization */
		if (renderer->isMac)
		{
			mtlSynchronizeResource(blit, handle);
		}

		/* Submit the blit command to the GPU and wait... */
		mtlEndEncoding(blit);
		Stall(renderer);
	}

	mtlGetTextureBytes(
		handle,
		data,
		BytesPerRow(w, format),
		0,
		region,
		level,
		0
	);

	if (mtlTexture->isPrivate)
	{
		/* We're done with the temp texture */
		mtlSetPurgeableState(
			handle,
			MTLPurgeableStateEmpty
		);
	}
}

/* Renderbuffers */

static FNA3D_Renderbuffer* METAL_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MTLPixelFormat pixelFormat = XNAToMTL_TextureFormat[format];
	int32_t sampleCount = GetCompatibleSampleCount(
		renderer,
		multiSampleCount
	);
	MTLTextureDescriptor *desc;
	MTLTexture *multiSampleTexture;
	MetalRenderbuffer *result;

	/* Generate a multisample texture */
	desc = mtlMakeTexture2DDescriptor(
		pixelFormat,
		width,
		height,
		0
	);
	mtlSetStorageMode(desc, MTLStorageModePrivate);
	mtlSetTextureUsage(desc, MTLTextureUsageRenderTarget);
	mtlSetTextureType(desc, MTLTextureType2DMultisample);
	mtlSetTextureSampleCount(desc, sampleCount);
	multiSampleTexture = mtlNewTexture(
		renderer->device,
		desc
	);

	/* Create and return the renderbuffer */
	result = SDL_malloc(sizeof(MetalRenderbuffer));
	result->handle = ((MetalTexture*) texture)->handle;
	result->pixelFormat = pixelFormat;
	result->multiSampleCount = sampleCount;
	result->multiSampleHandle = multiSampleTexture;
	return (FNA3D_Renderbuffer*) result;
}

static FNA3D_Renderbuffer* METAL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MTLPixelFormat pixelFormat = XNAToMTL_DepthFormat(renderer, format);
	int32_t sampleCount = GetCompatibleSampleCount(
		renderer,
		multiSampleCount
	);
	MTLTextureDescriptor *desc;
	MTLTexture *depthTexture;
	MetalRenderbuffer *result;

	/* Generate a depth texture */
	desc = mtlMakeTexture2DDescriptor(
		pixelFormat,
		width,
		height,
		0
	);
	mtlSetStorageMode(desc, MTLStorageModePrivate);
	mtlSetTextureUsage(desc, MTLTextureUsageRenderTarget);
	if (multiSampleCount > 0)
	{
		mtlSetTextureType(desc, MTLTextureType2DMultisample);
		mtlSetTextureSampleCount(desc, sampleCount);
	}
	depthTexture = mtlNewTexture(
		renderer->device,
		desc
	);

	/* Create and return the renderbuffer */
	result = SDL_malloc(sizeof(MetalRenderbuffer));
	result->handle = depthTexture;
	result->pixelFormat = pixelFormat;
	result->multiSampleCount = sampleCount;
	result->multiSampleHandle = NULL;
	return (FNA3D_Renderbuffer*) result;
}

static void METAL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalRenderbuffer *mtlRenderbuffer = (MetalRenderbuffer*) renderbuffer;
	uint8_t isDepthStencil = (mtlRenderbuffer->multiSampleHandle == NULL);
	int32_t i;

	if (isDepthStencil)
	{
		if (mtlRenderbuffer->handle == renderer->currentDepthStencilBuffer)
		{
			renderer->currentDepthStencilBuffer = NULL;
		}
		objc_release(mtlRenderbuffer->handle);
		mtlRenderbuffer->handle = NULL;
	}
	else
	{
		for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
		{
			if (mtlRenderbuffer->multiSampleHandle == renderer->currentMSAttachments[i])
			{
				renderer->currentMSAttachments[i] = NULL;
			}
		}
		objc_release(mtlRenderbuffer->multiSampleHandle);
		mtlRenderbuffer->multiSampleHandle = NULL;

		/* Don't release the regular handle since
		 * it's owned by the associated FNA3D_Texture.
		 */
	}
	SDL_free(mtlRenderbuffer);
}

/* Vertex Buffers */

static FNA3D_Buffer* METAL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	/* Note that dynamic is NOT used! */
	return (FNA3D_Buffer*) CreateBuffer(
		driverData,
		usage,
		vertexCount * vertexStride
	);
}

static void METAL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	DestroyBuffer(driverData, buffer);
}

static void METAL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		dataLength,
		options
	);
}

static void METAL_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	MetalBuffer *mtlBuffer = (MetalBuffer*) buffer;
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useStagingBuffer;
	int32_t i;

	dataBytes = (uint8_t*) data;
	useStagingBuffer = elementSizeInBytes < vertexStride;
	if (useStagingBuffer)
	{
		cpy = (uint8_t*) SDL_malloc(elementCount * vertexStride);
	}
	else
	{
		cpy = dataBytes + (startIndex * elementSizeInBytes);
	}

	SDL_memcpy(
		cpy,
		(uint8_t*) mtlBuffer->contents + offsetInBytes,
		elementCount * vertexStride
	);

	if (useStagingBuffer)
	{
		src = cpy;
		dst = dataBytes + (startIndex * elementSizeInBytes);
		for (i = 0; i < elementCount; i += 1)
		{
			SDL_memcpy(dst, src, elementSizeInBytes);
			dst += elementSizeInBytes;
			src += vertexStride;
		}
		SDL_free(cpy);
	}
}

/* Index Buffers */

static FNA3D_Buffer* METAL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	/* Note that dynamic is NOT used! */
	return (FNA3D_Buffer*) CreateBuffer(
		driverData,
		usage,
		indexCount * XNAToMTL_IndexSize[indexElementSize]
	);
}

static void METAL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	DestroyBuffer(driverData, buffer);
}

static void METAL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	SetBufferData(
		driverData,
		buffer,
		offsetInBytes,
		data,
		dataLength,
		options
	);
}

static void METAL_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	MetalBuffer *mtlBuffer = (MetalBuffer*) buffer;
	uint8_t *dataPtr = (uint8_t*) data;
	uint8_t *contentsPtr = (uint8_t*) mtlBuffer->contents;
	SDL_memcpy(
		dataPtr + (startIndex * elementSizeInBytes),
		contentsPtr + offsetInBytes,
		elementCount * elementSizeInBytes
	);
}

/* Effects */

static FNA3D_Effect* METAL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MOJOSHADER_effect *effect;
	MOJOSHADER_mtlEffect *mtlEffect;
	MetalEffect *result;
	int32_t i;

	effect = MOJOSHADER_parseEffect(
		"metal",
		effectCode,
		effectCodeLength,
		NULL,
		0,
		NULL,
		0,
		NULL,
		NULL,
		NULL
	);

	for (i = 0; i < effect->error_count; i += 1)
	{
		FNA3D_LogError(
			"MOJOSHADER_parseEffect Error: %s",
			effect->errors[i].error
		);
	}

	mtlEffect = MOJOSHADER_mtlCompileEffect(
		effect,
		renderer->device,
		renderer->maxFramesInFlight
	);
	if (mtlEffect == NULL)
	{
		FNA3D_LogError(
			"%s", MOJOSHADER_mtlGetError()
		);
	}

	result = (MetalEffect*) SDL_malloc(sizeof(MetalEffect));
	result->effect = effect;
	result->mtlEffect = mtlEffect;

	return (FNA3D_Effect*) result;
}

static FNA3D_Effect* METAL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalEffect *cloneSource = (MetalEffect*) effect;
	MOJOSHADER_effect *effectData;
	MOJOSHADER_mtlEffect *mtlEffect;
	MetalEffect *result;

	effectData = MOJOSHADER_cloneEffect(cloneSource->effect);
	mtlEffect = MOJOSHADER_mtlCompileEffect(
		effectData,
		renderer->device,
		renderer->maxFramesInFlight
	);
	if (mtlEffect == NULL)
	{
		FNA3D_LogError(
			"%s", MOJOSHADER_mtlGetError()
		);
		SDL_assert(0);
	}

	result = (MetalEffect*) SDL_malloc(sizeof(MetalEffect));
	result->effect = effectData;
	result->mtlEffect = mtlEffect;

	return (FNA3D_Effect*) result;
}

static void METAL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalEffect *mtlEffect = (MetalEffect*) effect;
	if (mtlEffect->mtlEffect == renderer->currentEffect)
	{
		MOJOSHADER_mtlEffectEndPass(renderer->currentEffect);
		MOJOSHADER_mtlEffectEnd(
			renderer->currentEffect,
			&renderer->currentShaderState
		);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;

		/* FIXME: Is this right? -caleb */
		SDL_memset(
			&renderer->currentShaderState,
			'\0',
			sizeof(MOJOSHADER_mtlShaderState)
		);
	}
	MOJOSHADER_mtlDeleteEffect(mtlEffect->mtlEffect);
	MOJOSHADER_freeEffect(mtlEffect->effect);
	SDL_free(effect);
}

static void METAL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MOJOSHADER_mtlEffect *mtlEffectData;
	uint32_t whatever;

	/* If a frame isn't already in progress,
	 * wait until one begins to avoid overwriting
	 * the previous frame's uniform buffers.
	 */
	METAL_BeginFrame(driverData);

	mtlEffectData = ((MetalEffect*) effect)->mtlEffect;
	if (mtlEffectData == renderer->currentEffect)
	{
		if (	technique == renderer->currentTechnique &&
			pass == renderer->currentPass			)
		{
			MOJOSHADER_mtlEffectCommitChanges(
				renderer->currentEffect,
				&renderer->currentShaderState
			);
			return;
		}
		MOJOSHADER_mtlEffectEndPass(renderer->currentEffect);
		MOJOSHADER_mtlEffectBeginPass(
			renderer->currentEffect,
			pass,
			&renderer->currentShaderState
		);
		renderer->currentTechnique = technique;
		renderer->currentPass = pass;
		return;
	}
	else if (renderer->currentEffect != NULL)
	{
		MOJOSHADER_mtlEffectEndPass(renderer->currentEffect);
		MOJOSHADER_mtlEffectEnd(
			renderer->currentEffect,
			&renderer->currentShaderState
		);
	}
	MOJOSHADER_mtlEffectBegin(
		mtlEffectData,
		&whatever,
		0,
		stateChanges
	);
	MOJOSHADER_mtlEffectBeginPass(
		mtlEffectData,
		pass,
		&renderer->currentShaderState
	);
	renderer->currentEffect = mtlEffectData;
	renderer->currentTechnique = technique;
	renderer->currentPass = pass;
}

static void METAL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MOJOSHADER_mtlEffect *mtlEffectData;
	uint32_t whatever;

	/* If a frame isn't already in progress,
	 * wait until one begins to avoid overwriting
	 * the previous frame's uniform buffers.
	 */
	METAL_BeginFrame(driverData);

	/* Store the current data */
	renderer->prevEffect = renderer->currentEffect;
	renderer->prevShaderState = renderer->currentShaderState;

	mtlEffectData = ((MetalEffect*) effect)->mtlEffect;
	MOJOSHADER_mtlEffectBegin(
		mtlEffectData,
		&whatever,
		1,
		stateChanges
	);
	MOJOSHADER_mtlEffectBeginPass(
		mtlEffectData,
		0,
		&renderer->currentShaderState
	);
	renderer->currentEffect = mtlEffectData;
}

static void METAL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalEffect *mtlEffect = (MetalEffect*) effect;
	MOJOSHADER_mtlEffectEndPass(mtlEffect->mtlEffect);
	MOJOSHADER_mtlEffectEnd(
		mtlEffect->mtlEffect,
		&renderer->currentShaderState
	);

	/* Restore the old data */
	renderer->currentShaderState = renderer->prevShaderState;
	renderer->currentEffect = renderer->prevEffect;
}

/* Queries */

static FNA3D_Query* METAL_CreateQuery(FNA3D_Renderer *driverData)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalQuery *result;
	SDL_assert(renderer->supportsOcclusionQueries);

	result = (MetalQuery*) SDL_malloc(sizeof(MetalQuery));
	result->handle = mtlNewBuffer(
		renderer->device,
		sizeof(uint64_t),
		0
	);
	return (FNA3D_Query*) result;
}

static void METAL_AddDisposeQuery(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	MetalQuery *mtlQuery = (MetalQuery*) query;
	objc_release(mtlQuery->handle);
	mtlQuery->handle = NULL;
	SDL_free(mtlQuery);
}

static void METAL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	MetalQuery *mtlQuery = (MetalQuery*) query;

	/* Stop the current pass */
	EndPass(renderer);

	/* Attach the visibility buffer to a new render pass */
	renderer->currentVisibilityBuffer = mtlQuery->handle;
	renderer->needNewRenderPass = 1;
}

static void METAL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (renderer->renderCommandEncoder != NULL)
	{
		/* Stop counting */
		mtlSetVisibilityResultMode(
			renderer->renderCommandEncoder,
			MTLVisibilityResultModeDisabled,
			0
		);
	}
	renderer->currentVisibilityBuffer = NULL;
}

static uint8_t METAL_QueryComplete(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	/* FIXME:
	 * There's no easy way to check for completion
	 * of the query. The only accurate way would be
	 * to monitor the completion of the command buffer
	 * associated with each query, but that gets tricky
	 * since in the event of a stalled buffer overwrite or
	 * something similar, a new command buffer would be
	 * created, likely screwing up the visibility test.
	 *
	 * The below code is obviously wrong, but it happens
	 * to work for the Lens Flare XNA sample. Maybe it'll
	 * work for your game too?
	 *
	 * (Although if you're making a new game with FNA,
	 * you really shouldn't be using queries anyway...)
	 *
	 * -caleb
	 */
	return 1;
}

static int32_t METAL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	MetalQuery *mtlQuery = (MetalQuery*) query;
	void* contents = mtlGetBufferContents(mtlQuery->handle);
	return (int32_t) (*((uint64_t*) contents));
}

/* Feature Queries */

static uint8_t METAL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	return ((MetalRenderer*) driverData)->supportsDxt1;
}

static uint8_t METAL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	return ((MetalRenderer*) driverData)->supportsS3tc;
}

static uint8_t METAL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t METAL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

static int32_t METAL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	return 16;
}

static int32_t METAL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	return ((MetalRenderer*) driverData)->maxMultiSampleCount;
}

/* Debugging */

static void METAL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	MetalRenderer *renderer = (MetalRenderer*) driverData;
	if (renderer->renderCommandEncoder != NULL)
	{
		mtlInsertDebugSignpost(renderer->renderCommandEncoder, text);
	}
}

/* Buffer Objects */

static intptr_t METAL_GetBufferSize(FNA3D_Buffer *buffer)
{
	MetalBuffer *mtlBuffer = (MetalBuffer*) buffer;
	return mtlBuffer->size;
}

/* Effect Objects */

static MOJOSHADER_effect* METAL_GetEffectData(FNA3D_Effect *effect)
{
	MetalEffect *mtlEffect = (MetalEffect*) effect;
	return mtlEffect->effect;
}

/* Driver */

static uint8_t METAL_PrepareWindowAttributes(uint32_t *flags)
{
	/* Let's find out if the OS supports Metal... */
	const char *osVersion = SDL_GetPlatform();
	uint8_t isApplePlatform = (
		(strcmp(osVersion, "Mac OS X") == 0) ||
		(strcmp(osVersion, "iOS") == 0) ||
		(strcmp(osVersion, "tvOS") == 0)
	);
	void* metalFramework;

	if (!isApplePlatform)
	{
		/* What are you even doing here...? */
		return 0;
	}

	/* Try loading MTLCreateSystemDefaultDevice */
	metalFramework = SDL_LoadObject(
		"/System/Library/Frameworks/Metal.framework/Metal"
	);
	if (metalFramework == NULL)
	{
		/* Can't load the Metal framework! */
		return 0;
	}
	MTLCreateSystemDefaultDevice =
		(pfn_CreateSystemDefaultDevice) SDL_LoadFunction(
			metalFramework,
			"MTLCreateSystemDefaultDevice"
		);
	if (MTLCreateSystemDefaultDevice() == NULL)
	{
		/* This OS is too old for Metal! */
		return 0;
	}

	/* We're good to go, so initialize the Objective-C references. */
	InitObjC();

	/* Metal doesn't require any window flags. */
	SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
	return 1;
}

void METAL_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	SDL_MetalView tempView = SDL_Metal_CreateView((SDL_Window*) window);
	CAMetalLayer *layer = mtlGetLayer(tempView);
	CGSize size = mtlGetDrawableSize(layer);
	*x = size.width;
	*y = size.height;
	SDL_Metal_DestroyView(tempView);
}

static void InitializeFauxBackbuffer(MetalRenderer *renderer)
{
	uint16_t indices[6] =
	{
		0, 1, 3,
		1, 2, 3
	};
	uint8_t* ptr;
	const char *shaderSource;
	NSString *nsShaderSource, *nsVertShader, *nsFragShader;
	MTLLibrary *library;
	MTLFunction *vertexFunc, *fragFunc;
	MTLSamplerDescriptor *samplerDesc;
	MTLRenderPipelineDescriptor *pipelineDesc;

	/* Create a combined vertex / index buffer
	 * for rendering the faux backbuffer.
	 */
	renderer->backbufferDrawBuffer = mtlNewBuffer(
		renderer->device,
		16 * sizeof(float) + sizeof(indices),
		MTLResourceOptionsCPUCacheModeWriteCombined
	);
	ptr = (uint8_t*) mtlGetBufferContents(
		renderer->backbufferDrawBuffer
	);
	SDL_memcpy(
		ptr + (16 * sizeof(float)),
		indices,
		sizeof(indices)
	);

	/* Create vertex and fragment shaders for the faux backbuffer */
	shaderSource =
		"#include <metal_stdlib>				\n"
		"using namespace metal;					\n"
		"struct VertexIn {					\n"
		"	packed_float2 position; 			\n"
		"	packed_float2 texCoord; 			\n"
		"}; 							\n"
		"struct VertexOut { 					\n"
		"	float4 position [[ position ]];	 		\n"
		"	float2 texCoord; 				\n"
		"}; 							\n"
		"vertex VertexOut vertexShader( 			\n"
		"	uint vertexID [[ vertex_id ]], 			\n"
		"	constant VertexIn *vertexArray [[ buffer(0) ]]	\n"
		") { 							\n"
		"	VertexOut out;					\n"
		"	out.position = float4(				\n"
		"		vertexArray[vertexID].position,		\n"
		"		0.0,					\n"
		"		1.0					\n"
		"	);						\n"
		"	out.position.y *= -1; 				\n"
		"	out.texCoord = vertexArray[vertexID].texCoord;	\n"
		"	return out; 					\n"
		"} 							\n"
		"fragment float4 fragmentShader( 			\n"
		"	VertexOut in [[stage_in]], 			\n"
		"	texture2d<half> colorTexture [[ texture(0) ]],	\n"
		"	sampler s0 [[sampler(0)]]			\n"
		") {							\n"
		"	const half4 colorSample = colorTexture.sample(	\n"
		"		s0,					\n"
		"		in.texCoord				\n"
		"	);						\n"
		"	return float4(colorSample);			\n"
		"}							\n";

	nsShaderSource	= UTF8ToNSString(shaderSource);
	nsVertShader	= UTF8ToNSString("vertexShader");
	nsFragShader	= UTF8ToNSString("fragmentShader");

	library = mtlNewLibraryWithSource(
		renderer->device,
		nsShaderSource
	);
	vertexFunc = mtlNewFunctionWithName(library, nsVertShader);
	fragFunc = mtlNewFunctionWithName(library, nsFragShader);

	objc_release(nsShaderSource);
	objc_release(nsVertShader);
	objc_release(nsFragShader);

	/* Create sampler state */
	samplerDesc = mtlNewSamplerDescriptor();
	mtlSetSamplerMinFilter(samplerDesc, renderer->backbufferScaleMode);
	mtlSetSamplerMagFilter(samplerDesc, renderer->backbufferScaleMode);
	renderer->backbufferSamplerState = mtlNewSamplerState(
		renderer->device,
		samplerDesc
	);
	objc_release(samplerDesc);

	/* Create render pipeline */
	pipelineDesc = mtlNewRenderPipelineDescriptor();
	mtlSetPipelineVertexFunction(pipelineDesc, vertexFunc);
	mtlSetPipelineFragmentFunction(pipelineDesc, fragFunc);
	mtlSetAttachmentPixelFormat(
		mtlGetColorAttachment(pipelineDesc, 0),
		mtlGetLayerPixelFormat(renderer->layer)
	);
	renderer->backbufferPipeline = mtlNewRenderPipelineState(
		renderer->device,
		pipelineDesc
	);
	objc_release(pipelineDesc);
	objc_release(vertexFunc);
	objc_release(fragFunc);
}

FNA3D_Device* METAL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	uint8_t supportsD24S8;
	int32_t i;
	MTLDepthStencilDescriptor *dsDesc;
	MetalRenderer *renderer;
	FNA3D_Device *result;

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(METAL)

	/* Init the MetalRenderer */
	renderer = (MetalRenderer*) SDL_malloc(sizeof(MetalRenderer));
	SDL_memset(renderer, '\0', sizeof(MetalRenderer));

	/* The FNA3D_Device and MetalRenderer need to reference each other */
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

	/* Create the MTLDevice and MTLCommandQueue */
	renderer->device = MTLCreateSystemDefaultDevice();
	renderer->queue = mtlNewCommandQueue(renderer->device);

	/* Create the Metal view and get its layer */
	renderer->view = SDL_Metal_CreateView(
		(SDL_Window*) presentationParameters->deviceWindowHandle
	);
	renderer->layer = mtlGetLayer(renderer->view);

	/* Set up the layer */
	mtlSetLayerDevice(renderer->layer, renderer->device);
	mtlSetLayerFramebufferOnly(renderer->layer, 1);
	mtlSetLayerMagnificationFilter(
		renderer->layer,
		UTF8ToNSString("nearest")
	);

	/* Log driver info */
	FNA3D_LogInfo(
		"FNA3D Driver: Metal\nDevice Name: %s",
		mtlGetDeviceName(renderer->device)
	);

	/* Some users might want pixely upscaling... */
	renderer->backbufferScaleMode = SDL_GetHintBoolean(
		"FNA_GRAPHICS_BACKBUFFER_SCALE_NEAREST", 0
	) ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;

	/* Set device properties */
	renderer->isMac = (strcmp(SDL_GetPlatform(), "Mac OS X") == 0);
	renderer->supportsS3tc = renderer->supportsDxt1 = renderer->isMac;
	if (mtlDeviceSupportsSampleCount(renderer->device, 8))
	{
		renderer->maxMultiSampleCount = 8;
	}
	else
	{
		renderer->maxMultiSampleCount = 4;
	}
	renderer->supportsOcclusionQueries = (
		renderer->isMac ||
		HasModernAppleGPU(renderer->device)
	);

	/* Determine supported depth formats */
	renderer->D16Format = MTLPixelFormatDepth32Float;
	renderer->D24Format = MTLPixelFormatDepth32Float;
	renderer->D24S8Format = MTLPixelFormatDepth32FloatStencil8;

	if (renderer->isMac)
	{
		supportsD24S8 = mtlDeviceSupportsDepth24Stencil8(renderer->device);
		if (supportsD24S8)
		{
			renderer->D24S8Format = MTLPixelFormatDepth24UnormStencil8;

			/* Gross, but at least it's a unorm format! -caleb */
			renderer->D24Format = MTLPixelFormatDepth24UnormStencil8;
			renderer->D16Format = MTLPixelFormatDepth24UnormStencil8;
		}

		/* Depth16Unorm requires macOS 10.12+ */
		if (OperatingSystemAtLeast(10, 12, 0))
		{
			renderer->D16Format = MTLPixelFormatDepth16Unorm;
		}
	}
	else
	{
		/* Depth16Unorm requires iOS/tvOS 13+ */
		if (OperatingSystemAtLeast(13, 0, 0))
		{
			renderer->D16Format = MTLPixelFormatDepth16Unorm;
		}
	}

	/* Initialize frame tracking */
	renderer->maxFramesInFlight = 1;
	renderer->frameSemaphore = SDL_CreateSemaphore(
		renderer->maxFramesInFlight
	);

	/* Initialize texture and sampler collections */
	for (i = 0; i < MAX_TEXTURE_SAMPLERS; i += 1)
	{
		renderer->textures[i] = &NullTexture;
		renderer->samplers[i] = NULL;
	}

	/* Create a default depth stencil state */
	dsDesc = mtlNewDepthStencilDescriptor();
	renderer->defaultDepthStencilState = mtlNewDepthStencilState(
		renderer->device,
		dsDesc
	);
	objc_release(dsDesc);

	/* Create and initialize the faux-backbuffer */
	renderer->backbuffer = (MetalBackbuffer*) SDL_malloc(
		sizeof(MetalBackbuffer)
	);
	SDL_memset(renderer->backbuffer, '\0', sizeof(MetalBackbuffer));
	CreateFramebuffer(renderer, presentationParameters);
	InitializeFauxBackbuffer(renderer);

	/* Initialize PSO caches */
	hmdefault(renderer->pipelineStateCache, NULL);
	hmdefault(renderer->depthStencilStateCache, NULL);
	hmdefault(renderer->samplerStateCache, NULL);
	hmdefault(renderer->vertexDescriptorCache, NULL);

	/* Initialize renderer members not covered by SDL_memset('\0') */
	renderer->multiSampleMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	renderer->multiSampleEnable = 1;
	renderer->viewport.maxDepth = 1.0f;
	renderer->clearDepth = 1.0f;

	/* Return the FNA3D_Device */
	return result;
}

FNA3D_Driver MetalDriver = {
	"Metal",
	METAL_PrepareWindowAttributes,
	METAL_GetDrawableSize,
	METAL_CreateDevice
};

#endif /* FNA3D_DRIVER_METAL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
