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

#if FNA3D_DRIVER_OPENGL

#include "FNA3D_Driver.h"
#include "FNA3D_Driver_OpenGL.h"

#include <SDL.h>
#include <SDL_syswm.h>

/* Internal Structures */

typedef struct OpenGLTexture /* Cast from FNA3D_Texture* */
{
	uint32_t handle;
	GLenum target;
	uint8_t hasMipmaps;
	FNA3D_TextureAddressMode wrapS;
	FNA3D_TextureAddressMode wrapT;
	FNA3D_TextureAddressMode wrapR;
	FNA3D_TextureFilter filter;
	float anisotropy;
	int32_t maxMipmapLevel;
	float lodBias;
} OpenGLTexture;

typedef struct OpenGLBuffer /* Cast from FNA3D_Buffer* */
{
	GLuint handle;
} OpenGLBuffer;

typedef struct OpenGLRenderbuffer /* Cast from FNA3D_Renderbuffer* */
{
	uint8_t filler;
} OpenGLRenderbuffer;

typedef struct OpenGLEffect /* Cast from FNA3D_Effect* */
{
	uint8_t filler;
} OpenGLEffect;

typedef struct OpenGLQuery /* Cast from FNA3D_Query* */
{
	uint8_t filler;
} OpenGLQuery;

typedef struct OpenGLBackbuffer /* Cast from FNA3D_Backbuffer */
{
	#define BACKBUFFER_TYPE_NULL 0
	#define BACKBUFFER_TYPE_OPENGL 1
	uint8_t type;
	union
	{
		struct
		{
			int32_t width;
			int32_t height;
			FNA3D_DepthFormat depthFormat;
			int32_t multiSampleCount;
		} null;
		struct
		{
			GLuint handle;
			int32_t width;
			int32_t height;
			FNA3D_DepthFormat depthFormat;
			int32_t multiSampleCount;

			GLuint texture;
			GLuint colorAttachment;
			GLuint depthStencilAttachment;
		} opengl;
	} buffer;
} OpenGLBackbuffer;

typedef struct OpenGLVertexAttribute
{
	uint32_t currentBuffer;
	void *currentPointer;
	FNA3D_VertexElementFormat currentFormat;
	uint8_t currentNormalized;
	uint32_t currentStride;
} OpenGLVertexAttribute;

typedef struct OpenGLDevice /* Cast from driverData */
{
	/* Context */
	SDL_GLContext context;
	uint8_t useES3;
	uint8_t useCoreProfile;

	/* The Faux-Backbuffer */
	OpenGLBackbuffer *backbuffer;
	FNA3D_DepthFormat windowDepthFormat;
	GLenum backbufferScaleMode;
	GLuint realBackbufferFBO;
	GLuint realBackbufferRBO;
	GLuint vao;

	/* Capabilities */
	/* TODO: Check these...
	 * - DoublePrecisionDepth/OES_single_precision for ClearDepth/DepthRange
	 * - EXT_framebuffer_blit for faux-backbuffer
	 * - ARB_invalidate_subdata for InvalidateFramebuffer
	 */
	uint8_t supports_BaseGL;
	uint8_t supports_CoreGL;
	uint8_t supports_3DTexture;
	uint8_t supports_DoublePrecisionDepth;
	uint8_t supports_OES_single_precision;
	uint8_t supports_ARB_occlusion_query;
	uint8_t supports_NonES3;
	uint8_t supports_NonES3NonCore;
	uint8_t supports_ARB_framebuffer_object;
	uint8_t supports_EXT_framebuffer_blit;
	uint8_t supports_EXT_framebuffer_multisample;
	uint8_t supports_ARB_invalidate_subdata;
	uint8_t supports_ARB_draw_instanced;
	uint8_t supports_ARB_instanced_arrays;
	uint8_t supports_ARB_draw_elements_base_vertex;
	uint8_t supports_EXT_draw_buffers2;
	uint8_t supports_ARB_texture_multisample;
	uint8_t supports_KHR_debug;
	uint8_t supports_GREMEDY_string_marker;
	uint8_t supports_s3tc;
	uint8_t supports_dxt1;
	int32_t maxMultiSampleCount;

	/* Textures */
	int32_t numTextureSlots;
	OpenGLTexture textures[MAX_TEXTURE_SAMPLERS];

	/* Buffer Binding Cache */
	GLuint currentVertexBuffer;
	GLuint currentIndexBuffer;

	/* ld, or LastDrawn, effect/vertex attributes */
	int32_t ldBaseVertex;
	FNA3D_VertexDeclaration ldVertexDeclaration;
	void* ldPointer;
	MOJOSHADER_glEffect *ldEffect;
	MOJOSHADER_effectTechnique *ldTechnique;
	uint32_t ldPass;

	/* Some vertex declarations may have overlapping attributes :/ */
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];

	/* Vertex Attributes */
	int32_t numVertexAttributes;
	OpenGLVertexAttribute attributes[MAX_VERTEX_ATTRIBUTES];
	uint8_t attributeEnabled[MAX_VERTEX_ATTRIBUTES];
	uint8_t previousAttributeEnabled[MAX_VERTEX_ATTRIBUTES];
	int32_t attributeDivisor[MAX_VERTEX_ATTRIBUTES];
	int32_t previousAttributeDivisor[MAX_VERTEX_ATTRIBUTES];

	/* Render Targets */
	int32_t numAttachments;
	GLuint attachments[MAX_RENDERTARGET_BINDINGS];
	GLenum attachmentTypes[MAX_RENDERTARGET_BINDINGS];
	GLuint currentAttachments[MAX_RENDERTARGET_BINDINGS];
	GLenum currentAttachmentTypes[MAX_RENDERTARGET_BINDINGS];
	GLenum drawBuffersArray[MAX_RENDERTARGET_BINDINGS + 2];
	int32_t currentDrawBuffers;
	GLuint currentRenderbuffer;
	FNA3D_DepthFormat currentDepthStencilFormat;
	GLuint targetFramebuffer;
	GLuint resolveFramebufferRead;
	GLuint resolveFramebufferDraw;
	GLuint currentReadFramebuffer;
	GLuint currentDrawFramebuffer;

	/* MojoShader Interop */
	const char *shaderProfile;
	MOJOSHADER_glContext *shaderContext;
	MOJOSHADER_glEffect *currentEffect;
	MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;
	uint8_t renderTargetBound;
	uint8_t effectApplied;

	/* State */
	uint8_t scissorTestEnable;
	FNA3D_Vec4 currentClearColor;
	float currentClearDepth;
	int32_t currentClearStencil;
	FNA3D_ColorWriteChannels colorWriteEnable;
	uint8_t zWriteEnable;
	uint32_t stencilWriteMask;
	uint8_t togglePointSprite;

	/* Threading */
	SDL_threadID threadID;

	/* GL entry points */
	glfntype_glGetString glGetString; /* Loaded early! */
	#define GL_PROC(ext, ret, func, parms) \
		glfntype_##func func;
	#define GL_PROC_EXT(ext, fallback, ret, func, parms) \
		glfntype_##func func;
	#include "FNA3D_Driver_OpenGL_glfuncs.h"
	#undef GL_PROC
	#undef GL_PROC_EXT
} OpenGLDevice;

/* XNA->OpenGL Translation Arrays */

static int32_t XNAToGL_TextureFormat[] =
{
	GL_RGBA,			/* SurfaceFormat.Color */
	GL_RGB,				/* SurfaceFormat.Bgr565 */
	GL_BGRA,			/* SurfaceFormat.Bgra5551 */
	GL_BGRA,			/* SurfaceFormat.Bgra4444 */
	GL_COMPRESSED_TEXTURE_FORMATS,	/* SurfaceFormat.Dxt1 */
	GL_COMPRESSED_TEXTURE_FORMATS,	/* SurfaceFormat.Dxt3 */
	GL_COMPRESSED_TEXTURE_FORMATS,	/* SurfaceFormat.Dxt5 */
	GL_RG,				/* SurfaceFormat.NormalizedByte2 */
	GL_RGBA,			/* SurfaceFormat.NormalizedByte4 */
	GL_RGBA,			/* SurfaceFormat.Rgba1010102 */
	GL_RG,				/* SurfaceFormat.Rg32 */
	GL_RGBA,			/* SurfaceFormat.Rgba64 */
	GL_ALPHA,			/* SurfaceFormat.Alpha8 */
	GL_RED,				/* SurfaceFormat.Single */
	GL_RG,				/* SurfaceFormat.Vector2 */
	GL_RGBA,			/* SurfaceFormat.Vector4 */
	GL_RED,				/* SurfaceFormat.HalfSingle */
	GL_RG,				/* SurfaceFormat.HalfVector2 */
	GL_RGBA,			/* SurfaceFormat.HalfVector4 */
	GL_RGBA,			/* SurfaceFormat.HdrBlendable */
	GL_BGRA,			/* SurfaceFormat.ColorBgraEXT */
};

static int32_t XNAToGL_TextureInternalFormat[] =
{
	GL_RGBA8,				/* SurfaceFormat.Color */
	GL_RGB8,				/* SurfaceFormat.Bgr565 */
	GL_RGB5_A1,				/* SurfaceFormat.Bgra5551 */
	GL_RGBA4,				/* SurfaceFormat.Bgra4444 */
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,	/* SurfaceFormat.Dxt1 */
	GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,	/* SurfaceFormat.Dxt3 */
	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,	/* SurfaceFormat.Dxt5 */
	GL_RG8,					/* SurfaceFormat.NormalizedByte2 */
	GL_RGBA8,				/* SurfaceFormat.NormalizedByte4 */
	GL_RGB10_A2_EXT,			/* SurfaceFormat.Rgba1010102 */
	GL_RG16,				/* SurfaceFormat.Rg32 */
	GL_RGBA16,				/* SurfaceFormat.Rgba64 */
	GL_ALPHA,				/* SurfaceFormat.Alpha8 */
	GL_R32F,				/* SurfaceFormat.Single */
	GL_RG32F,				/* SurfaceFormat.Vector2 */
	GL_RGBA32F,				/* SurfaceFormat.Vector4 */
	GL_R16F,				/* SurfaceFormat.HalfSingle */
	GL_RG16F,				/* SurfaceFormat.HalfVector2 */
	GL_RGBA16F,				/* SurfaceFormat.HalfVector4 */
	GL_RGBA16F,				/* SurfaceFormat.HdrBlendable */
	GL_RGBA8				/* SurfaceFormat.ColorBgraEXT */
};

static int32_t XNAToGL_TextureDataType[] =
{
	GL_UNSIGNED_BYTE,		/* SurfaceFormat.Color */
	GL_UNSIGNED_SHORT_5_6_5,	/* SurfaceFormat.Bgr565 */
	GL_UNSIGNED_SHORT_5_5_5_1_REV,	/* SurfaceFormat.Bgra5551 */
	GL_UNSIGNED_SHORT_4_4_4_4_REV,	/* SurfaceFormat.Bgra4444 */
	GL_ZERO,			/* NOPE */
	GL_ZERO,			/* NOPE */
	GL_ZERO,			/* NOPE */
	GL_BYTE,			/* SurfaceFormat.NormalizedByte2 */
	GL_BYTE,			/* SurfaceFormat.NormalizedByte4 */
	GL_UNSIGNED_INT_2_10_10_10_REV,	/* SurfaceFormat.Rgba1010102 */
	GL_UNSIGNED_SHORT,		/* SurfaceFormat.Rg32 */
	GL_UNSIGNED_SHORT,		/* SurfaceFormat.Rgba64 */
	GL_UNSIGNED_BYTE,		/* SurfaceFormat.Alpha8 */
	GL_FLOAT,			/* SurfaceFormat.Single */
	GL_FLOAT,			/* SurfaceFormat.Vector2 */
	GL_FLOAT,			/* SurfaceFormat.Vector4 */
	GL_HALF_FLOAT,			/* SurfaceFormat.HalfSingle */
	GL_HALF_FLOAT,			/* SurfaceFormat.HalfVector2 */
	GL_HALF_FLOAT,			/* SurfaceFormat.HalfVector4 */
	GL_HALF_FLOAT,			/* SurfaceFormat.HdrBlendable */
	GL_UNSIGNED_BYTE		/* SurfaceFormat.ColorBgraEXT */
};

static int32_t XNAToGL_BlendMode[] =
{
	GL_ONE,				/* Blend.One */
	GL_ZERO,			/* Blend.Zero */
	GL_SRC_COLOR,			/* Blend.SourceColor */
	GL_ONE_MINUS_SRC_COLOR,		/* Blend.InverseSourceColor */
	GL_SRC_ALPHA,			/* Blend.SourceAlpha */
	GL_ONE_MINUS_SRC_ALPHA,		/* Blend.InverseSourceAlpha */
	GL_DST_COLOR,			/* Blend.DestinationColor */
	GL_ONE_MINUS_DST_COLOR,		/* Blend.InverseDestinationColor */
	GL_DST_ALPHA,			/* Blend.DestinationAlpha */
	GL_ONE_MINUS_DST_ALPHA,		/* Blend.InverseDestinationAlpha */
	GL_CONSTANT_COLOR,		/* Blend.BlendFactor */
	GL_ONE_MINUS_CONSTANT_COLOR,	/* Blend.InverseBlendFactor */
	GL_SRC_ALPHA_SATURATE		/* Blend.SourceAlphaSaturation */
};

static int32_t XNAToGL_BlendEquation[] =
{
	GL_FUNC_ADD,			/* BlendFunction.Add */
	GL_FUNC_SUBTRACT,		/* BlendFunction.Subtract */
	GL_FUNC_REVERSE_SUBTRACT,	/* BlendFunction.ReverseSubtract */
	GL_MAX,				/* BlendFunction.Max */
	GL_MIN				/* BlendFunction.Min */
};

static int32_t XNAToGL_CompareFunc[] =
{
	GL_ALWAYS,	/* CompareFunction.Always */
	GL_NEVER,	/* CompareFunction.Never */
	GL_LESS,	/* CompareFunction.Less */
	GL_LEQUAL,	/* CompareFunction.LessEqual */
	GL_EQUAL,	/* CompareFunction.Equal */
	GL_GEQUAL,	/* CompareFunction.GreaterEqual */
	GL_GREATER,	/* CompareFunction.Greater */
	GL_NOTEQUAL	/* CompareFunction.NotEqual */
};

static int32_t XNAToGL_GLStencilOp[] =
{
	GL_KEEP,	/* StencilOperation.Keep */
	GL_ZERO,	/* StencilOperation.Zero */
	GL_REPLACE,	/* StencilOperation.Replace */
	GL_INCR_WRAP,	/* StencilOperation.Increment */
	GL_DECR_WRAP,	/* StencilOperation.Decrement */
	GL_INCR,	/* StencilOperation.IncrementSaturation */
	GL_DECR,	/* StencilOperation.DecrementSaturation */
	GL_INVERT	/* StencilOperation.Invert */
};

static int32_t XNAToGL_FrontFace[] =
{
	GL_ZERO,	/* NOPE */
	GL_CW,		/* CullMode.CullClockwiseFace */
	GL_CCW		/* CullMode.CullCounterClockwiseFace */
};

static int32_t XNAToGL_GLFillMode[] =
{
	GL_FILL,	/* FillMode.Solid */
	GL_LINE		/* FillMode.WireFrame */
};

static int32_t XNAToGL_Wrap[] =
{
	GL_REPEAT,		/* TextureAddressMode.Wrap */
	GL_CLAMP_TO_EDGE,	/* TextureAddressMode.Clamp */
	GL_MIRRORED_REPEAT	/* TextureAddressMode.Mirror */
};

static int32_t XNAToGL_MagFilter[] =
{
	GL_LINEAR,	/* TextureFilter.Linear */
	GL_NEAREST,	/* TextureFilter.Point */
	GL_LINEAR,	/* TextureFilter.Anisotropic */
	GL_LINEAR,	/* TextureFilter.LinearMipPoint */
	GL_NEAREST,	/* TextureFilter.PointMipLinear */
	GL_NEAREST,	/* TextureFilter.MinLinearMagPointMipLinear */
	GL_NEAREST,	/* TextureFilter.MinLinearMagPointMipPoint */
	GL_LINEAR,	/* TextureFilter.MinPointMagLinearMipLinear */
	GL_LINEAR	/* TextureFilter.MinPointMagLinearMipPoint */
};

static int32_t XNAToGL_MinMipFilter[] =
{
	GL_LINEAR_MIPMAP_LINEAR,	/* TextureFilter.Linear */
	GL_NEAREST_MIPMAP_NEAREST,	/* TextureFilter.Point */
	GL_LINEAR_MIPMAP_LINEAR,	/* TextureFilter.Anisotropic */
	GL_LINEAR_MIPMAP_NEAREST,	/* TextureFilter.LinearMipPoint */
	GL_NEAREST_MIPMAP_LINEAR,	/* TextureFilter.PointMipLinear */
	GL_LINEAR_MIPMAP_LINEAR,	/* TextureFilter.MinLinearMagPointMipLinear */
	GL_LINEAR_MIPMAP_NEAREST,	/* TextureFilter.MinLinearMagPointMipPoint */
	GL_NEAREST_MIPMAP_LINEAR,	/* TextureFilter.MinPointMagLinearMipLinear */
	GL_NEAREST_MIPMAP_NEAREST	/* TextureFilter.MinPointMagLinearMipPoint */
};

static int32_t XNAToGL_MinFilter[] =
{
	GL_LINEAR,	/* TextureFilter.Linear */
	GL_NEAREST,	/* TextureFilter.Point */
	GL_LINEAR,	/* TextureFilter.Anisotropic */
	GL_LINEAR,	/* TextureFilter.LinearMipPoint */
	GL_NEAREST,	/* TextureFilter.PointMipLinear */
	GL_LINEAR,	/* TextureFilter.MinLinearMagPointMipLinear */
	GL_LINEAR,	/* TextureFilter.MinLinearMagPointMipPoint */
	GL_NEAREST,	/* TextureFilter.MinPointMagLinearMipLinear */
	GL_NEAREST	/* TextureFilter.MinPointMagLinearMipPoint */
};

static int32_t XNAToGL_DepthStencilAttachment[] =
{
	GL_ZERO,			/* NOPE */
	GL_DEPTH_ATTACHMENT,		/* DepthFormat.Depth16 */
	GL_DEPTH_ATTACHMENT,		/* DepthFormat.Depth24 */
	GL_DEPTH_STENCIL_ATTACHMENT	/* DepthFormat.Depth24Stencil8 */
};

static int32_t XNAToGL_DepthStorage[] =
{
	GL_ZERO,		/* NOPE */
	GL_DEPTH_COMPONENT16,	/* DepthFormat.Depth16 */
	GL_DEPTH_COMPONENT24,	/* DepthFormat.Depth24 */
	GL_DEPTH24_STENCIL8	/* DepthFormat.Depth24Stencil8 */
};

static float XNAToGL_DepthBiasScale[] =
{
	0.0f,				/* DepthFormat.None */
	(float) ((1 << 16) - 1),	/* DepthFormat.Depth16 */
	(float) ((1 << 24) - 1),	/* DepthFormat.Depth24 */
	(float) ((1 << 24) - 1)		/* DepthFormat.Depth24Stencil8 */
};

static MOJOSHADER_usage XNAToGL_VertexAttribUsage[] =
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

static int32_t XNAToGL_VertexAttribSize[] =
{
	1,	/* VertexElementFormat.Single */
	2,	/* VertexElementFormat.Vector2 */
	3,	/* VertexElementFormat.Vector3 */
	4,	/* VertexElementFormat.Vector4 */
	4,	/* VertexElementFormat.Color */
	4,	/* VertexElementFormat.Byte4 */
	2,	/* VertexElementFormat.Short2 */
	4,	/* VertexElementFormat.Short4 */
	2,	/* VertexElementFormat.NormalizedShort2 */
	4,	/* VertexElementFormat.NormalizedShort4 */
	2,	/* VertexElementFormat.HalfVector2 */
	4	/* VertexElementFormat.HalfVector4 */
};

static int32_t XNAToGL_VertexAttribType[] =
{
	GL_FLOAT,		/* VertexElementFormat.Single */
	GL_FLOAT,		/* VertexElementFormat.Vector2 */
	GL_FLOAT,		/* VertexElementFormat.Vector3 */
	GL_FLOAT,		/* VertexElementFormat.Vector4 */
	GL_UNSIGNED_BYTE,	/* VertexElementFormat.Color */
	GL_UNSIGNED_BYTE,	/* VertexElementFormat.Byte4 */
	GL_SHORT,		/* VertexElementFormat.Short2 */
	GL_SHORT,		/* VertexElementFormat.Short4 */
	GL_SHORT,		/* VertexElementFormat.NormalizedShort2 */
	GL_SHORT,		/* VertexElementFormat.NormalizedShort4 */
	GL_HALF_FLOAT,		/* VertexElementFormat.HalfVector2 */
	GL_HALF_FLOAT		/* VertexElementFormat.HalfVector4 */
};

static uint8_t XNAToGL_VertexAttribNormalized(FNA3D_VertexElement *element)
{
	return (	element->vertexElementUsage == FNA3D_VERTEXELEMENTUSAGE_COLOR ||
			element->vertexElementFormat == FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2 ||
			element->vertexElementFormat == FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4	);
}

static int32_t XNAToGL_IndexType[] =
{
	GL_UNSIGNED_SHORT,	/* IndexElementSize.SixteenBits */
	GL_UNSIGNED_INT		/* IndexElementSize.ThirtyTwoBits */
};

static int32_t XNAToGL_IndexSize[] =
{
	2,	/* IndexElementSize.SixteenBits */
	4	/* IndexElementSize.ThirtyTwoBits */
};

static int32_t XNAToGL_Primitive[] =
{
	GL_TRIANGLES,		/* PrimitiveType.TriangleList */
	GL_TRIANGLE_STRIP,	/* PrimitiveType.TriangleStrip */
	GL_LINES,		/* PrimitiveType.LineList */
	GL_LINE_STRIP,		/* PrimitiveType.LineStrip */
	GL_POINTS		/* PrimitiveType.PointListEXT */
};

static int32_t XNAToGL_PrimitiveVerts(
	FNA3D_PrimitiveType primitiveType,
	int32_t primitiveCount
) {
	switch (primitiveType)
	{
		case FNA3D_PRIMITIVETYPE_TRIANGLELIST:
			return primitiveCount * 3;
		case FNA3D_PRIMITIVETYPE_TRIANGLESTRIP:
			return primitiveCount + 2;
		case FNA3D_PRIMITIVETYPE_LINELIST:
			return primitiveCount * 2;
		case FNA3D_PRIMITIVETYPE_LINESTRIP:
			return primitiveCount + 1;
		case FNA3D_PRIMITIVETYPE_POINTLIST_EXT:
			return primitiveCount;
	}
	SDL_assert(0 && "Unrecognized primitive type!");
	return 0;
}

/* Inline Functions */

/* Windows/Visual Studio cruft */
#if defined(_WIN32) && !defined(__cplusplus) /* C++ should have `inline` */
	#define inline __inline
#endif

static inline void BindReadFramebuffer(OpenGLDevice *device, GLuint handle)
{
	if (handle != device->currentReadFramebuffer)
	{
		device->glBindFramebuffer(GL_READ_FRAMEBUFFER, handle);
		device->currentReadFramebuffer = handle;
	}
}

static inline void BindDrawFramebuffer(OpenGLDevice *device, GLuint handle)
{
	if (handle != device->currentDrawFramebuffer)
	{
		device->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, handle);
		device->currentDrawFramebuffer = handle;
	}
}

static inline void BindFramebuffer(OpenGLDevice *device, GLuint handle)
{
	if (	device->currentReadFramebuffer != handle &&
		device->currentDrawFramebuffer != handle	)
	{
		device->glBindFramebuffer(GL_FRAMEBUFFER, handle);
		device->currentReadFramebuffer = handle;
		device->currentDrawFramebuffer = handle;
	}
	else if (device->currentReadFramebuffer != handle)
	{
		device->glBindFramebuffer(GL_READ_FRAMEBUFFER, handle);
		device->currentReadFramebuffer = handle;
	}
	else if (device->currentDrawFramebuffer != handle)
	{
		device->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, handle);
		device->currentDrawFramebuffer = handle;
	}
}

static inline void BindVertexBuffer(OpenGLDevice *device, GLuint handle)
{
	if (handle != device->currentVertexBuffer)
	{
		device->glBindBuffer(GL_ARRAY_BUFFER, handle);
		device->currentVertexBuffer = handle;
	}
}

static inline void BindIndexBuffer(OpenGLDevice *device, GLuint handle)
{
	if (handle != device->currentIndexBuffer)
	{
		device->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle);
		device->currentIndexBuffer = handle;
	}
}

/* Forward Declarations for Internal Functions */

static void OPENGL_INTERNAL_CreateBackbuffer(
	OpenGLDevice *device,
	FNA3D_PresentationParameters *parameters
);
static void OPENGL_INTERNAL_DisposeBackbuffer(OpenGLDevice *device);

/* Device Implementation */

/* Quit */

void OPENGL_DestroyDevice(FNA3D_Device *device)
{
	OpenGLDevice *glDevice = (OpenGLDevice*) device->driverData;
	if (glDevice->useCoreProfile)
	{
		glDevice->glBindVertexArray(0);
		glDevice->glDeleteVertexArrays(1, &glDevice->vao);
	}

	glDevice->glDeleteFramebuffers(1, &glDevice->resolveFramebufferRead);
	glDevice->resolveFramebufferRead = 0;
	glDevice->glDeleteFramebuffers(1, &glDevice->resolveFramebufferDraw);
	glDevice->resolveFramebufferDraw = 0;
	glDevice->glDeleteFramebuffers(1, &glDevice->targetFramebuffer);
	glDevice->targetFramebuffer = 0;

	if (glDevice->backbuffer->type == BACKBUFFER_TYPE_OPENGL)
	{
		OPENGL_INTERNAL_DisposeBackbuffer(glDevice);
	}
	SDL_free(glDevice->backbuffer);
	glDevice->backbuffer = NULL;

	MOJOSHADER_glMakeContextCurrent(NULL);
	MOJOSHADER_glDestroyContext(glDevice->shaderContext);

	SDL_GL_DeleteContext(glDevice->context);

	SDL_free(glDevice);
	SDL_free(device);
}

/* Begin/End Frame */

void OPENGL_BeginFrame(void* driverData)
{
	/* No-op */
}

void OPENGL_SwapBuffers(
	void* driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	/* TODO */
}

void OPENGL_SetPresentationInterval(
	void* driverData,
	FNA3D_PresentInterval presentInterval
) {
	const char *osVersion;
	int disableLateSwapTear;

	if (	presentInterval == FNA3D_PRESENTINTERVAL_DEFAULT ||
		presentInterval == FNA3D_PRESENTINTERVAL_ONE	)
	{
		osVersion = SDL_GetPlatform();
		disableLateSwapTear = (
			(SDL_strcmp(osVersion, "Mac OS X") == 0) ||
			(SDL_strcmp(osVersion, "WinRT") == 0) ||
			SDL_GetHintBoolean("FNA_OPENGL_DISABLE_LATESWAPTEAR", 0)
		);
		if (disableLateSwapTear)
		{
			SDL_GL_SetSwapInterval(1);
		}
		else
		{
			if (SDL_GL_SetSwapInterval(-1) != -1)
			{
				SDL_LogInfo(
					SDL_LOG_CATEGORY_APPLICATION,
					"Using EXT_swap_control_tear VSync!"
				);
			}
			else
			{
				SDL_LogInfo(
					SDL_LOG_CATEGORY_APPLICATION,
					"EXT_swap_control_tear unsupported. Fall back to standard VSync."
				);
				SDL_ClearError();
				SDL_GL_SetSwapInterval(1);
			}
		}
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_IMMEDIATE)
	{
		SDL_GL_SetSwapInterval(0);
	}
	else if (presentInterval == FNA3D_PRESENTINTERVAL_TWO)
	{
		SDL_GL_SetSwapInterval(2);
	}
	else
	{
		SDL_assert(0 && "Unrecognized PresentInterval!");
	}
}

/* Drawing */

static uint8_t colorEquals(FNA3D_Vec4 c1, FNA3D_Vec4 c2)
{
	return (
		c1.x == c2.x &&
		c1.y == c2.y &&
		c1.z == c2.z &&
		c1.w == c2.w
	);
}

void OPENGL_Clear(
	void* driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	uint8_t clearTarget, clearDepth, clearStencil;
	GLenum clearMask;

	/* glClear depends on the scissor rectangle! */
	if (device->scissorTestEnable)
	{
		device->glDisable(GL_SCISSOR_TEST);
	}

	clearTarget = (options & FNA3D_CLEAROPTIONS_TARGET) != 0;
	clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) != 0;
	clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) != 0;

	/* Get the clear mask, set the clear properties if needed */
	clearMask = GL_ZERO;
	if (clearTarget)
	{
		clearMask |= GL_COLOR_BUFFER_BIT;
		if (!colorEquals(*color, device->currentClearColor))
		{
			device->glClearColor(
				color->x,
				color->y,
				color->z,
				color->w
			);
			device->currentClearColor = *color;
		}
		/* glClear depends on the color write mask! */
		if (device->colorWriteEnable != FNA3D_COLORWRITECHANNELS_ALL)
		{
			/* FIXME: ColorWriteChannels1/2/3? -flibit */
			device->glColorMask(1, 1, 1, 1);
		}
	}
	if (clearDepth)
	{
		clearMask |= GL_DEPTH_BUFFER_BIT;
		if (depth != device->currentClearDepth)
		{
			if (device->supports_DoublePrecisionDepth)
			{
				device->glClearDepth((double) depth);
			}
			else
			{
				device->glClearDepthf(depth);
			}
			device->currentClearDepth = depth;
		}
		/* glClear depends on the depth write mask! */
		if (!device->zWriteEnable)
		{
			device->glDepthMask(1);
		}
	}
	if (clearStencil)
	{
		clearMask |= GL_STENCIL_BUFFER_BIT;
		if (stencil != device->currentClearStencil)
		{
			device->glClearStencil(stencil);
			device->currentClearStencil = stencil;
		}
		/* glClear depends on the stencil write mask! */
		if (device->stencilWriteMask != -1)
		{
			/* AKA 0xFFFFFFFF, ugh -flibit */
			device->glStencilMask(-1);
		}
	}

	/* CLEAR! */
	device->glClear(clearMask);

	/* Clean up after ourselves. */
	if (device->scissorTestEnable)
	{
		device->glEnable(GL_SCISSOR_TEST);
	}
	if (clearTarget && device->colorWriteEnable != FNA3D_COLORWRITECHANNELS_ALL)
	{
		/* FIXME: ColorWriteChannels1/2/3? -flibit */
		device->glColorMask(
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (clearDepth && !device->zWriteEnable)
	{
		device->glDepthMask(0);
	}
	if (clearStencil && device->stencilWriteMask != -1) /* AKA 0xFFFFFFFF, ugh -flibit */
	{
		device->glStencilMask(device->stencilWriteMask);
	}
}

void OPENGL_DrawIndexedPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
) {
	uint8_t tps;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *buffer = (OpenGLBuffer*) indices;

	BindIndexBuffer(device, buffer->handle);

	tps = (	device->togglePointSprite &&
		primitiveType == FNA3D_PRIMITIVETYPE_POINTLIST_EXT	);
	if (tps)
	{
		device->glEnable(GL_POINT_SPRITE);
	}

	/* Draw! */
	if (device->supports_ARB_draw_elements_base_vertex)
	{
		device->glDrawRangeElementsBaseVertex(
			XNAToGL_Primitive[primitiveType],
			minVertexIndex,
			minVertexIndex + numVertices - 1,
			XNAToGL_PrimitiveVerts(primitiveType, primitiveCount),
			XNAToGL_IndexType[indexElementSize],
			(void*) (size_t) (startIndex * XNAToGL_IndexSize[indexElementSize]),
			baseVertex
		);
	}
	else
	{
		device->glDrawRangeElements(
			XNAToGL_Primitive[primitiveType],
			minVertexIndex,
			minVertexIndex + numVertices - 1,
			XNAToGL_PrimitiveVerts(primitiveType, primitiveCount),
			XNAToGL_IndexType[indexElementSize],
			(void*) (size_t) (startIndex * XNAToGL_IndexSize[indexElementSize])
		);
	}

	if (tps)
	{
		device->glDisable(GL_POINT_SPRITE);
	}
}

void OPENGL_DrawInstancedPrimitives(
	void* driverData,
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
	/* Note that minVertexIndex and numVertices are NOT used! */

	uint8_t tps;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *buffer = (OpenGLBuffer*) indices;

	SDL_assert(device->supports_ARB_draw_instanced);

	BindIndexBuffer(device, buffer->handle);

	tps = (	device->togglePointSprite &&
		primitiveType == FNA3D_PRIMITIVETYPE_POINTLIST_EXT	);
	if (tps)
	{
		device->glEnable(GL_POINT_SPRITE);
	}

	/* Draw! */
	if (device->supports_ARB_draw_elements_base_vertex)
	{
		device->glDrawElementsInstancedBaseVertex(
			XNAToGL_Primitive[primitiveType],
			XNAToGL_PrimitiveVerts(primitiveType, primitiveCount),
			XNAToGL_IndexType[indexElementSize],
			(void*) (size_t) (startIndex * XNAToGL_IndexSize[indexElementSize]),
			instanceCount,
			baseVertex
		);
	}
	else
	{
		device->glDrawElementsInstanced(
			XNAToGL_Primitive[primitiveType],
			XNAToGL_PrimitiveVerts(primitiveType, primitiveCount),
			XNAToGL_IndexType[indexElementSize],
			(void*) (size_t) (startIndex * XNAToGL_IndexSize[indexElementSize]),
			instanceCount
		);
	}

	if (tps)
	{
		device->glDisable(GL_POINT_SPRITE);
	}
}

void OPENGL_DrawPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	uint8_t tps;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	tps = (	device->togglePointSprite &&
		primitiveType == FNA3D_PRIMITIVETYPE_POINTLIST_EXT	);
	if (tps)
	{
		device->glEnable(GL_POINT_SPRITE);
	}

	/* Draw! */
	device->glDrawArrays(
		XNAToGL_Primitive[primitiveType],
		vertexStart,
		XNAToGL_PrimitiveVerts(primitiveType, primitiveCount)
	);

	if (tps)
	{
		device->glDisable(GL_POINT_SPRITE);
	}
}

void OPENGL_DrawUserIndexedPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t numVertices,
	void* indexData,
	int32_t indexOffset,
	FNA3D_IndexElementSize indexElementSize,
	int32_t primitiveCount
) {
	uint8_t tps;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	/* Unbind any active index buffer */
	BindIndexBuffer(device, 0);

	tps = (	device->togglePointSprite &&
		primitiveType == FNA3D_PRIMITIVETYPE_POINTLIST_EXT	);
	if (tps)
	{
		device->glEnable(GL_POINT_SPRITE);
	}

	/* Draw! */
	device->glDrawRangeElements(
		XNAToGL_Primitive[primitiveType],
		0,
		numVertices - 1,
		XNAToGL_PrimitiveVerts(primitiveType, primitiveCount),
		XNAToGL_IndexType[indexElementSize],
		(void*) (
			((size_t) indexData) +
			(indexOffset * XNAToGL_IndexSize[indexElementSize])
		)
	);

	if (tps)
	{
		device->glDisable(GL_POINT_SPRITE);
	}
}

void OPENGL_DrawUserPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	uint8_t tps;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	tps = (	device->togglePointSprite &&
		primitiveType == FNA3D_PRIMITIVETYPE_POINTLIST_EXT	);
	if (tps)
	{
		device->glEnable(GL_POINT_SPRITE);
	}

	/* Draw! */
	device->glDrawArrays(
		XNAToGL_Primitive[primitiveType],
		vertexOffset,
		XNAToGL_PrimitiveVerts(primitiveType, primitiveCount)
	);

	if (tps)
	{
		device->glDisable(GL_POINT_SPRITE);
	}
}

/* Mutable Render States */

void OPENGL_SetViewport(void* driverData, FNA3D_Viewport *viewport)
{
	/* TODO */
}

void OPENGL_SetScissorRect(void* driverData, FNA3D_Rect *scissor)
{
	/* TODO */
}

void OPENGL_GetBlendFactor(
	void* driverData,
	FNA3D_Color *blendFactor
) {
	/* TODO */
}

void OPENGL_SetBlendFactor(
	void* driverData,
	FNA3D_Color *blendFactor
) {
	/* TODO */
}

int32_t OPENGL_GetMultiSampleMask(void* driverData)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_texture_multisample);
	return 0;
}

void OPENGL_SetMultiSampleMask(void* driverData, int32_t mask)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_texture_multisample);
}

int32_t OPENGL_GetReferenceStencil(void* driverData)
{
	/* TODO */
	return 0;
}

void OPENGL_SetReferenceStencil(void* driverData, int32_t ref)
{
	/* TODO */
}

/* Immutable Render States */

void OPENGL_SetBlendState(
	void* driverData,
	FNA3D_BlendState *blendState
) {
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_EXT_draw_buffers2);
}

void OPENGL_SetDepthStencilState(
	void* driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	/* TODO */
}

void OPENGL_ApplyRasterizerState(
	void* driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	/* TODO */
}

void OPENGL_VerifySampler(
	void* driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	/* TODO */
}

/* Vertex State */

void OPENGL_ApplyVertexBufferBindings(
	void* driverData,
	/* FIXME: Oh shit VertexBufferBinding[] bindings, */
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_instanced_arrays); /* If divisor > 0 */
}

void OPENGL_ApplyVertexDeclaration(
	void* driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	/* TODO */
}

/* Render Targets */

void OPENGL_SetRenderTargets(
	void* driverData,
	/* FIXME: Oh shit RenderTargetBinding[] renderTargets, */
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	/* TODO */
}

void OPENGL_ResolveTarget(
	void* driverData
	/* FIXME: Oh shit RenderTargetBinding target */
) {
	/* TODO */
}

/* Backbuffer Functions */

static void OPENGL_INTERNAL_CreateBackbuffer(
	OpenGLDevice *device,
	FNA3D_PresentationParameters *parameters
) {
	int useFauxBackbuffer;
	int drawX, drawY;
	SDL_GL_GetDrawableSize(
		(SDL_Window*) parameters->deviceWindowHandle,
		&drawX,
		&drawY
	);
	useFauxBackbuffer = (	drawX != parameters->backBufferWidth ||
				drawY != parameters->backBufferHeight	);
	useFauxBackbuffer = (	useFauxBackbuffer ||
				(parameters->multiSampleCount > 0)	);

	if (useFauxBackbuffer)
	{
		#define GLBACKBUFFER device->backbuffer->buffer.opengl
		if (	device->backbuffer == NULL ||
			device->backbuffer->type == BACKBUFFER_TYPE_NULL	)
		{
			if (!device->supports_EXT_framebuffer_blit)
			{
				SDL_LogError(
					SDL_LOG_CATEGORY_APPLICATION,
					"Your hardware does not support the faux-backbuffer!"
					"\n\nKeep the window/backbuffer resolution the same."
				);
				return;
			}
			if (device->backbuffer != NULL)
			{
				SDL_free(device->backbuffer);
			}
			device->backbuffer = (OpenGLBackbuffer*) SDL_malloc(
				sizeof(OpenGLBackbuffer)
			);
			device->backbuffer->type = BACKBUFFER_TYPE_OPENGL;

			GLBACKBUFFER.width = parameters->backBufferWidth;
			GLBACKBUFFER.height = parameters->backBufferHeight;
			GLBACKBUFFER.depthFormat = parameters->depthStencilFormat;
			GLBACKBUFFER.multiSampleCount = parameters->multiSampleCount;
			GLBACKBUFFER.texture = 0;

			/* Generate and bind the FBO. */
			device->glGenFramebuffers(1, &GLBACKBUFFER.handle);
			BindFramebuffer(device, GLBACKBUFFER.handle);

			/* Create and attach the color buffer */
			device->glGenRenderbuffers(
				1,
				&GLBACKBUFFER.colorAttachment
			);
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				GLBACKBUFFER.colorAttachment
			);
			if (GLBACKBUFFER.multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					GLBACKBUFFER.multiSampleCount,
					GL_RGBA8,
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					GL_RGBA8,
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				GLBACKBUFFER.colorAttachment
			);

			if (GLBACKBUFFER.depthFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				/* Don't bother creating a DS buffer */
				GLBACKBUFFER.depthStencilAttachment = 0;

				/* Keep this state sane. */
				device->glBindRenderbuffer(
					GL_RENDERBUFFER,
					device->realBackbufferRBO
				);

				return;
			}

			device->glGenRenderbuffers(
				1,
				&GLBACKBUFFER.depthStencilAttachment
			);
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				GLBACKBUFFER.depthStencilAttachment
			);
			if (GLBACKBUFFER.multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					GLBACKBUFFER.multiSampleCount,
					XNAToGL_DepthStorage[
						GLBACKBUFFER.depthFormat
					],
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					XNAToGL_DepthStorage[
						GLBACKBUFFER.depthFormat
					],
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_DEPTH_ATTACHMENT,
				GL_RENDERBUFFER,
				GLBACKBUFFER.depthStencilAttachment
			);
			if (GLBACKBUFFER.depthFormat == FNA3D_DEPTHFORMAT_D24S8)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_STENCIL_ATTACHMENT,
					GL_RENDERBUFFER,
					GLBACKBUFFER.depthStencilAttachment
				);
			}

			/* Keep this state sane. */
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				device->realBackbufferRBO
			);
		}
		else
		{
			GLBACKBUFFER.width = parameters->backBufferWidth;
			GLBACKBUFFER.height = parameters->backBufferHeight;
			GLBACKBUFFER.multiSampleCount = parameters->multiSampleCount;
			if (GLBACKBUFFER.texture != 0)
			{
				device->glDeleteTextures(1, &GLBACKBUFFER.texture);
				GLBACKBUFFER.texture = 0;
			}

			if (device->renderTargetBound)
			{
				device->glBindFramebuffer(
					GL_FRAMEBUFFER,
					GLBACKBUFFER.handle
				);
			}

			/* Detach color attachment */
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				0
			);

			/* Detach depth/stencil attachment, if applicable */
			if (GLBACKBUFFER.depthStencilAttachment != 0)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					0
				);
				if (GLBACKBUFFER.depthFormat == FNA3D_DEPTHFORMAT_D24S8)
				{
					device->glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						0
					);
				}
			}

			/* Update our color attachment to the new resolution. */
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				GLBACKBUFFER.colorAttachment
			);
			if (GLBACKBUFFER.multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					GLBACKBUFFER.multiSampleCount,
					GL_RGBA8,
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					GL_RGBA8,
					GLBACKBUFFER.width,
					GLBACKBUFFER.height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				GLBACKBUFFER.colorAttachment
			);

			/* Generate/Delete depth/stencil attachment, if needed */
			if (parameters->depthStencilFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				if (GLBACKBUFFER.depthStencilAttachment != 0)
				{
					device->glDeleteRenderbuffers(
						1,
						&GLBACKBUFFER.depthStencilAttachment
					);
					GLBACKBUFFER.depthStencilAttachment = 0;
				}
			}
			else if (GLBACKBUFFER.depthStencilAttachment == 0)
			{
				device->glGenRenderbuffers(
					1,
					&GLBACKBUFFER.depthStencilAttachment
				);
			}

			/* Update the depth/stencil buffer, if applicable */
			GLBACKBUFFER.depthFormat = parameters->depthStencilFormat;
			if (GLBACKBUFFER.depthStencilAttachment != 0)
			{
				device->glBindRenderbuffer(
					GL_RENDERBUFFER,
					GLBACKBUFFER.depthStencilAttachment
				);
				if (GLBACKBUFFER.multiSampleCount > 0)
				{
					device->glRenderbufferStorageMultisample(
						GL_RENDERBUFFER,
						GLBACKBUFFER.multiSampleCount,
						XNAToGL_DepthStorage[GLBACKBUFFER.depthFormat],
						GLBACKBUFFER.width,
						GLBACKBUFFER.height
					);
				}
				else
				{
					device->glRenderbufferStorage(
						GL_RENDERBUFFER,
						XNAToGL_DepthStorage[GLBACKBUFFER.depthFormat],
						GLBACKBUFFER.width,
						GLBACKBUFFER.height
					);
				}
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					GLBACKBUFFER.depthStencilAttachment
				);
				if (GLBACKBUFFER.depthFormat == FNA3D_DEPTHFORMAT_D24S8)
				{
					device->glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						GLBACKBUFFER.depthStencilAttachment
					);
				}
			}

			if (device->renderTargetBound)
			{
				device->glBindFramebuffer(
					GL_FRAMEBUFFER,
					device->targetFramebuffer
				);
			}

			/* Keep this state sane. */
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				device->realBackbufferRBO
			);
		}
		#undef GLBACKBUFFER
	}
	else
	{
		if (	device->backbuffer == NULL ||
			device->backbuffer->type == BACKBUFFER_TYPE_OPENGL	)
		{
			if (device->backbuffer != NULL)
			{
				OPENGL_INTERNAL_DisposeBackbuffer(device);
				SDL_free(device->backbuffer);
			}
			device->backbuffer = (OpenGLBackbuffer*) SDL_malloc(
				sizeof(OpenGLBackbuffer)
			);
			device->backbuffer->type = BACKBUFFER_TYPE_NULL;
		}
		device->backbuffer->buffer.null.width = parameters->backBufferWidth;
		device->backbuffer->buffer.null.height = parameters->backBufferHeight;
		device->backbuffer->buffer.null.depthFormat = device->windowDepthFormat;
	}
}

static void OPENGL_INTERNAL_DisposeBackbuffer(OpenGLDevice *device)
{
	#define GLBACKBUFFER device->backbuffer->buffer.opengl

	BindFramebuffer(device, device->realBackbufferFBO);
	device->glDeleteFramebuffers(1, &GLBACKBUFFER.handle);
	device->glDeleteRenderbuffers(1, &GLBACKBUFFER.colorAttachment);
	if (GLBACKBUFFER.depthStencilAttachment != 0)
	{
		device->glDeleteRenderbuffers(1, &GLBACKBUFFER.depthStencilAttachment);
	}
	if (GLBACKBUFFER.texture != 0)
	{
		device->glDeleteTextures(1, &GLBACKBUFFER.texture);
	}
	GLBACKBUFFER.handle = 0;

	#undef GLBACKBUFFER
}

FNA3D_Backbuffer* OPENGL_GetBackbuffer(void* driverData)
{
	/* TODO */
	return NULL;
}

void OPENGL_ResetBackbuffer(
	void* driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
}

void OPENGL_ReadBackbuffer(
	void* driverData,
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
	/* TODO */
}

/* Textures */

FNA3D_Texture* OPENGL_CreateTexture2D(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
	return NULL;
}

FNA3D_Texture* OPENGL_CreateTexture3D(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_3DTexture);
	return NULL;
}

FNA3D_Texture* OPENGL_CreateTextureCube(
	void* driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
	return NULL;
}

void OPENGL_AddDisposeTexture(
	void* driverData,
	FNA3D_Texture *texture
) {
	/* TODO */
}

void OPENGL_SetTextureData2D(
	void* driverData,
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

void OPENGL_SetTextureData3D(
	void* driverData,
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
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_3DTexture);
}

void OPENGL_SetTextureDataCube(
	void* driverData,
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

void OPENGL_SetTextureDataYUV(
	void* driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
	/* TODO */
}

void OPENGL_GetTextureData2D(
	void* driverData,
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
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_NonES3);
}

void OPENGL_GetTextureData3D(
	void* driverData,
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
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_NonES3);
}

void OPENGL_GetTextureDataCube(
	void* driverData,
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
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_NonES3);
}

/* Renderbuffers */

FNA3D_Renderbuffer* OPENGL_GenColorRenderbuffer(
	void* driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	/* TODO */
	return NULL;
}

FNA3D_Renderbuffer* OPENGL_GenDepthStencilRenderbuffer(
	void* driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	/* TODO */
	return NULL;
}

void OPENGL_AddDisposeRenderbuffer(
	void* driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	/* TODO */
}

/* Vertex Buffers */

FNA3D_Buffer* OPENGL_GenVertexBuffer(
	void* driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	/* TODO */
	return NULL;
}

void OPENGL_AddDisposeVertexBuffer(
	void* driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

void OPENGL_SetVertexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

void OPENGL_GetVertexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	/* TODO */
}

/* Index Buffers */

FNA3D_Buffer* OPENGL_GenIndexBuffer(
	void* driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	/* TODO */
	return NULL;
}

void OPENGL_AddDisposeIndexBuffer(
	void* driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

void OPENGL_SetIndexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

void OPENGL_GetIndexBufferData(
	void* driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	/* TODO */
}

/* Effects */

FNA3D_Effect* OPENGL_CreateEffect(
	void* driverData,
	uint8_t *effectCode
) {
	/* TODO */
	return NULL;
}

FNA3D_Effect* OPENGL_CloneEffect(
	void* driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
	return NULL;
}

void OPENGL_AddDisposeEffect(
	void* driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

void OPENGL_ApplyEffect(
	void* driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

void OPENGL_BeginPassRestore(
	void* driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	/* TODO */
}

void OPENGL_EndPassRestore(
	void* driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
}

/* Queries */

FNA3D_Query* OPENGL_CreateQuery(void* driverData)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
	return NULL;
}

void OPENGL_AddDisposeQuery(void* driverData, FNA3D_Query *query)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
}

void OPENGL_QueryBegin(void* driverData, FNA3D_Query *query)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
}

void OPENGL_QueryEnd(void* driverData, FNA3D_Query *query)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
}

uint8_t OPENGL_QueryComplete(void* driverData, FNA3D_Query *query)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
	return 1;
}

int32_t OPENGL_QueryPixelCount(
	void* driverData,
	FNA3D_Query *query
) {
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);
	return 0;
}

/* Feature Queries */

uint8_t OPENGL_SupportsDXT1(void* driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->supports_dxt1;
}

uint8_t OPENGL_SupportsS3TC(void* driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->supports_s3tc;
}

uint8_t OPENGL_SupportsHardwareInstancing(void* driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return (	device->supports_ARB_draw_instanced &&
			device->supports_ARB_instanced_arrays	);
}

uint8_t OPENGL_SupportsNoOverwrite(void* driverData)
{
	return 0;
}

int32_t OPENGL_GetMaxTextureSlots(void* driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->numTextureSlots;
}

int32_t OPENGL_GetMaxMultiSampleCount(void* driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->maxMultiSampleCount;
}

/* Debugging */

void OPENGL_SetStringMarker(void* driverData, const char *text)
{
	/* TODO */
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_GREMEDY_string_marker);
}

static const char *debugSourceStr[] = {
	"GL_DEBUG_SOURCE_API",
	"GL_DEBUG_SOURCE_WINDOW_SYSTEM",
	"GL_DEBUG_SOURCE_SHADER_COMPILER",
	"GL_DEBUG_SOURCE_THIRD_PARTY",
	"GL_DEBUG_SOURCE_APPLICATION",
	"GL_DEBUG_SOURCE_OTHER"
};
static const char *debugTypeStr[] = {
	"GL_DEBUG_TYPE_ERROR",
	"GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR",
	"GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR",
	"GL_DEBUG_TYPE_PORTABILITY",
	"GL_DEBUG_TYPE_PERFORMANCE",
	"GL_DEBUG_TYPE_OTHER"
};
static const char *debugSeverityStr[] = {
	"GL_DEBUG_SEVERITY_HIGH",
	"GL_DEBUG_SEVERITY_MEDIUM",
	"GL_DEBUG_SEVERITY_LOW",
	"GL_DEBUG_SEVERITY_NOTIFICATION"
};

static void GLAPIENTRY DebugCall(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam
) {
	SDL_LogWarn(
		SDL_LOG_CATEGORY_APPLICATION,
		"%s\n\tSource: %s\n\tType: %s\n\tSeverity: %s",
		message,
		debugSourceStr[source - GL_DEBUG_SOURCE_API],
		debugTypeStr[type - GL_DEBUG_TYPE_ERROR],
		debugSeverityStr[severity - GL_DEBUG_SEVERITY_HIGH]
	);
	if (type == GL_DEBUG_TYPE_ERROR)
	{
		SDL_assert(0 && "ARB_debug_output error, check your logs!");
	}
}

/* Buffer Objects */

intptr_t OPENGL_GetBufferSize(FNA3D_Buffer *buffer)
{
	/* TODO */
	return 0;
}

/* Effect Objects */

void* OPENGL_GetEffectData(FNA3D_Effect *effect)
{
	/* TODO */
	return NULL;
}

/* Backbuffer Objects */

int32_t OPENGL_GetBackbufferWidth(FNA3D_Backbuffer *backbuffer)
{
	/* TODO */
	return 0;
}

int32_t OPENGL_GetBackbufferHeight(FNA3D_Backbuffer *backbuffer)
{
	/* TODO */
	return 0;
}

FNA3D_DepthFormat OPENGL_GetBackbufferDepthFormat(
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return FNA3D_DEPTHFORMAT_NONE;
}

int32_t OPENGL_GetBackbufferMultiSampleCount(FNA3D_Backbuffer *backbuffer)
{
	/* TODO */
	return 0;
}

/* Load GL Entry Points */

static void LoadEntryPoints(
	OpenGLDevice *device,
	const char *driverInfo,
	uint8_t debugMode
) {
	const char *baseErrorString = (
		device->useES3 ?
		"OpenGL ES 3.0 support is required!" :
		"OpenGL 2.1 support is required!"
	);

	device->supports_BaseGL = 1;
	device->supports_CoreGL = 1;
	device->supports_3DTexture = 1;
	device->supports_DoublePrecisionDepth = 1;
	device->supports_OES_single_precision = 1;
	device->supports_ARB_occlusion_query = 1;
	device->supports_NonES3 = 1;
	device->supports_NonES3NonCore = 1;
	device->supports_ARB_framebuffer_object = 1;
	device->supports_EXT_framebuffer_blit = 1;
	device->supports_EXT_framebuffer_multisample = 1;
	device->supports_ARB_invalidate_subdata = 1;
	device->supports_ARB_draw_instanced = 1;
	device->supports_ARB_instanced_arrays = 1;
	device->supports_ARB_draw_elements_base_vertex = 1;
	device->supports_EXT_draw_buffers2 = 1;
	device->supports_ARB_texture_multisample = 1;
	device->supports_KHR_debug = 1;
	device->supports_GREMEDY_string_marker = 1;

	#define GL_PROC(ext, ret, func, parms) \
		device->func = (glfntype_##func) SDL_GL_GetProcAddress(#func); \
		if (device->func == NULL) \
		{ \
			device->supports_##ext = 0; \
		}
	#define GL_PROC_EXT(ext, fallback, ret, func, parms) \
		device->func = (glfntype_##func) SDL_GL_GetProcAddress(#func); \
		if (device->func == NULL) \
		{ \
			device->func = (glfntype_##func) SDL_GL_GetProcAddress(#func #fallback); \
			if (device->func == NULL) \
			{ \
				device->supports_##ext = 0; \
			} \
		}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	#include "FNA3D_Driver_OpenGL_glfuncs.h"
#pragma GCC diagnostic pop
	#undef GL_PROC
	#undef GL_PROC_EXT

	/* Weeding out the GeForce FX cards... */
	if (!device->supports_BaseGL)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n%s",
			baseErrorString,
			driverInfo
		);
		return;
	}

	/* No depth precision whatsoever? Something's busted. */
	if (	!device->supports_DoublePrecisionDepth &&
		!device->supports_OES_single_precision	)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n%s",
			baseErrorString,
			driverInfo
		);
		return;
	}

	/* If you asked for core profile, you better have it! */
	if (device->useCoreProfile && !device->supports_CoreGL)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"OpenGL 3.2 Core support is required!\n%s",
			driverInfo
		);
		return;
	}

	/* Some stuff is okay for ES3, not for desktop. */
	if (device->useES3)
	{
		if (!device->supports_3DTexture)
		{
			SDL_LogWarn(
				SDL_LOG_CATEGORY_APPLICATION,
				"3D textures unsupported, beware..."
			);
		}
		if (!device->supports_ARB_occlusion_query)
		{
			SDL_LogWarn(
				SDL_LOG_CATEGORY_APPLICATION,
				"Occlusion queries unsupported, beware..."
			);
		}
		if (!device->supports_ARB_invalidate_subdata)
		{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
			device->glInvalidateFramebuffer =
				(glfntype_glInvalidateFramebuffer) SDL_GL_GetProcAddress(
					"glDiscardFramebufferEXT"
			);
#pragma GCC diagnostic pop
			device->supports_ARB_invalidate_subdata =
				device->glInvalidateFramebuffer != NULL;
		}
	}
	else
	{
		if (	!device->supports_3DTexture ||
			!device->supports_ARB_occlusion_query ||
			!device->supports_NonES3	)
		{
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"%s\n%s",
				baseErrorString,
				driverInfo
			);
			return;
		}
	}

	/* AKA: The shitty TexEnvi check */
	if (	!device->useES3 &&
		!device->useCoreProfile &&
		!device->supports_NonES3NonCore	)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s\n%s",
			baseErrorString,
			driverInfo
		);
		return;
	}

	/* ColorMask is an absolute mess */
	if (!device->supports_EXT_draw_buffers2)
	{
		#define LOAD_COLORMASK(suffix) \
		device->glColorMaski = (glfntype_glColorMaski) \
			SDL_GL_GetProcAddress("glColorMask" #suffix);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
		LOAD_COLORMASK(IndexedEXT)
		if (device->glColorMaski == NULL) LOAD_COLORMASK(iOES)
		if (device->glColorMaski == NULL) LOAD_COLORMASK(iEXT)
#pragma GCC diagnostic pop
		if (device->glColorMaski != NULL)
		{
			device->supports_EXT_draw_buffers2 = 1;
		}
		#undef LOAD_COLORMASK
	}

	/* Possibly bogus if a game never uses render targets? */
	if (!device->supports_ARB_framebuffer_object)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"OpenGL framebuffer support is required!\n%s",
			driverInfo
		);
		return;
	}

	/* Everything below this check is for debug contexts */
	if (!debugMode)
	{
		return;
	}

	if (device->supports_KHR_debug)
	{
		device->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DONT_CARE,
			GL_DONT_CARE,
			0,
			NULL,
			GL_TRUE
		);
		device->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DEBUG_TYPE_OTHER,
			GL_DEBUG_SEVERITY_LOW,
			0,
			NULL,
			GL_FALSE
		);
		device->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DEBUG_TYPE_OTHER,
			GL_DEBUG_SEVERITY_NOTIFICATION,
			0,
			NULL,
			GL_FALSE
		);
		device->glDebugMessageCallback(DebugCall, NULL);
	}
	else
	{
		SDL_LogWarn(
			SDL_LOG_CATEGORY_APPLICATION,
			"ARB_debug_output/KHR_debug not supported!"
		);
	}

	if (!device->supports_GREMEDY_string_marker)
	{
		SDL_LogWarn(
			SDL_LOG_CATEGORY_APPLICATION,
			"GREMEDY_string_marker not supported!"
		);
	}
}

static void* MOJOSHADERCALL GLGetProcAddress(const char *ep, void* d)
{
	return SDL_GL_GetProcAddress(ep);
}

static void checkExtensions(
	const char *ext,
	uint8_t *supportsS3tc,
	uint8_t *supportsDxt1
) {
	int s3tc = (
		SDL_strstr(ext, "GL_EXT_texture_compression_s3tc") ||
		SDL_strstr(ext, "GL_OES_texture_compression_S3TC") ||
		SDL_strstr(ext, "GL_EXT_texture_compression_dxt3") ||
		SDL_strstr(ext, "GL_EXT_texture_compression_dxt5")
	);

	if (s3tc)
	{
		*supportsS3tc = 1;
	}
	if (s3tc || SDL_strstr(ext, "GL_EXT_texture_compression_dxt1"))
	{
		*supportsDxt1 = 1;
	}
}

/* Driver */

uint8_t OPENGL_PrepareWindowAttributes(uint8_t debugMode, uint32_t *flags)
{
	int forceES3, forceCore, forceCompat;
	const char *osVersion;
	int depthSize, stencilSize;
	const char *depthFormatHint;

	/* GLContext environment variables */
	forceES3 = SDL_GetHintBoolean("FNA_OPENGL_FORCE_ES3", 0);
	forceCore = SDL_GetHintBoolean("FNA_OPENGL_FORCE_CORE_PROFILE", 0);
	forceCompat = SDL_GetHintBoolean("FNA_OPENGL_FORCE_COMPATIBILITY_PROFILE", 0);

	/* Some platforms are GLES only */
	osVersion = SDL_GetPlatform();
	forceES3 |= (
		(SDL_strcmp(osVersion, "WinRT") == 0) ||
		(SDL_strcmp(osVersion, "iOS") == 0) ||
		(SDL_strcmp(osVersion, "tvOS") == 0) ||
		(SDL_strcmp(osVersion, "Stadia") == 0) ||
		(SDL_strcmp(osVersion, "Android") == 0) ||
		(SDL_strcmp(osVersion, "Emscripten") == 0)
	);

	/* Window depth format */
	depthSize = 24;
	stencilSize = 8;
	depthFormatHint = SDL_GetHint("FNA_OPENGL_WINDOW_DEPTHSTENCILFORMAT");
	if (depthFormatHint != NULL)
	{
		if (SDL_strcmp(depthFormatHint, "None") == 0)
		{
			depthSize = 0;
			stencilSize = 0;
		}
		else if (SDL_strcmp(depthFormatHint, "Depth16") == 0)
		{
			depthSize = 16;
			stencilSize = 0;
		}
		else if (SDL_strcmp(depthFormatHint, "Depth24") == 0)
		{
			depthSize = 24;
			stencilSize = 0;
		}
		else if (SDL_strcmp(depthFormatHint, "Depth24Stencil8") == 0)
		{
			depthSize = 24;
			stencilSize = 8;
		}
	}

	/* Set context attributes */
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthSize);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencilSize);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	if (forceES3)
	{
		SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_PROFILE_MASK,
			SDL_GL_CONTEXT_PROFILE_ES
		);
	}
	else if (forceCore)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_PROFILE_MASK,
			SDL_GL_CONTEXT_PROFILE_CORE
		);
	}
	else if (forceCompat)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_PROFILE_MASK,
			SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
		);
	}
	if (debugMode)
	{
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_FLAGS,
			SDL_GL_CONTEXT_DEBUG_FLAG
		);
	}

	*flags = SDL_WINDOW_OPENGL;
	return 1;
}

FNA3D_Device* OPENGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters
) {
	int flags;
	int depthSize, stencilSize;
	SDL_SysWMinfo wmInfo;
	const char *renderer, *version, *vendor;
	char driverInfo[256];
	int i;
	int numExtensions, numSamplers, numAttributes, numAttachments;
	OpenGLDevice *device;
	FNA3D_Device *result;

	/* Init the OpenGLDevice */
	device = (OpenGLDevice*) SDL_malloc(sizeof(OpenGLDevice));
	SDL_memset(device, '\0', sizeof(OpenGLDevice));

	/* Create OpenGL context */
	device->context = SDL_GL_CreateContext(
		(SDL_Window*) presentationParameters->deviceWindowHandle
	);

	/* Check for a possible ES/Core context */
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &flags);
	device->useES3 = (flags & SDL_GL_CONTEXT_PROFILE_ES) != 0;
	device->useCoreProfile = (flags & SDL_GL_CONTEXT_PROFILE_CORE) != 0;

	/* Check the window's depth/stencil format */
	SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthSize);
	SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencilSize);
	if (depthSize == 0 && stencilSize == 0)
	{
		device->windowDepthFormat = FNA3D_DEPTHFORMAT_NONE;
	}
	else if (depthSize == 16 && stencilSize == 0)
	{
		device->windowDepthFormat = FNA3D_DEPTHFORMAT_D16;
	}
	else if (depthSize == 24 && stencilSize == 0)
	{
		device->windowDepthFormat = FNA3D_DEPTHFORMAT_D24;
	}
	else if (depthSize == 24 && stencilSize == 8)
	{
		device->windowDepthFormat = FNA3D_DEPTHFORMAT_D24S8;
	}
	else
	{
		SDL_assert(0 && "Unrecognized window depth/stencil format!");
	}

	/* UIKit needs special treatment for backbuffer behavior */
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		&wmInfo
	);
#ifdef SDL_VIDEO_UIKIT
	if (wmInfo.subsystem == SDL_SYSWM_UIKIT)
	{
		device->realBackbufferFBO = wmInfo.info.uikit.framebuffer;
		device->realBackbufferRBO = wmInfo.info.uikit.colorbuffer;
	}
#endif /* SDL_VIDEO_UIKIT */

	/* Print GL information */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	device->glGetString = (glfntype_glGetString) SDL_GL_GetProcAddress("glGetString");
#pragma GCC diagnostic pop
	if (!device->glGetString)
	{
		SDL_assert(0 && "GRAPHICS DRIVER IS EXTREMELY BROKEN!");
	}
	renderer =	(const char*) device->glGetString(GL_RENDERER);
	version =	(const char*) device->glGetString(GL_VERSION);
	vendor =	(const char*) device->glGetString(GL_VENDOR);
	SDL_snprintf(
		driverInfo, sizeof(driverInfo),
		"OpenGL Device: %s\nOpenGL Driver: %s\nOpenGL Vendor: %s",
		renderer, version, vendor
	);
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"IGLDevice: OpenGLDevice\n%s",
		driverInfo
	);

	/* Initialize entry points */
	LoadEntryPoints(device, driverInfo, 0); /* FIXME: Debug context check */

	/* FIXME: REMOVE ME ASAP! TERRIBLE HACK FOR ANGLE! */
	if (!SDL_strstr(renderer, "Direct3D11"))
	{
		device->supports_ARB_draw_elements_base_vertex = 0;
	}

	/* Initialize shader context */
	device->shaderProfile = SDL_GetHint("FNA_GRAPHICS_MOJOSHADER_PROFILE");
	if (device->shaderProfile == NULL || device->shaderProfile[0] == '\0')
	{
		device->shaderProfile = MOJOSHADER_glBestProfile(
			GLGetProcAddress,
			NULL,
			NULL,
			NULL,
			NULL
		);

		/* SPIR-V is very new and not really necessary. */
		if (	(SDL_strcmp(device->shaderProfile, "glspirv") == 0) &&
			!device->useCoreProfile	)
		{
			device->shaderProfile = "glsl120";
		}
	}
	device->shaderContext = MOJOSHADER_glCreateContext(
		device->shaderProfile,
		GLGetProcAddress,
		NULL,
		NULL,
		NULL,
		NULL
	);
	MOJOSHADER_glMakeContextCurrent(device->shaderContext);

	/* Some users might want pixely upscaling... */
	device->backbufferScaleMode = SDL_GetHintBoolean(
		"FNA_GRAPHICS_BACKBUFFER_SCALE_NEAREST", 0
	) ? GL_NEAREST : GL_LINEAR;

	/* Load the extension list, initialize extension-dependent components */
	device->supports_s3tc = 0;
	device->supports_dxt1 = 0;
	if (device->useCoreProfile)
	{
		device->glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
		for (i = 0; i < numExtensions; i += 1)
		{
			checkExtensions(
				(const char*) device->glGetStringi(GL_EXTENSIONS, i),
				&device->supports_s3tc,
				&device->supports_dxt1
			);

			if (device->supports_s3tc && device->supports_dxt1)
			{
				/* No need to look further. */
				break;
			}
		}
	}
	else
	{
		checkExtensions(
			(const char*) device->glGetString(GL_EXTENSIONS),
			&device->supports_s3tc,
			&device->supports_dxt1
		);
	}

	/* Check the max multisample count, override parameters if necessary */
	if (device->supports_EXT_framebuffer_multisample)
	{
		device->glGetIntegerv(
			GL_MAX_SAMPLES,
			&device->maxMultiSampleCount
		);
	}
	presentationParameters->multiSampleCount = SDL_min(
		presentationParameters->multiSampleCount,
		device->maxMultiSampleCount
	);

	/* Initialize the faux backbuffer */
	OPENGL_INTERNAL_CreateBackbuffer(device, presentationParameters);

	/* Initialize texture collection array */
	device->glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &numSamplers);
	numSamplers = SDL_min(numSamplers, MAX_TEXTURE_SAMPLERS);
	for (i = 0; i < numSamplers; i += 1)
	{
		device->textures[i].handle = 0;
		device->textures[i].target = GL_TEXTURE_2D;
		device->textures[i].hasMipmaps = 0;
		device->textures[i].wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP;
		device->textures[i].wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP;
		device->textures[i].wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP;
		device->textures[i].filter = FNA3D_TEXTUREFILTER_LINEAR;
		device->textures[i].anisotropy = 0;
		device->textures[i].maxMipmapLevel = 0;
		device->textures[i].lodBias = 0;
	}
	device->numTextureSlots = numSamplers;

	/* Initialize vertex attribute state arrays */
	device->ldBaseVertex = -1;
	device->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &numAttributes);
	numAttributes = SDL_min(numAttributes, MAX_VERTEX_ATTRIBUTES);
	for (i = 0; i < numAttributes; i += 1)
	{
		device->attributes[i].currentBuffer = 0;
		device->attributes[i].currentPointer = NULL;
		device->attributes[i].currentFormat = FNA3D_VERTEXELEMENTFORMAT_SINGLE;
		device->attributes[i].currentNormalized = 0;
		device->attributes[i].currentStride = 0;

		device->attributeEnabled[i] = 0;
		device->previousAttributeEnabled[i] = 0;
		device->attributeDivisor[i] = 0;
		device->previousAttributeDivisor[i] = 0;
	}
	device->numVertexAttributes = numAttributes;

	/* Initialize render target FBO and state arrays */
	device->glGetIntegerv(GL_MAX_DRAW_BUFFERS, &numAttachments);
	numAttachments = SDL_min(numAttachments, MAX_RENDERTARGET_BINDINGS);
	for (i = 0; i < numAttachments; i += 1)
	{
		device->attachments[i] = 0;
		device->attachmentTypes[i] = 0;
		device->currentAttachments[i] = 0;
		device->currentAttachmentTypes[i] = GL_TEXTURE_2D;
		device->drawBuffersArray[i] = GL_COLOR_ATTACHMENT0 + 1;
	}
	device->numAttachments = numAttachments;

	device->drawBuffersArray[numAttachments] = GL_DEPTH_ATTACHMENT;
	device->drawBuffersArray[numAttachments + 1] = GL_STENCIL_ATTACHMENT;
	device->currentDepthStencilFormat = FNA3D_DEPTHFORMAT_NONE;
	device->glGenFramebuffers(1, &device->targetFramebuffer);
	device->glGenFramebuffers(1, &device->resolveFramebufferRead);
	device->glGenFramebuffers(1, &device->resolveFramebufferDraw);

	if (device->useCoreProfile)
	{
		/* Generate and bind a VAO, to shut Core up */
		device->glGenVertexArrays(1, &device->vao);
		device->glBindVertexArray(device->vao);
	}
	else if (!device->useCoreProfile && !device->useES3)
	{
		/* Compatibility contexts require that point sprites be enabled
		 * explicitly. However, Apple's drivers have a blatant spec
		 * violation that disallows a simple glEnable. So, here we are.
		 * -flibit
		 */
		if (SDL_strcmp(SDL_GetPlatform(), "Mac OS X") == 0)
		{
			device->togglePointSprite = 1;
		}
		else
		{
			device->glEnable(GL_POINT_SPRITE);
		}
		device->glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, 1);
	}

	/* The creation thread will be the "main" thread */
	device->threadID = SDL_ThreadID();

	/* Set up and return the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(OPENGL)
	result->driverData = device;
	return result;
}

FNA3D_Driver OpenGLDriver = {
	"OpenGL",
	OPENGL_PrepareWindowAttributes,
	OPENGL_CreateDevice
};

#endif /* FNA3D_DRIVER_OPENGL */
