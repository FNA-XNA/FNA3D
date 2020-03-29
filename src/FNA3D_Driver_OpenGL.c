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
#include "FNA3D_CommandStream.h"

#include <SDL.h>
#include <SDL_syswm.h>

/* Internal Structures */

typedef struct OpenGLTexture OpenGLTexture;
typedef struct OpenGLRenderbuffer OpenGLRenderbuffer;
typedef struct OpenGLBuffer OpenGLBuffer;
typedef struct OpenGLEffect OpenGLEffect;
typedef struct OpenGLQuery OpenGLQuery;

struct OpenGLTexture /* Cast from FNA3D_Texture* */
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
	OpenGLTexture *next; /* linked list */
};

static OpenGLTexture NullTexture =
{
	0,
	GL_TEXTURE_2D,
	0,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREFILTER_LINEAR,
	0,
	0,
	0
};

struct OpenGLBuffer /* Cast from FNA3D_Buffer* */
{
	GLuint handle;
	intptr_t size;
	GLenum dynamic;
	OpenGLBuffer *next; /* linked list */
};

struct OpenGLRenderbuffer /* Cast from FNA3D_Renderbuffer* */
{
	GLuint handle;
	OpenGLRenderbuffer *next; /* linked list */
};

struct OpenGLEffect /* Cast from FNA3D_Effect* */
{
	MOJOSHADER_effect *effect;
	MOJOSHADER_glEffect *glEffect;
	OpenGLEffect *next; /* linked list */
};

struct OpenGLQuery /* Cast from FNA3D_Query* */
{
	GLuint handle;
	OpenGLQuery *next; /* linked list */
};

typedef struct OpenGLBackbuffer
{
	#define BACKBUFFER_TYPE_NULL 0
	#define BACKBUFFER_TYPE_OPENGL 1
	uint8_t type;

	int32_t width;
	int32_t height;
	FNA3D_DepthFormat depthFormat;
	int32_t multiSampleCount;
	struct
	{
		GLuint handle;

		GLuint texture;
		GLuint colorAttachment;
		GLuint depthStencilAttachment;
	} opengl;
} OpenGLBackbuffer;

typedef struct OpenGLVertexAttribute
{
	uint32_t currentBuffer;
	void *currentPointer;
	FNA3D_VertexElementFormat currentFormat;
	uint8_t currentNormalized;
	uint32_t currentStride;
} OpenGLVertexAttribute;

typedef struct OpenGLDevice /* Cast from FNA3D_Renderer* */
{
	/* Associated FNA3D_Device */
	FNA3D_Device *parentDevice;

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

	/* Blend State */
	uint8_t alphaBlendEnable;
	FNA3D_Color blendColor;
	FNA3D_BlendFunction blendOp;
	FNA3D_BlendFunction blendOpAlpha;
	FNA3D_Blend srcBlend;
	FNA3D_Blend dstBlend;
	FNA3D_Blend srcBlendAlpha;
	FNA3D_Blend dstBlendAlpha;
	FNA3D_ColorWriteChannels colorWriteEnable;
	FNA3D_ColorWriteChannels colorWriteEnable1;
	FNA3D_ColorWriteChannels colorWriteEnable2;
	FNA3D_ColorWriteChannels colorWriteEnable3;
	int32_t multiSampleMask;

	/* Depth Stencil State */
	uint8_t zEnable;
	uint8_t zWriteEnable;
	FNA3D_CompareFunction depthFunc;
	uint8_t stencilEnable;
	int32_t stencilWriteMask;
	uint8_t separateStencilEnable;
	int32_t stencilRef;
	int32_t stencilMask;
	FNA3D_CompareFunction stencilFunc;
	FNA3D_StencilOperation stencilFail;
	FNA3D_StencilOperation stencilZFail;
	FNA3D_StencilOperation stencilPass;
	FNA3D_CompareFunction ccwStencilFunc;
	FNA3D_StencilOperation ccwStencilFail;
	FNA3D_StencilOperation ccwStencilZFail;
	FNA3D_StencilOperation ccwStencilPass;

	/* Rasterizer State */
	uint8_t scissorTestEnable;
	FNA3D_CullMode cullFrontFace;
	FNA3D_FillMode fillMode;
	float depthBias;
	float slopeScaleDepthBias;
	uint8_t multiSampleEnable;

	/* Viewport */
	FNA3D_Viewport viewport;
	FNA3D_Rect scissorRect;
	float depthRangeMin;
	float depthRangeMax;

	/* Textures */
	int32_t numTextureSlots;
	OpenGLTexture *textures[MAX_TEXTURE_SAMPLERS];

	/* Buffer Binding Cache */
	GLuint currentVertexBuffer;
	GLuint currentIndexBuffer;

	/* ld, or LastDrawn, effect/vertex attributes */
	int32_t ldBaseVertex;
	FNA3D_VertexDeclaration *ldVertexDeclaration;
	void* ldPointer;
	MOJOSHADER_glEffect *ldEffect;
	MOJOSHADER_effectTechnique *ldTechnique;
	uint32_t ldPass;

	/* Some vertex declarations may have overlapping attributes :/ */
	uint8_t attrUse[MOJOSHADER_USAGE_TOTAL][16];

	/* Render Targets */
	int32_t numAttachments;
	GLuint currentReadFramebuffer;
	GLuint currentDrawFramebuffer;
	GLuint targetFramebuffer;
	GLuint resolveFramebufferRead;
	GLuint resolveFramebufferDraw;
	GLuint currentAttachments[MAX_RENDERTARGET_BINDINGS];
	GLenum currentAttachmentTypes[MAX_RENDERTARGET_BINDINGS];
	int32_t currentDrawBuffers;
	GLenum drawBuffersArray[MAX_RENDERTARGET_BINDINGS + 2];
	GLuint currentRenderbuffer;
	FNA3D_DepthFormat currentDepthStencilFormat;
	GLuint attachments[MAX_RENDERTARGET_BINDINGS];
	GLenum attachmentTypes[MAX_RENDERTARGET_BINDINGS];

	/* Clear Cache */
	FNA3D_Vec4 currentClearColor;
	float currentClearDepth;
	int32_t currentClearStencil;

	/* Vertex Attributes */
	int32_t numVertexAttributes;
	OpenGLVertexAttribute attributes[MAX_VERTEX_ATTRIBUTES];
	uint8_t attributeEnabled[MAX_VERTEX_ATTRIBUTES];
	uint8_t previousAttributeEnabled[MAX_VERTEX_ATTRIBUTES];
	int32_t attributeDivisor[MAX_VERTEX_ATTRIBUTES];
	int32_t previousAttributeDivisor[MAX_VERTEX_ATTRIBUTES];

	/* MojoShader Interop */
	const char *shaderProfile;
	MOJOSHADER_glContext *shaderContext;
	MOJOSHADER_glEffect *currentEffect;
	MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;
	uint8_t renderTargetBound;
	uint8_t effectApplied;

	/* Point Sprite Toggle */
	uint8_t togglePointSprite;

	/* Threading */
	SDL_threadID threadID;
	FNA3D_Command *commands;
	SDL_mutex *commandsLock;
	OpenGLTexture *disposeTextures;
	SDL_mutex *disposeTexturesLock;
	OpenGLRenderbuffer *disposeRenderbuffers;
	SDL_mutex *disposeRenderbuffersLock;
	OpenGLBuffer *disposeVertexBuffers;
	SDL_mutex *disposeVertexBuffersLock;
	OpenGLBuffer *disposeIndexBuffers;
	SDL_mutex *disposeIndexBuffersLock;
	OpenGLEffect *disposeEffects;
	SDL_mutex *disposeEffectsLock;
	OpenGLQuery *disposeQueries;
	SDL_mutex *disposeQueriesLock;

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

/* Linked Lists */

#define LinkedList_Add(start, toAdd, curr) \
	toAdd->next = NULL; \
	if (start == NULL) \
	{ \
		start = toAdd; \
	} \
	else \
	{ \
		curr = start; \
		while (curr->next != NULL) \
		{ \
			curr = curr->next; \
		} \
		curr->next = toAdd; \
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

static inline void BindTexture(OpenGLDevice *device, OpenGLTexture* tex)
{
	if (tex->target != device->textures[0]->target)
	{
		device->glBindTexture(device->textures[0]->target, 0);
	}
	if (device->textures[0] != tex)
	{
		device->glBindTexture(tex->target, tex->handle);
	}
	device->textures[0] = tex;
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

static inline void ToggleGLState(
	OpenGLDevice *device,
	GLenum feature,
	uint8_t enable
) {
	if (enable)
	{
		device->glEnable(feature);
	}
	else
	{
		device->glDisable(feature);
	}
}

static inline void ForceToMainThread(
	OpenGLDevice *device,
	FNA3D_Command *command
) {
	FNA3D_Command *curr;
	SDL_LockMutex(device->commandsLock);
	LinkedList_Add(device->commands, command, curr);
	SDL_UnlockMutex(device->commandsLock);
}

/* Forward Declarations for Internal Functions */

static void OPENGL_INTERNAL_CreateBackbuffer(
	OpenGLDevice *device,
	FNA3D_PresentationParameters *parameters
);
static void OPENGL_INTERNAL_DisposeBackbuffer(OpenGLDevice *device);
static void OPENGL_INTERNAL_DestroyTexture(
	OpenGLDevice *device,
	OpenGLTexture *texture
);
static void OPENGL_INTERNAL_DestroyRenderbuffer(
	OpenGLDevice *device,
	OpenGLRenderbuffer *renderbuffer
);
static void OPENGL_INTERNAL_DestroyVertexBuffer(
	OpenGLDevice *device,
	OpenGLBuffer *buffer
);
static void OPENGL_INTERNAL_DestroyIndexBuffer(
	OpenGLDevice *device,
	OpenGLBuffer *buffer
);
static void OPENGL_INTERNAL_DestroyEffect(
	OpenGLDevice *device,
	OpenGLEffect *effect
);
static void OPENGL_INTERNAL_DestroyQuery(
	OpenGLDevice *device,
	OpenGLQuery *query
);

/* Device Implementation */

/* Quit */

void OPENGL_DestroyDevice(FNA3D_Device *device)
{
	OpenGLDevice *glDevice = (OpenGLDevice*) device->driverData;
	FNA3D_Command *curr, *next;

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

	SDL_DestroyMutex(glDevice->commandsLock);
	SDL_DestroyMutex(glDevice->disposeTexturesLock);
	SDL_DestroyMutex(glDevice->disposeRenderbuffersLock);
	SDL_DestroyMutex(glDevice->disposeVertexBuffersLock);
	SDL_DestroyMutex(glDevice->disposeIndexBuffersLock);
	SDL_DestroyMutex(glDevice->disposeEffectsLock);
	SDL_DestroyMutex(glDevice->disposeQueriesLock);

	SDL_GL_DeleteContext(glDevice->context);

	SDL_free(glDevice);
	SDL_free(device);
}

/* Begin/End Frame */

void OPENGL_BeginFrame(FNA3D_Renderer *driverData)
{
	/* No-op */
}

static void ExecuteCommands(OpenGLDevice *device)
{
	FNA3D_Command *cmd;

	SDL_LockMutex(device->commandsLock);
	cmd = device->commands;
	while (cmd != NULL)
	{
		FNA3D_ExecuteCommand(
			device->parentDevice,
			cmd
		);
		SDL_SemPost(cmd->semaphore);
		cmd = cmd->next;
	}
	device->commands = NULL; /* No heap memory to free! -caleb */
	SDL_UnlockMutex(device->commandsLock);
}

static void DisposeResources(OpenGLDevice *device)
{
	OpenGLTexture *tex, *texNext;
	OpenGLEffect *eff, *effNext;
	OpenGLBuffer *buf, *bufNext;
	OpenGLRenderbuffer *ren, *renNext;
	OpenGLQuery *qry, *qryNext;

	/* All heap allocations are freed by func! -caleb */
	#define DISPOSE(prefix, list, func) \
		SDL_LockMutex(list##Lock); \
		prefix = list; \
		while (prefix != NULL) \
		{ \
			prefix##Next = prefix->next; \
			OPENGL_INTERNAL_##func(device, prefix); \
			prefix = prefix##Next; \
		} \
		list = NULL; \
		SDL_UnlockMutex(list##Lock);

	DISPOSE(tex, device->disposeTextures, DestroyTexture)
	DISPOSE(ren, device->disposeRenderbuffers, DestroyRenderbuffer)
	DISPOSE(buf, device->disposeVertexBuffers, DestroyVertexBuffer)
	DISPOSE(buf, device->disposeIndexBuffers, DestroyIndexBuffer)
	DISPOSE(eff, device->disposeEffects, DestroyEffect)
	DISPOSE(qry, device->disposeQueries, DestroyQuery)

	#undef DISPOSE
}

void OPENGL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	int32_t srcX, srcY, srcW, srcH;
	int32_t dstX, dstY, dstW, dstH;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	/* Only the faux-backbuffer supports presenting
	 * specific regions given to Present().
	 * -flibit
	 */
	if (device->backbuffer->type == BACKBUFFER_TYPE_OPENGL)
	{
		if (sourceRectangle != NULL)
		{
			srcX = sourceRectangle->x;
			srcY = sourceRectangle->y;
			srcW = sourceRectangle->w;
			srcH = sourceRectangle->h;
		}
		else
		{
			srcX = 0;
			srcY = 0;
			srcW = device->backbuffer->width;
			srcH = device->backbuffer->height;
		}
		if (destinationRectangle != NULL)
		{
			dstX = destinationRectangle->x;
			dstY = destinationRectangle->y;
			dstW = destinationRectangle->w;
			dstH = destinationRectangle->h;
		}
		else
		{
			dstX = 0;
			dstY = 0;
			SDL_GL_GetDrawableSize(
				(SDL_Window*) overrideWindowHandle,
				&dstW,
				&dstH
			);
		}

		if (device->scissorTestEnable)
		{
			device->glDisable(GL_SCISSOR_TEST);
		}

		if (	device->backbuffer->multiSampleCount > 0 &&
			(srcX != dstX || srcY != dstY || srcW != dstW || srcH != dstH)	)
		{
			/* We have to resolve the renderbuffer to a texture first.
			 * For whatever reason, we can't blit a multisample renderbuffer
			 * to the backbuffer. Not sure why, but oh well.
			 * -flibit
			 */
			if (device->backbuffer->opengl.texture == 0)
			{
				device->glGenTextures(1, &device->backbuffer->opengl.texture);
				device->glBindTexture(GL_TEXTURE_2D, device->backbuffer->opengl.texture);
				device->glTexImage2D(
					GL_TEXTURE_2D,
					0,
					GL_RGBA,
					device->backbuffer->width,
					device->backbuffer->height,
					0,
					GL_RGBA,
					GL_UNSIGNED_BYTE,
					NULL
				);
				device->glBindTexture(
					device->textures[0]->target,
					device->textures[0]->handle
				);
			}
			BindFramebuffer(device, device->resolveFramebufferDraw);
			device->glFramebufferTexture2D(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D,
				device->backbuffer->opengl.texture,
				0
			);
			BindReadFramebuffer(device, device->backbuffer->opengl.handle);
			device->glBlitFramebuffer(
				0, 0, device->backbuffer->width, device->backbuffer->height,
				0, 0, device->backbuffer->width, device->backbuffer->height,
				GL_COLOR_BUFFER_BIT,
				GL_LINEAR
			);
			/* Invalidate the MSAA faux-backbuffer */
			if (device->supports_ARB_invalidate_subdata)
			{
				device->glInvalidateFramebuffer(
					GL_READ_FRAMEBUFFER,
					device->numAttachments + 2,
					device->drawBuffersArray
				);
			}
			BindReadFramebuffer(device, device->resolveFramebufferDraw);
		}
		else
		{
			BindReadFramebuffer(device, device->backbuffer->opengl.handle);
		}
		BindDrawFramebuffer(device, device->realBackbufferFBO);

		device->glBlitFramebuffer(
			srcX, srcY, srcW, srcH,
			dstX, dstY, dstW, dstH,
			GL_COLOR_BUFFER_BIT,
			device->backbufferScaleMode
		);
		/* Invalidate the faux-backbuffer */
		if (device->supports_ARB_invalidate_subdata)
		{
			device->glInvalidateFramebuffer(
				GL_READ_FRAMEBUFFER,
				device->numAttachments + 2,
				device->drawBuffersArray
			);
		}

		BindFramebuffer(device, device->realBackbufferFBO);

		if (device->scissorTestEnable)
		{
			device->glEnable(GL_SCISSOR_TEST);
		}

		SDL_GL_SwapWindow((SDL_Window*) overrideWindowHandle);

		BindFramebuffer(device, device->backbuffer->opengl.handle);
	}
	else
	{
		/* Nothing left to do, just swap! */
		SDL_GL_SwapWindow((SDL_Window*) overrideWindowHandle);
	}

	/* Run any threaded commands */
	ExecuteCommands(device);

	/* Destroy any disposed resources */
	DisposeResources(device);
}

void OPENGL_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	const char *osVersion;
	int32_t disableLateSwapTear;

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
	FNA3D_Renderer *driverData,
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
	FNA3D_Renderer *driverData,
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
	FNA3D_Renderer *driverData,
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

void OPENGL_GetBackbufferSize(FNA3D_Renderer*, int*, int*);

void OPENGL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	int32_t bbw, bbh;

	/* Flip viewport when target is not bound */
	if (!device->renderTargetBound)
	{
		OPENGL_GetBackbufferSize(driverData, &bbw, &bbh);
		viewport->y = bbh - viewport->y - viewport->h;
	}

	if (	viewport->x != device->viewport.x ||
		viewport->y != device->viewport.y ||
		viewport->w != device->viewport.w ||
		viewport->h != device->viewport.h	)
	{
		device->viewport = *viewport;
		device->glViewport(
			viewport->x,
			viewport->y,
			viewport->w,
			viewport->h
		);
	}

	if (	viewport->minDepth != device->depthRangeMin ||
		viewport->maxDepth != device->depthRangeMax	)
	{
		device->depthRangeMin = viewport->minDepth;
		device->depthRangeMax = viewport->maxDepth;

		if (device->supports_DoublePrecisionDepth)
		{
			device->glDepthRange(
				(double) viewport->minDepth,
				(double) viewport->maxDepth
			);
		}
		else
		{
			device->glDepthRangef(
				viewport->minDepth,
				viewport->maxDepth
			);
		}
	}
}

void OPENGL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	int32_t bbw, bbh;

	/* Flip rectangle when target is not bound */
	if (!device->renderTargetBound)
	{
		OPENGL_GetBackbufferSize(driverData, &bbw, &bbh);
		scissor->y = bbh - scissor->y - scissor->h;
	}

	if (	scissor->x != device->scissorRect.x ||
		scissor->y != device->scissorRect.y ||
		scissor->w != device->scissorRect.w ||
		scissor->h != device->scissorRect.h	)
	{
		device->scissorRect = *scissor;
		device->glScissor(
			scissor->x,
			scissor->y,
			scissor->w,
			scissor->h
		);
	}
}

void OPENGL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_memcpy(blendFactor, &device->blendColor, sizeof(FNA3D_Color));
}

void OPENGL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	if (	device->blendColor.r != blendFactor->r ||
		device->blendColor.g != blendFactor->g ||
		device->blendColor.b != blendFactor->b ||
		device->blendColor.a != blendFactor->a	)
	{
		device->blendColor.r = blendFactor->r;
		device->blendColor.g = blendFactor->g;
		device->blendColor.b = blendFactor->b;
		device->blendColor.a = blendFactor->a;
		device->glBlendColor(
			device->blendColor.r / 255.0f,
			device->blendColor.g / 255.0f,
			device->blendColor.b / 255.0f,
			device->blendColor.a / 255.0f
		);
	}
}

int32_t OPENGL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_texture_multisample);
	return device->multiSampleMask;
}

void OPENGL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_texture_multisample);
	if (mask != device->multiSampleMask)
	{
		if (mask == -1)
		{
			device->glDisable(GL_SAMPLE_MASK);
		}
		else
		{
			if (device->multiSampleMask == -1)
			{
				device->glEnable(GL_SAMPLE_MASK);
			}
			/* FIXME: Index...? -flibit */
			device->glSampleMaski(0, (GLuint) mask);
		}
		device->multiSampleMask = mask;
	}
}

int32_t OPENGL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->stencilRef;
}

void OPENGL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	if (ref != device->stencilRef)
	{
		device->stencilRef = ref;
		if (device->separateStencilEnable)
		{
			device->glStencilFuncSeparate(
				GL_FRONT,
				XNAToGL_CompareFunc[device->stencilFunc],
				device->stencilRef,
				device->stencilMask
			);
			device->glStencilFuncSeparate(
				GL_BACK,
				XNAToGL_CompareFunc[device->stencilFunc],
				device->stencilRef,
				device->stencilMask
			);
		}
		else
		{
			device->glStencilFunc(
				XNAToGL_CompareFunc[device->stencilFunc],
				device->stencilRef,
				device->stencilMask
			);
		}
	}
}

/* Immutable Render States */

void OPENGL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	uint8_t newEnable = !(
		blendState->colorSourceBlend == FNA3D_BLEND_ONE &&
		blendState->colorDestinationBlend == FNA3D_BLEND_ZERO &&
		blendState->alphaSourceBlend == FNA3D_BLEND_ONE &&
		blendState->alphaDestinationBlend == FNA3D_BLEND_ZERO
	);

	if (newEnable != device->alphaBlendEnable)
	{
		device->alphaBlendEnable = newEnable;
		ToggleGLState(device, GL_BLEND, device->alphaBlendEnable);
	}

	if (device->alphaBlendEnable)
	{
		if (	blendState->blendFactor.r != device->blendColor.r ||
			blendState->blendFactor.g != device->blendColor.g ||
			blendState->blendFactor.b != device->blendColor.b ||
			blendState->blendFactor.a != device->blendColor.a	)
		{
			device->blendColor = blendState->blendFactor;
			device->glBlendColor(
				device->blendColor.r / 255.0f,
				device->blendColor.g / 255.0f,
				device->blendColor.b / 255.0f,
				device->blendColor.a / 255.0f
			);
		}

		if (	blendState->colorSourceBlend != device->srcBlend ||
			blendState->colorDestinationBlend != device->dstBlend ||
			blendState->alphaSourceBlend != device->srcBlendAlpha ||
			blendState->alphaDestinationBlend != device->dstBlendAlpha	)
		{
			device->srcBlend = blendState->colorSourceBlend;
			device->dstBlend = blendState->colorDestinationBlend;
			device->srcBlendAlpha = blendState->alphaSourceBlend;
			device->dstBlendAlpha = blendState->alphaDestinationBlend;
			device->glBlendFuncSeparate(
				XNAToGL_BlendMode[device->srcBlend],
				XNAToGL_BlendMode[device->dstBlend],
				XNAToGL_BlendMode[device->srcBlendAlpha],
				XNAToGL_BlendMode[device->dstBlendAlpha]
			);
		}

		if (	blendState->colorBlendFunction != device->blendOp ||
			blendState->alphaBlendFunction != device->blendOpAlpha	)
		{
			device->blendOp = blendState->colorBlendFunction;
			device->blendOpAlpha = blendState->alphaBlendFunction;
			device->glBlendEquationSeparate(
				XNAToGL_BlendEquation[device->blendOp],
				XNAToGL_BlendEquation[device->blendOpAlpha]
			);
		}
	}

	if (blendState->colorWriteEnable != device->colorWriteEnable)
	{
		device->colorWriteEnable = blendState->colorWriteEnable;
		device->glColorMask(
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(device->colorWriteEnable & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	/* FIXME: So how exactly do we factor in
	 * COLORWRITEENABLE for buffer 0? Do we just assume that
	 * the default is just buffer 0, and all other calls
	 * update the other write masks afterward? Or do we
	 * assume that COLORWRITEENABLE only touches 0, and the
	 * other 3 buffers are left alone unless we don't have
	 * EXT_draw_buffers2?
	 * -flibit
	 */
	if (blendState->colorWriteEnable1 != device->colorWriteEnable1)
	{
		device->colorWriteEnable1 = blendState->colorWriteEnable1;
		device->glColorMaski(
			1,
			(device->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(device->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(device->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(device->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (blendState->colorWriteEnable2 != device->colorWriteEnable2)
	{
		device->colorWriteEnable2 = blendState->colorWriteEnable2;
		device->glColorMaski(
			2,
			(device->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(device->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(device->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(device->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (blendState->colorWriteEnable3 != device->colorWriteEnable3)
	{
		device->colorWriteEnable3 = blendState->colorWriteEnable3;
		device->glColorMaski(
			3,
			(device->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(device->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(device->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(device->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}

	if (blendState->multiSampleMask != device->multiSampleMask)
	{
		if (blendState->multiSampleMask == -1)
		{
			device->glDisable(GL_SAMPLE_MASK);
		}
		else
		{
			if (device->multiSampleMask == -1)
			{
				device->glEnable(GL_SAMPLE_MASK);
			}
			/* FIXME: index...? -flibit */
			device->glSampleMaski(0, (uint32_t) blendState->multiSampleMask);
		}
		device->multiSampleMask = blendState->multiSampleMask;
	}
}

void OPENGL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	if (depthStencilState->depthBufferEnable != device->zEnable)
	{
		device->zEnable = depthStencilState->depthBufferEnable;
		ToggleGLState(device, GL_DEPTH_TEST, device->zEnable);
	}

	if (device->zEnable)
	{
		if (depthStencilState->depthBufferWriteEnable != device->zWriteEnable)
		{
			device->zWriteEnable = depthStencilState->depthBufferWriteEnable;
			device->glDepthMask(device->zWriteEnable);
		}

		if (depthStencilState->depthBufferFunction != device->depthFunc)
		{
			device->depthFunc = depthStencilState->depthBufferFunction;
			device->glDepthFunc(XNAToGL_CompareFunc[device->depthFunc]);
		}
	}

	if (depthStencilState->stencilEnable != device->stencilEnable)
	{
		device->stencilEnable = depthStencilState->stencilEnable;
		ToggleGLState(device, GL_STENCIL_TEST, device->stencilEnable);
	}

	if (device->stencilEnable)
	{
		if (depthStencilState->stencilWriteMask != device->stencilWriteMask)
		{
			device->stencilWriteMask = depthStencilState->stencilWriteMask;
			device->glStencilMask(device->stencilWriteMask);
		}

		/* TODO: Can we split up StencilFunc/StencilOp nicely? -flibit */
		if (	depthStencilState->twoSidedStencilMode != device->separateStencilEnable ||
			depthStencilState->referenceStencil != device->stencilRef ||
			depthStencilState->stencilMask != device->stencilMask ||
			depthStencilState->stencilFunction != device->stencilFunc ||
			depthStencilState->ccwStencilFunction != device->ccwStencilFunc ||
			depthStencilState->stencilFail != device->stencilFail ||
			depthStencilState->stencilDepthBufferFail != device->stencilZFail ||
			depthStencilState->stencilPass != device->stencilPass ||
			depthStencilState->ccwStencilFail != device->ccwStencilFail ||
			depthStencilState->ccwStencilDepthBufferFail != device->ccwStencilZFail ||
			depthStencilState->ccwStencilPass != device->ccwStencilPass			)
		{
			device->separateStencilEnable = depthStencilState->twoSidedStencilMode;
			device->stencilRef = depthStencilState->referenceStencil;
			device->stencilMask = depthStencilState->stencilMask;
			device->stencilFunc = depthStencilState->stencilFunction;
			device->stencilFail = depthStencilState->stencilFail;
			device->stencilZFail = depthStencilState->stencilDepthBufferFail;
			device->stencilPass = depthStencilState->stencilPass;
			if (device->separateStencilEnable)
			{
				device->ccwStencilFunc = depthStencilState->ccwStencilFunction;
				device->ccwStencilFail = depthStencilState->ccwStencilFail;
				device->ccwStencilZFail = depthStencilState->ccwStencilDepthBufferFail;
				device->ccwStencilPass = depthStencilState->ccwStencilPass;
				device->glStencilFuncSeparate(
					GL_FRONT,
					XNAToGL_CompareFunc[device->stencilFunc],
					device->stencilRef,
					device->stencilMask
				);
				device->glStencilFuncSeparate(
					GL_BACK,
					XNAToGL_CompareFunc[device->ccwStencilFunc],
					device->stencilRef,
					device->stencilMask
				);
				device->glStencilOpSeparate(
					GL_FRONT,
					XNAToGL_GLStencilOp[device->stencilFail],
					XNAToGL_GLStencilOp[device->stencilZFail],
					XNAToGL_GLStencilOp[device->stencilPass]
				);
				device->glStencilOpSeparate(
					GL_BACK,
					XNAToGL_GLStencilOp[device->ccwStencilFail],
					XNAToGL_GLStencilOp[device->ccwStencilZFail],
					XNAToGL_GLStencilOp[device->ccwStencilPass]
				);
			}
			else
			{
				device->glStencilFunc(
					XNAToGL_CompareFunc[device->stencilFunc],
					device->stencilRef,
					device->stencilMask
				);
				device->glStencilOp(
					XNAToGL_GLStencilOp[device->stencilFail],
					XNAToGL_GLStencilOp[device->stencilZFail],
					XNAToGL_GLStencilOp[device->stencilPass]
				);
			}
		}
	}
}

void OPENGL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	FNA3D_CullMode actualMode;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != device->scissorTestEnable)
	{
		device->scissorTestEnable = rasterizerState->scissorTestEnable;
		ToggleGLState(device, GL_SCISSOR_TEST, device->scissorTestEnable);
	}

	if (device->renderTargetBound)
	{
		actualMode = rasterizerState->cullMode;
	}
	else
	{
		/* When not rendering offscreen the faces change order. */
		if (rasterizerState->cullMode == FNA3D_CULLMODE_NONE)
		{
			actualMode = rasterizerState->cullMode;
		}
		else
		{
			actualMode = (
				rasterizerState->cullMode == FNA3D_CULLMODE_CULLCLOCKWISEFACE ?
					FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE :
					FNA3D_CULLMODE_CULLCLOCKWISEFACE
			);
		}
	}
	if (actualMode != device->cullFrontFace)
	{
		if ((actualMode == FNA3D_CULLMODE_NONE) != (device->cullFrontFace == FNA3D_CULLMODE_NONE))
		{
			ToggleGLState(device, GL_CULL_FACE, actualMode != FNA3D_CULLMODE_NONE);
		}
		device->cullFrontFace = actualMode;
		if (device->cullFrontFace != FNA3D_CULLMODE_NONE)
		{
			device->glFrontFace(XNAToGL_FrontFace[device->cullFrontFace]);
		}
	}

	if (rasterizerState->fillMode != device->fillMode)
	{
		device->fillMode = rasterizerState->fillMode;
		device->glPolygonMode(
			GL_FRONT_AND_BACK,
			XNAToGL_GLFillMode[device->fillMode]
		);
	}

	realDepthBias = rasterizerState->depthBias * XNAToGL_DepthBiasScale[
		device->renderTargetBound ?
			device->currentDepthStencilFormat :
			device->backbuffer->depthFormat
	];
	if (	realDepthBias != device->depthBias ||
		rasterizerState->slopeScaleDepthBias != device->slopeScaleDepthBias	)
	{
		if (	realDepthBias == 0.0f &&
			rasterizerState->slopeScaleDepthBias == 0.0f)
		{
			/* We're changing to disabled bias, disable! */
			device->glDisable(GL_POLYGON_OFFSET_FILL);
		}
		else
		{
			if (device->depthBias == 0.0f && device->slopeScaleDepthBias == 0.0f)
			{
				/* We're changing away from disabled bias, enable! */
				device->glEnable(GL_POLYGON_OFFSET_FILL);
			}
			device->glPolygonOffset(
				rasterizerState->slopeScaleDepthBias,
				realDepthBias
			);
		}
		device->depthBias = realDepthBias;
		device->slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
	}

	/* If you're reading this, you have a user with broken MSAA!
	 * Here's the deal: On all modern drivers this should work,
	 * but there was a period of time where, for some reason,
	 * IHVs all took a nap and decided that they didn't have to
	 * respect GL_MULTISAMPLE toggles. A couple sources:
	 *
	 * https://developer.apple.com/library/content/documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_fsaa/opengl_fsaa.html
	 *
	 * https://www.opengl.org/discussion_boards/showthread.php/172025-glDisable(GL_MULTISAMPLE)-has-no-effect
	 *
	 * So yeah. Have em update their driver. If they're on Intel,
	 * tell them to install Linux. Yes, really.
	 * -flibit
	 */
	if (rasterizerState->multiSampleAntiAlias != device->multiSampleEnable)
	{
		device->multiSampleEnable = rasterizerState->multiSampleAntiAlias;
		ToggleGLState(device, GL_MULTISAMPLE, device->multiSampleEnable);
	}
}

void OPENGL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *tex = (OpenGLTexture*) texture;

	if (texture == NULL)
	{
		if (device->textures[index] != &NullTexture)
		{
			if (index != 0)
			{
				device->glActiveTexture(GL_TEXTURE0 + index);
			}
			device->glBindTexture(device->textures[index]->target, 0);
			if (index != 0)
			{
				/* Keep this state sane. -flibit */
				device->glActiveTexture(GL_TEXTURE0);
			}
			device->textures[index] = &NullTexture;
		}
		return;
	}

	if (	tex == device->textures[index] &&
		sampler->addressU == tex->wrapS &&
		sampler->addressV == tex->wrapT &&
		sampler->addressW == tex->wrapR &&
		sampler->filter == tex->filter &&
		sampler->maxAnisotropy == tex->anisotropy &&
		sampler->maxMipLevel == tex->maxMipmapLevel &&
		sampler->mipMapLevelOfDetailBias == tex->lodBias	)
	{
		/* Nothing's changing, forget it. */
		return;
	}

	/* Set the active texture slot */
	if (index != 0)
	{
		device->glActiveTexture(GL_TEXTURE0 + index);
	}

	/* Bind the correct texture */
	if (tex != device->textures[index])
	{
		if (tex->target != device->textures[index]->target)
		{
			/* If we're changing targets, unbind the old texture first! */
			device->glBindTexture(device->textures[index]->target, 0);
		}
		device->glBindTexture(tex->target, tex->handle);
		device->textures[index] = tex;
	}

	/* Apply the sampler states to the GL texture */
	if (sampler->addressU != tex->wrapS)
	{
		tex->wrapS = sampler->addressU;
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_WRAP_S,
			XNAToGL_Wrap[tex->wrapS]
		);
	}
	if (sampler->addressV != tex->wrapT)
	{
		tex->wrapT = sampler->addressV;
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_WRAP_T,
			XNAToGL_Wrap[tex->wrapT]
		);
	}
	if (sampler->addressW != tex->wrapR)
	{
		tex->wrapR = sampler->addressW;
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_WRAP_R,
			XNAToGL_Wrap[tex->wrapR]
		);
	}
	if (	sampler->filter != tex->filter ||
		sampler->maxAnisotropy != tex->anisotropy	)
	{
		tex->filter = sampler->filter;
		tex->anisotropy = (float) sampler->maxAnisotropy;
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_MAG_FILTER,
			XNAToGL_MagFilter[tex->filter]
		);
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_MIN_FILTER,
			tex->hasMipmaps ?
				XNAToGL_MinMipFilter[tex->filter] :
				XNAToGL_MinFilter[tex->filter]
		);
		device->glTexParameterf(
			tex->target,
			GL_TEXTURE_MAX_ANISOTROPY_EXT,
			(tex->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC) ?
				SDL_max(tex->anisotropy, 1.0f) :
				1.0f
		);
	}
	if (sampler->maxMipLevel != tex->maxMipmapLevel)
	{
		tex->maxMipmapLevel = sampler->maxMipLevel;
		device->glTexParameteri(
			tex->target,
			GL_TEXTURE_BASE_LEVEL,
			tex->maxMipmapLevel
		);
	}
	if (sampler->mipMapLevelOfDetailBias != tex->lodBias && !device->useES3)
	{
		tex->lodBias = sampler->mipMapLevelOfDetailBias;
		device->glTexParameterf(
			tex->target,
			GL_TEXTURE_LOD_BIAS,
			tex->lodBias
		);
	}

	if (index != 0)
	{
		/* Keep this state sane. -flibit */
		device->glActiveTexture(GL_TEXTURE0);
	}
}

/* Vertex State */

static void OPENGL_INTERNAL_FlushGLVertexAttributes(OpenGLDevice *device)
{
	int32_t i, divisor;
	for (i = 0; i < device->numVertexAttributes; i += 1)
	{
		if (device->attributeEnabled[i])
		{
			device->attributeEnabled[i] = 0;
			if (!device->previousAttributeEnabled[i])
			{
				device->glEnableVertexAttribArray(i);
				device->previousAttributeEnabled[i] = 1;
			}
		}
		else if (device->previousAttributeEnabled[i])
		{
			device->glDisableVertexAttribArray(i);
			device->previousAttributeEnabled[i] = 0;
		}

		divisor = device->attributeDivisor[i];
		if (divisor != device->previousAttributeDivisor[i])
		{
			device->glVertexAttribDivisor(i, divisor);
			device->previousAttributeDivisor[i] = divisor;
		}
	}
}

void OPENGL_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	uint8_t *basePtr, *ptr;
	uint8_t normalized;
	int32_t i, j, k;
	int32_t usage, index, attribLoc;
	FNA3D_VertexElement *element;
	FNA3D_VertexDeclaration *vertexDeclaration;
	OpenGLVertexAttribute *attr;
	OpenGLBuffer *buffer;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	if (device->supports_ARB_draw_elements_base_vertex)
	{
		baseVertex = 0;
	}

	if (	bindingsUpdated ||
		baseVertex != device->ldBaseVertex ||
		device->currentEffect != device->ldEffect ||
		device->currentTechnique != device->ldTechnique ||
		device->currentPass != device->ldPass ||
		device->effectApplied	)
	{
		/* There's this weird case where you can have overlapping
		 * vertex usage/index combinations. It seems like the first
		 * attrib gets priority, so whenever a duplicate attribute
		 * exists, give it the next available index. If that fails, we
		 * have to crash :/
		 * -flibit
		 */
		SDL_memset(device->attrUse, '\0', sizeof(device->attrUse));
		for (i = 0; i < numBindings; i += 1)
		{
			buffer = (OpenGLBuffer*) bindings[i].vertexBuffer;
			BindVertexBuffer(device, buffer->handle);
			vertexDeclaration = &bindings[i].vertexDeclaration;
			basePtr = (uint8_t*) (size_t) (
				vertexDeclaration->vertexStride *
				(bindings[i].vertexOffset + baseVertex)
			);
			for (j = 0; j < vertexDeclaration->elementCount; j += 1)
			{
				element = &vertexDeclaration->elements[j];
				usage = element->vertexElementUsage;
				index = element->usageIndex;
				if (device->attrUse[usage][index])
				{
					index = -1;
					for (k = 0; k < 16; k += 1)
					{
						if (!device->attrUse[usage][k])
						{
							index = k;
							break;
						}
					}
					if (index < 0)
					{
						SDL_LogError(
							SDL_LOG_CATEGORY_APPLICATION,
							"Vertex usage collision!"
						);
					}
				}
				device->attrUse[usage][index] = 1;
				attribLoc = MOJOSHADER_glGetVertexAttribLocation(
					XNAToGL_VertexAttribUsage[usage],
					index
				);
				if (attribLoc == -1)
				{
					/* Stream not in use! */
					continue;
				}
				device->attributeEnabled[attribLoc] = 1;
				attr = &device->attributes[attribLoc];
				ptr = basePtr + element->offset;
				normalized = XNAToGL_VertexAttribNormalized(element);
				if (	attr->currentBuffer != buffer->handle ||
					attr->currentPointer != ptr ||
					attr->currentFormat != element->vertexElementFormat ||
					attr->currentNormalized != normalized ||
					attr->currentStride != vertexDeclaration->vertexStride	)
				{
					device->glVertexAttribPointer(
						attribLoc,
						XNAToGL_VertexAttribSize[element->vertexElementFormat],
						XNAToGL_VertexAttribType[element->vertexElementFormat],
						normalized,
						vertexDeclaration->vertexStride,
						ptr
					);
					attr->currentBuffer = buffer->handle;
					attr->currentPointer = ptr;
					attr->currentFormat = element->vertexElementFormat;
					attr->currentNormalized = normalized;
					attr->currentStride = vertexDeclaration->vertexStride;
				}
				if (device->supports_ARB_instanced_arrays)
				{
					device->attributeDivisor[attribLoc] = bindings[i].instanceFrequency;
				}
			}
		}
		OPENGL_INTERNAL_FlushGLVertexAttributes(device);

		device->ldBaseVertex = baseVertex;
		device->ldEffect = device->currentEffect;
		device->ldTechnique = device->currentTechnique;
		device->ldPass = device->currentPass;
		device->effectApplied = 0;
		device->ldVertexDeclaration = NULL;
		device->ldPointer = NULL;
	}

	MOJOSHADER_glProgramReady();
	MOJOSHADER_glProgramViewportInfo(
		device->viewport.w, device->viewport.h,
		device->backbuffer->width, device->backbuffer->height,
		device->renderTargetBound
	);
}

void OPENGL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	int32_t usage, index, attribLoc, i, j;
	FNA3D_VertexElement *element;
	OpenGLVertexAttribute *attr;
	uint8_t normalized;
	uint8_t *finalPtr;
	uint8_t *basePtr = (uint8_t*) ptr;
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	BindVertexBuffer(device, 0);
	basePtr += (vertexDeclaration->vertexStride * vertexOffset);

	if (	vertexDeclaration != device->ldVertexDeclaration ||
		basePtr != device->ldPointer ||
		device->currentEffect != device->ldEffect ||
		device->currentTechnique != device->ldTechnique ||
		device->currentPass != device->ldPass ||
		device->effectApplied	)
	{
		/* There's this weird case where you can have overlapping
		 * vertex usage/index combinations. It seems like the first
		 * attrib gets priority, so whenever a duplicate attribute
		 * exists, give it the next available index. If that fails, we
		 * have to crash :/
		 * -flibit
		 */
		SDL_memset(device->attrUse, '\0', sizeof(device->attrUse));
		for (i = 0; i < vertexDeclaration->elementCount; i += 1)
		{
			element = &vertexDeclaration->elements[i];
			usage = element->vertexElementUsage;
			index = element->usageIndex;
			if (device->attrUse[usage][index])
			{
				index = -1;
				for (j = 0; j < 16; j += 1)
				{
					if (!device->attrUse[usage][j])
					{
						index = j;
						break;
					}
				}
				if (index < 0)
				{
					SDL_LogError(
						SDL_LOG_CATEGORY_APPLICATION,
						"Vertex usage collision!"
					);
				}
			}
			device->attrUse[usage][index] = 1;
			attribLoc = MOJOSHADER_glGetVertexAttribLocation(
				XNAToGL_VertexAttribUsage[usage],
				index
			);
			if (attribLoc == -1)
			{
				/* Stream not used! */
				continue;
			}
			device->attributeEnabled[attribLoc] = 1;
			attr = &device->attributes[attribLoc];
			finalPtr = basePtr + element->offset;
			normalized = XNAToGL_VertexAttribNormalized(element);
			if (	attr->currentBuffer != 0 ||
				attr->currentPointer != finalPtr ||
				attr->currentFormat != element->vertexElementFormat ||
				attr->currentNormalized != normalized ||
				attr->currentStride != vertexDeclaration->vertexStride	)
			{
				device->glVertexAttribPointer(
					attribLoc,
					XNAToGL_VertexAttribSize[element->vertexElementFormat],
					XNAToGL_VertexAttribType[element->vertexElementFormat],
					normalized,
					vertexDeclaration->vertexStride,
					finalPtr
				);
				attr->currentBuffer = 0;
				attr->currentPointer = finalPtr;
				attr->currentFormat = element->vertexElementFormat;
				attr->currentNormalized = normalized;
				attr->currentStride = vertexDeclaration->vertexStride;
			}
			device->attributeDivisor[attribLoc] = 0;
		}
		OPENGL_INTERNAL_FlushGLVertexAttributes(device);

		device->ldVertexDeclaration = vertexDeclaration;
		device->ldPointer = ptr;
		device->ldEffect = device->currentEffect;
		device->ldTechnique = device->currentTechnique;
		device->ldPass = device->currentPass;
		device->effectApplied = 0;
		device->ldBaseVertex = -1;
	}

	MOJOSHADER_glProgramReady();
	MOJOSHADER_glProgramViewportInfo(
		device->viewport.w, device->viewport.h,
		device->backbuffer->width, device->backbuffer->height,
		device->renderTargetBound
	);
}

/* Render Targets */

void OPENGL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLRenderbuffer *rb = (OpenGLRenderbuffer*) renderbuffer;
	FNA3D_RenderTargetBinding rt;
	int32_t i;
	GLuint handle;

	/* Bind the right framebuffer, if needed */
	if (renderTargets == NULL)
	{
		BindFramebuffer(
			device,
			device->backbuffer->type == BACKBUFFER_TYPE_OPENGL ?
				device->backbuffer->opengl.handle :
				device->realBackbufferFBO
		);
		device->renderTargetBound = 0;
		return;
	}
	else
	{
		BindFramebuffer(device, device->targetFramebuffer);
		device->renderTargetBound = 1;
	}

	for (i = 0; i < numRenderTargets; i += 1)
	{
		rt = renderTargets[i];
		if (rt.colorBuffer != NULL)
		{
			device->attachments[i] = ((OpenGLRenderbuffer*) rt.colorBuffer)->handle;
			device->attachmentTypes[i] = GL_RENDERBUFFER;
		}
		else
		{
			device->attachments[i] = ((OpenGLTexture*) rt.texture)->handle;
			if (rt.type == RENDERTARGET_TYPE_2D)
			{
				device->attachmentTypes[i] = GL_TEXTURE_2D;
			}
			else
			{
				device->attachmentTypes[i] = GL_TEXTURE_CUBE_MAP_POSITIVE_X + rt.cubeMapFace;
			}
		}
	}

	/* Update the color attachments, DrawBuffers state */
	for (i = 0; i < numRenderTargets; i += 1)
	{
		if (device->attachments[i] != device->currentAttachments[i])
		{
			if (device->currentAttachments[i] != 0)
			{
				if (	device->attachmentTypes[i] != GL_RENDERBUFFER &&
					device->currentAttachmentTypes[i] == GL_RENDERBUFFER	)
				{
					device->glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0 + i,
						GL_RENDERBUFFER,
						0
					);
				}
				else if (	device->attachmentTypes[i] == GL_RENDERBUFFER &&
						device->currentAttachmentTypes[i] != GL_RENDERBUFFER	)
				{
					device->glFramebufferTexture2D(
						GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0 + i,
						device->currentAttachmentTypes[i],
						0,
						0
					);
				}
			}
			if (device->attachmentTypes[i] == GL_RENDERBUFFER)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0 + i,
					GL_RENDERBUFFER,
					device->attachments[i]
				);
			}
			else
			{
				device->glFramebufferTexture2D(
					GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0 + i,
					device->attachmentTypes[i],
					device->attachments[i],
					0
				);
			}
			device->currentAttachments[i] = device->attachments[i];
			device->currentAttachmentTypes[i] = device->attachmentTypes[i];
		}
		else if (device->attachmentTypes[i] != device->currentAttachmentTypes[i])
		{
			/* Texture cube face change! */
			device->glFramebufferTexture2D(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0 + i,
				device->attachmentTypes[i],
				device->attachments[i],
				0
			);
			device->currentAttachmentTypes[i] = device->attachmentTypes[i];
		}
	}
	while (i < device->numAttachments)
	{
		if (device->currentAttachments[i] != 0)
		{
			if (device->currentAttachmentTypes[i] == GL_RENDERBUFFER)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0 + i,
					GL_RENDERBUFFER,
					0
				);
			}
			else
			{
				device->glFramebufferTexture2D(
					GL_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0 + i,
					device->currentAttachmentTypes[i],
					0,
					0
				);
			}
			device->currentAttachments[i] = 0;
			device->currentAttachmentTypes[i] = GL_TEXTURE_2D;
		}
		i += 1;
	}
	if (numRenderTargets != device->currentDrawBuffers)
	{
		device->glDrawBuffers(numRenderTargets, device->drawBuffersArray);
		device->currentDrawBuffers = numRenderTargets;
	}

	/* Update the depth/stencil attachment */
	/* FIXME: Notice that we do separate attach calls for the stencil.
	 * We _should_ be able to do a single attach for depthstencil, but
	 * some drivers (like Mesa) cannot into GL_DEPTH_STENCIL_ATTACHMENT.
	 * Use XNAToGL.DepthStencilAttachment when this isn't a problem.
	 * -flibit
	 */
	if (renderbuffer == NULL)
	{
		handle = 0;
	}
	else
	{
		handle = rb->handle;
	}
	if (handle != device->currentRenderbuffer)
	{
		if (device->currentDepthStencilFormat == FNA3D_DEPTHFORMAT_D24S8)
		{
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER,
				0
			);
		}
		device->currentDepthStencilFormat = depthFormat;
		device->glFramebufferRenderbuffer(
			GL_FRAMEBUFFER,
			GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER,
			handle
		);
		if (device->currentDepthStencilFormat == FNA3D_DEPTHFORMAT_D24S8)
		{
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER,
				handle
			);
		}
		device->currentRenderbuffer = handle;
	}
}

void OPENGL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	int32_t width, height;
	GLuint prevBuffer;
	OpenGLTexture *prevTex;
	OpenGLTexture *rtTex = (OpenGLTexture*) target->texture;
	GLenum textureTarget = (
		target->type == RENDERTARGET_TYPE_2D ?
			GL_TEXTURE_2D :
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + target->cubeMapFace
	);

	if (target->multiSampleCount > 0)
	{
		prevBuffer = device->currentDrawFramebuffer;

		/* Set up the texture framebuffer */
		width = target->width;
		height = target->height;
		BindFramebuffer(device, device->resolveFramebufferDraw);
		device->glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			textureTarget,
			rtTex->handle,
			0
		);

		/* Set up the renderbuffer framebuffer */
		BindFramebuffer(device, device->resolveFramebufferRead);
		device->glFramebufferRenderbuffer(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER,
			((OpenGLRenderbuffer*) target->colorBuffer)->handle
		);

		/* Blit! */
		if (device->scissorTestEnable)
		{
			device->glDisable(GL_SCISSOR_TEST);
		}
		BindDrawFramebuffer(device, device->resolveFramebufferDraw);
		device->glBlitFramebuffer(
			0, 0, width, height,
			0, 0, width, height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR
		);
		/* Invalidate the MSAA buffer */
		if (device->supports_ARB_invalidate_subdata)
		{
			device->glInvalidateFramebuffer(
				GL_READ_FRAMEBUFFER,
				device->numAttachments + 2,
				device->drawBuffersArray
			);
		}
		if (device->scissorTestEnable)
		{
			device->glEnable(GL_SCISSOR_TEST);
		}

		BindFramebuffer(device, prevBuffer);
	}

	/* If the target has mipmaps, regenerate them now */
	if (target->levelCount > 1)
	{
		prevTex = device->textures[0];
		BindTexture(device, rtTex);
		device->glGenerateMipmap(textureTarget);
		BindTexture(device, prevTex);
	}
}

/* Backbuffer Functions */

static void OPENGL_INTERNAL_CreateBackbuffer(
	OpenGLDevice *device,
	FNA3D_PresentationParameters *parameters
) {
	int32_t useFauxBackbuffer;
	int32_t drawX, drawY;
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

			device->backbuffer->width = parameters->backBufferWidth;
			device->backbuffer->height = parameters->backBufferHeight;
			device->backbuffer->depthFormat = parameters->depthStencilFormat;
			device->backbuffer->multiSampleCount = parameters->multiSampleCount;
			device->backbuffer->opengl.texture = 0;

			/* Generate and bind the FBO. */
			device->glGenFramebuffers(
				1,
				&device->backbuffer->opengl.handle
			);
			BindFramebuffer(
				device,
				device->backbuffer->opengl.handle
			);

			/* Create and attach the color buffer */
			device->glGenRenderbuffers(
				1,
				&device->backbuffer->opengl.colorAttachment
			);
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				device->backbuffer->opengl.colorAttachment
			);
			if (device->backbuffer->multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					device->backbuffer->multiSampleCount,
					GL_RGBA8,
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					GL_RGBA8,
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				device->backbuffer->opengl.colorAttachment
			);

			if (device->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				/* Don't bother creating a DS buffer */
				device->backbuffer->opengl.depthStencilAttachment = 0;

				/* Keep this state sane. */
				device->glBindRenderbuffer(
					GL_RENDERBUFFER,
					device->realBackbufferRBO
				);

				return;
			}

			device->glGenRenderbuffers(
				1,
				&device->backbuffer->opengl.depthStencilAttachment
			);
			device->glBindRenderbuffer(
				GL_RENDERBUFFER,
				device->backbuffer->opengl.depthStencilAttachment
			);
			if (device->backbuffer->multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					device->backbuffer->multiSampleCount,
					XNAToGL_DepthStorage[
						device->backbuffer->depthFormat
					],
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					XNAToGL_DepthStorage[
						device->backbuffer->depthFormat
					],
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_DEPTH_ATTACHMENT,
				GL_RENDERBUFFER,
				device->backbuffer->opengl.depthStencilAttachment
			);
			if (device->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_STENCIL_ATTACHMENT,
					GL_RENDERBUFFER,
					device->backbuffer->opengl.depthStencilAttachment
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
			device->backbuffer->width = parameters->backBufferWidth;
			device->backbuffer->height = parameters->backBufferHeight;
			device->backbuffer->multiSampleCount = parameters->multiSampleCount;
			if (device->backbuffer->opengl.texture != 0)
			{
				device->glDeleteTextures(
					1,
					&device->backbuffer->opengl.texture
				);
				device->backbuffer->opengl.texture = 0;
			}

			if (device->renderTargetBound)
			{
				device->glBindFramebuffer(
					GL_FRAMEBUFFER,
					device->backbuffer->opengl.handle
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
			if (device->backbuffer->opengl.depthStencilAttachment != 0)
			{
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					0
				);
				if (device->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
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
				device->backbuffer->opengl.colorAttachment
			);
			if (device->backbuffer->multiSampleCount > 0)
			{
				device->glRenderbufferStorageMultisample(
					GL_RENDERBUFFER,
					device->backbuffer->multiSampleCount,
					GL_RGBA8,
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			else
			{
				device->glRenderbufferStorage(
					GL_RENDERBUFFER,
					GL_RGBA8,
					device->backbuffer->width,
					device->backbuffer->height
				);
			}
			device->glFramebufferRenderbuffer(
				GL_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				device->backbuffer->opengl.colorAttachment
			);

			/* Generate/Delete depth/stencil attachment, if needed */
			if (parameters->depthStencilFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				if (device->backbuffer->opengl.depthStencilAttachment != 0)
				{
					device->glDeleteRenderbuffers(
						1,
						&device->backbuffer->opengl.depthStencilAttachment
					);
					device->backbuffer->opengl.depthStencilAttachment = 0;
				}
			}
			else if (device->backbuffer->opengl.depthStencilAttachment == 0)
			{
				device->glGenRenderbuffers(
					1,
					&device->backbuffer->opengl.depthStencilAttachment
				);
			}

			/* Update the depth/stencil buffer, if applicable */
			device->backbuffer->depthFormat = parameters->depthStencilFormat;
			if (device->backbuffer->opengl.depthStencilAttachment != 0)
			{
				device->glBindRenderbuffer(
					GL_RENDERBUFFER,
					device->backbuffer->opengl.depthStencilAttachment
				);
				if (device->backbuffer->multiSampleCount > 0)
				{
					device->glRenderbufferStorageMultisample(
						GL_RENDERBUFFER,
						device->backbuffer->multiSampleCount,
						XNAToGL_DepthStorage[device->backbuffer->depthFormat],
						device->backbuffer->width,
						device->backbuffer->height
					);
				}
				else
				{
					device->glRenderbufferStorage(
						GL_RENDERBUFFER,
						XNAToGL_DepthStorage[device->backbuffer->depthFormat],
						device->backbuffer->width,
						device->backbuffer->height
					);
				}
				device->glFramebufferRenderbuffer(
					GL_FRAMEBUFFER,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					device->backbuffer->opengl.depthStencilAttachment
				);
				if (device->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
				{
					device->glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						device->backbuffer->opengl.depthStencilAttachment
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
		device->backbuffer->width = parameters->backBufferWidth;
		device->backbuffer->height = parameters->backBufferHeight;
		device->backbuffer->depthFormat = device->windowDepthFormat;
	}
}

static void OPENGL_INTERNAL_DisposeBackbuffer(OpenGLDevice *device)
{
	#define GLBACKBUFFER device->backbuffer->opengl

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

static uint8_t OPENGL_INTERNAL_ReadTargetIfApplicable(
	FNA3D_Renderer *driverData,
	FNA3D_Texture* textureIn,
	int32_t width,
	int32_t height,
	int32_t level,
	void* data,
	int32_t subX,
	int32_t subY,
	int32_t subW,
	int32_t subH
) {
	GLuint prevReadBuffer, prevWriteBuffer;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *texture = (OpenGLTexture*) textureIn;
	uint8_t texUnbound = (	device->currentDrawBuffers != 1 ||
				device->currentAttachments[0] != texture->handle	);
	if (texUnbound && !device->useES3)
	{
		return 0;
	}

	prevReadBuffer = device->currentReadFramebuffer;
	prevWriteBuffer = device->currentDrawFramebuffer;
	if (texUnbound)
	{
		BindFramebuffer(device, device->resolveFramebufferRead);
		device->glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D,
			texture->handle,
			level
		);
	}
	else
	{
		BindReadFramebuffer(device, device->targetFramebuffer);
	}

	/* glReadPixels should be faster than reading
	 * back from the render target if we are already bound.
	 */
	device->glReadPixels(
		subX,
		subY,
		subW,
		subH,
		GL_RGBA, /* FIXME: Assumption! */
		GL_UNSIGNED_BYTE,
		data
	);

	if (texUnbound)
	{
		if (prevReadBuffer == prevWriteBuffer)
		{
			BindFramebuffer(device, prevReadBuffer);
		}
		else
		{
			BindReadFramebuffer(device, prevReadBuffer);
			BindDrawFramebuffer(device, prevWriteBuffer);
		}
	}
	else
	{
		BindReadFramebuffer(device, prevReadBuffer);
	}
	return 1;
}

void OPENGL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OPENGL_INTERNAL_CreateBackbuffer(device, presentationParameters);
}

void OPENGL_ReadBackbuffer(
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
	GLuint prevReadBuffer, prevDrawBuffer;
	int32_t pitch, row;
	uint8_t *temp;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	uint8_t *dataPtr = (uint8_t*) data;

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
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"ReadBackbuffer startIndex/elementCount combination unimplemented!"
		);
		return;
	}

	prevReadBuffer = device->currentReadFramebuffer;

	if (device->backbuffer->multiSampleCount > 0)
	{
		/* We have to resolve the renderbuffer to a texture first. */
		prevDrawBuffer = device->currentDrawFramebuffer;

		if (device->backbuffer->opengl.texture == 0)
		{
			device->glGenTextures(
				1,
				&device->backbuffer->opengl.texture
			);
			device->glBindTexture(
				GL_TEXTURE_2D,
				device->backbuffer->opengl.texture
			);
			device->glTexImage2D(
				GL_TEXTURE_2D,
				0,
				GL_RGBA,
				device->backbuffer->width,
				device->backbuffer->height,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				NULL
			);
			device->glBindTexture(
				device->textures[0]->target,
				device->textures[0]->handle
			);
		}
		BindFramebuffer(device, device->resolveFramebufferDraw);
		device->glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			GL_TEXTURE_2D,
			device->backbuffer->opengl.texture,
			0
		);
		BindReadFramebuffer(device, device->backbuffer->opengl.handle);
		device->glBlitFramebuffer(
			0, 0, device->backbuffer->width, device->backbuffer->height,
			0, 0, device->backbuffer->width, device->backbuffer->height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR
		);
		/* Don't invalidate the backbuffer here! */
		BindDrawFramebuffer(device, prevDrawBuffer);
		BindReadFramebuffer(device, device->resolveFramebufferDraw);
	}
	else
	{
		BindReadFramebuffer(
			device,
			(device->backbuffer->type == BACKBUFFER_TYPE_OPENGL) ?
				device->backbuffer->opengl.handle :
				0
		);
	}

	device->glReadPixels(
		x,
		y,
		w,
		h,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		data
	);

	BindReadFramebuffer(device, prevReadBuffer);

	/* Now we get to do a software-based flip! Yes, really! -flibit */
	pitch = w * 4;
	temp = (uint8_t*) SDL_malloc(pitch);
	for (row = 0; row < h / 2; row += 1)
	{
		/* Top to temp, bottom to top, temp to bottom */
		SDL_memcpy(temp, dataPtr + (row * pitch), pitch);
		SDL_memcpy(dataPtr + (row * pitch), dataPtr + ((h - row - 1) * pitch), pitch);
		SDL_memcpy(dataPtr + ((h - row - 1) * pitch), temp, pitch);
	}
	SDL_free(temp);
}

void OPENGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	*w = device->backbuffer->width;
	*h = device->backbuffer->height;
}

FNA3D_SurfaceFormat OPENGL_GetBackbufferSurfaceFormat(FNA3D_Renderer *driverData)
{
	return FNA3D_SURFACEFORMAT_COLOR;
}

FNA3D_DepthFormat OPENGL_GetBackbufferDepthFormat(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->backbuffer->depthFormat;
}

int32_t OPENGL_GetBackbufferMultiSampleCount(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->backbuffer->multiSampleCount;
}

/* Textures */

static OpenGLTexture* OPENGL_INTERNAL_CreateTexture(
	OpenGLDevice *device,
	GLenum target,
	int32_t levelCount
) {
	OpenGLTexture* result = (OpenGLTexture*) SDL_malloc(
		sizeof(OpenGLTexture)
	);

	device->glGenTextures(1, &result->handle);
	result->target = target;
	result->hasMipmaps = (levelCount > 1);
	result->wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP;
	result->filter = FNA3D_TEXTUREFILTER_LINEAR;
	result->anisotropy = 4.0f;
	result->maxMipmapLevel = 0;
	result->lodBias = 0.0f;

	BindTexture(device, result);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_WRAP_S,
		XNAToGL_Wrap[result->wrapS]
	);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_WRAP_T,
		XNAToGL_Wrap[result->wrapT]
	);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_WRAP_R,
		XNAToGL_Wrap[result->wrapR]
	);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_MAG_FILTER,
		XNAToGL_MagFilter[result->filter]
	);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_MIN_FILTER,
		result->hasMipmaps ?
			XNAToGL_MinMipFilter[result->filter] :
			XNAToGL_MinFilter[result->filter]
	);
	device->glTexParameterf(
		result->target,
		GL_TEXTURE_MAX_ANISOTROPY_EXT,
		(result->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC) ?
			SDL_max(result->anisotropy, 1.0f) :
			1.0f
	);
	device->glTexParameteri(
		result->target,
		GL_TEXTURE_BASE_LEVEL,
		result->maxMipmapLevel
	);
	if (!device->useES3)
	{
		device->glTexParameterf(
			result->target,
			GL_TEXTURE_LOD_BIAS,
			result->lodBias
		);
	}
	return result;
}

static int32_t OPENGL_INTERNAL_Texture_GetFormatSize(FNA3D_SurfaceFormat format)
{
	switch (format)
	{
		case FNA3D_SURFACEFORMAT_DXT1:
			return 8;
		case FNA3D_SURFACEFORMAT_DXT3:
		case FNA3D_SURFACEFORMAT_DXT5:
			return 16;
		case FNA3D_SURFACEFORMAT_ALPHA8:
			return 1;
		case FNA3D_SURFACEFORMAT_BGR565:
		case FNA3D_SURFACEFORMAT_BGRA4444:
		case FNA3D_SURFACEFORMAT_BGRA5551:
		case FNA3D_SURFACEFORMAT_HALFSINGLE:
		case FNA3D_SURFACEFORMAT_NORMALIZEDBYTE2:
			return 2;
		case FNA3D_SURFACEFORMAT_COLOR:
		case FNA3D_SURFACEFORMAT_SINGLE:
		case FNA3D_SURFACEFORMAT_RG32:
		case FNA3D_SURFACEFORMAT_HALFVECTOR2:
		case FNA3D_SURFACEFORMAT_NORMALIZEDBYTE4:
		case FNA3D_SURFACEFORMAT_RGBA1010102:
		case FNA3D_SURFACEFORMAT_COLORBGRA_EXT:
			return 4;
		case FNA3D_SURFACEFORMAT_HALFVECTOR4:
		case FNA3D_SURFACEFORMAT_RGBA64:
		case FNA3D_SURFACEFORMAT_VECTOR2:
			return 8;
		case FNA3D_SURFACEFORMAT_VECTOR4:
			return 16;
		default:
			SDL_LogError(
				SDL_LOG_CATEGORY_APPLICATION,
				"Unrecognized SurfaceFormat!"
			);
			return 0;
	}
}

static int32_t OPENGL_INTERNAL_Texture_GetPixelStoreAlignment(FNA3D_SurfaceFormat format)
{
	/* https://github.com/FNA-XNA/FNA/pull/238
	 * https://www.khronos.org/registry/OpenGL/specs/gl/glspec21.pdf
	 * OpenGL 2.1 Specification, section 3.6.1, table 3.1 specifies that
	 * the pixelstorei alignment cannot exceed 8
	 */
	return SDL_min(8, OPENGL_INTERNAL_Texture_GetFormatSize(format));
}

FNA3D_Texture* OPENGL_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *result;
	GLenum glFormat, glInternalFormat, glType;
	int32_t levelWidth, levelHeight, i;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURE2D;
		cmd.createTexture2D.format = format;
		cmd.createTexture2D.width = width;
		cmd.createTexture2D.height = height;
		cmd.createTexture2D.levelCount = levelCount;
		cmd.createTexture2D.isRenderTarget = isRenderTarget;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.createTexture2D.retval;
	}

	result = (OpenGLTexture*) OPENGL_INTERNAL_CreateTexture(
		device,
		GL_TEXTURE_2D,
		levelCount
	);

	glFormat = XNAToGL_TextureFormat[format];
	glInternalFormat = XNAToGL_TextureInternalFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		for (i = 0; i < levelCount; i += 1)
		{
			levelWidth = SDL_max(width >> i, 1);
			levelHeight = SDL_max(height >> i, 1);
			device->glCompressedTexImage2D(
				GL_TEXTURE_2D,
				i,
				glInternalFormat,
				levelWidth,
				levelHeight,
				0,
				((levelWidth + 3) / 4) * ((levelHeight + 3) / 4) * OPENGL_INTERNAL_Texture_GetFormatSize(format),
				NULL
			);
		}
	}
	else
	{
		glType = XNAToGL_TextureDataType[format];
		for (i = 0; i < levelCount; i += 1)
		{
			device->glTexImage2D(
				GL_TEXTURE_2D,
				i,
				glInternalFormat,
				SDL_max(width >> i, 1),
				SDL_max(height >> i, 1),
				0,
				glFormat,
				glType,
				NULL
			);
		}
	}

	return (FNA3D_Texture*) result;
}

FNA3D_Texture* OPENGL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *result;
	GLenum glFormat, glInternalFormat, glType;
	int32_t i;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_3DTexture);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURE3D;
		cmd.createTexture3D.format = format;
		cmd.createTexture3D.width = width;
		cmd.createTexture3D.height = height;
		cmd.createTexture3D.depth = depth;
		cmd.createTexture3D.levelCount = levelCount;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.createTexture3D.retval;
	}

	result = OPENGL_INTERNAL_CreateTexture(
		device,
		GL_TEXTURE_3D,
		levelCount
	);

	glFormat = XNAToGL_TextureFormat[format];
	glInternalFormat = XNAToGL_TextureInternalFormat[format];
	glType = XNAToGL_TextureDataType[format];
	for (i = 0; i < levelCount; i += 1)
	{
		device->glTexImage3D(
			GL_TEXTURE_3D,
			i,
			glInternalFormat,
			SDL_max(width >> i, 1),
			SDL_max(height >> i, 1),
			SDL_max(depth >> i, 1),
			0,
			glFormat,
			glType,
			NULL
		);
	}
	return (FNA3D_Texture*) result;
}

FNA3D_Texture* OPENGL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *result;
	GLenum glFormat, glInternalFormat;
	int32_t levelSize, i, l;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURECUBE;
		cmd.createTextureCube.format = format;
		cmd.createTextureCube.size = size;
		cmd.createTextureCube.levelCount = levelCount;
		cmd.createTextureCube.isRenderTarget = isRenderTarget;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.createTextureCube.retval;
	}

	result = OPENGL_INTERNAL_CreateTexture(
		device,
		GL_TEXTURE_CUBE_MAP,
		levelCount
	);

	glFormat = XNAToGL_TextureFormat[format];
	glInternalFormat = XNAToGL_TextureInternalFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		for (i = 0; i < 6; i += 1)
		{
			for (l = 0; l < levelCount; l += 1)
			{
				levelSize = SDL_max(size >> l, 1);
				device->glCompressedTexImage2D(
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
					l,
					glInternalFormat,
					levelSize,
					levelSize,
					0,
					((levelSize + 3) / 4) * ((levelSize + 3) / 4) * OPENGL_INTERNAL_Texture_GetFormatSize(format),
					NULL
				);
			}
		}
	}
	else
	{
		GLenum glType = XNAToGL_TextureDataType[format];
		for (i = 0; i < 6; i += 1)
		{
			for (l = 0; l < levelCount; l += 1)
			{
				levelSize = SDL_max(size >> l, 1);
				device->glTexImage2D(
					GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
					l,
					glInternalFormat,
					levelSize,
					levelSize,
					0,
					glFormat,
					glType,
					NULL
				);
			}
		}
	}

	return (FNA3D_Texture*) result;
}

static void OPENGL_INTERNAL_DestroyTexture(
	OpenGLDevice *device,
	OpenGLTexture *texture
) {
	int32_t i;
	for (i = 0; i < device->numAttachments; i += 1)
	{
		if (texture->handle == device->currentAttachments[i])
		{
			/* Force an attachment update, this no longer exists! */
			device->currentAttachments[i] = UINT32_MAX;
		}
	}
	for (i = 0; i < device->numTextureSlots; i += 1)
	{
		if (device->textures[i] == texture)
		{
			/* Remove this texture from the sampler cache */
			device->textures[i] = &NullTexture;
		}
	}
	device->glDeleteTextures(1, &texture->handle);
	SDL_free(texture);
}

void OPENGL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLTexture *glTexture = (OpenGLTexture*) texture;
	OpenGLTexture *curr;

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyTexture(device, glTexture);
	}
	else
	{
		SDL_LockMutex(device->disposeTexturesLock);
		LinkedList_Add(device->disposeTextures, glTexture, curr);
		SDL_UnlockMutex(device->disposeTexturesLock);
	}
}

void OPENGL_SetTextureData2D(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	GLenum glFormat;
	int32_t packSize;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETTEXTUREDATA2D;
		cmd.setTextureData2D.texture = texture;
		cmd.setTextureData2D.format = format;
		cmd.setTextureData2D.x = x;
		cmd.setTextureData2D.y = y;
		cmd.setTextureData2D.w = w;
		cmd.setTextureData2D.h = h;
		cmd.setTextureData2D.level = level;
		cmd.setTextureData2D.data = data;
		cmd.setTextureData2D.dataLength = dataLength;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindTexture(device, (OpenGLTexture*) texture);

	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		/* Note that we're using glInternalFormat, not glFormat.
		 * In this case, they should actually be the same thing,
		 * but we use glFormat somewhat differently for
		 * compressed textures.
		 * -flibit
		 */
		device->glCompressedTexSubImage2D(
			GL_TEXTURE_2D,
			level,
			x,
			y,
			w,
			h,
			XNAToGL_TextureInternalFormat[format],
			dataLength,
			data
		);
	}
	else
	{
		/* Set pixel alignment to match texel size in bytes. */
		packSize = OPENGL_INTERNAL_Texture_GetPixelStoreAlignment(format);
		if (packSize != 4)
		{
			device->glPixelStorei(
				GL_UNPACK_ALIGNMENT,
				packSize
			);
		}

		device->glTexSubImage2D(
			GL_TEXTURE_2D,
			level,
			x,
			y,
			w,
			h,
			glFormat,
			XNAToGL_TextureDataType[format],
			data
		);

		/* Keep this state sane -flibit */
		if (packSize != 4)
		{
			device->glPixelStorei(
				GL_UNPACK_ALIGNMENT,
				4
			);
		}
	}
}

void OPENGL_SetTextureData3D(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_3DTexture);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETTEXTUREDATA3D;
		cmd.setTextureData3D.texture = texture;
		cmd.setTextureData3D.format = format;
		cmd.setTextureData3D.level = level;
		cmd.setTextureData3D.left = left;
		cmd.setTextureData3D.top = top;
		cmd.setTextureData3D.right = right;
		cmd.setTextureData3D.bottom = bottom;
		cmd.setTextureData3D.front = front;
		cmd.setTextureData3D.back = back;
		cmd.setTextureData3D.data = data;
		cmd.setTextureData3D.dataLength = dataLength;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindTexture(device, (OpenGLTexture*) texture);

	device->glTexSubImage3D(
		GL_TEXTURE_3D,
		level,
		left,
		top,
		front,
		right - left,
		bottom - top,
		back - front,
		XNAToGL_TextureFormat[format],
		XNAToGL_TextureDataType[format],
		data
	);
}

void OPENGL_SetTextureDataCube(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	GLenum glFormat;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETTEXTUREDATACUBE;
		cmd.setTextureDataCube.texture = texture;
		cmd.setTextureDataCube.format = format;
		cmd.setTextureDataCube.x = x;
		cmd.setTextureDataCube.y = y;
		cmd.setTextureDataCube.w = w;
		cmd.setTextureDataCube.h = h;
		cmd.setTextureDataCube.cubeMapFace = cubeMapFace;
		cmd.setTextureDataCube.level = level;
		cmd.setTextureDataCube.data = data;
		cmd.setTextureDataCube.dataLength = dataLength;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindTexture(device, (OpenGLTexture*) texture);

	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		/* Note that we're using glInternalFormat, not glFormat.
		 * In this case, they should actually be the same thing,
		 * but we use glFormat somewhat differently for
		 * compressed textures.
		 * -flibit
		 */
		device->glCompressedTexSubImage2D(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeMapFace,
			level,
			x,
			y,
			w,
			h,
			XNAToGL_TextureInternalFormat[format],
			dataLength,
			data
		);
	}
	else
	{
		device->glTexSubImage2D(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeMapFace,
			level,
			x,
			y,
			w,
			h,
			glFormat,
			XNAToGL_TextureDataType[format],
			data
		);
	}
}

void OPENGL_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	uint8_t *dataPtr = (uint8_t*) ptr;

	device->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	BindTexture(device, (OpenGLTexture*) y);
	device->glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		0,
		0,
		w,
		h,
		GL_ALPHA,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	dataPtr += (w * h);
	BindTexture(device, (OpenGLTexture*) u);
	device->glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		0,
		0,
		w / 2,
		h / 2,
		GL_ALPHA,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	dataPtr += (w/2 * h/2);
	BindTexture(device, (OpenGLTexture*) v);
	device->glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		0,
		0,
		w / 2,
		h / 2,
		GL_ALPHA,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	device->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void OPENGL_GetTextureData2D(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	GLenum glFormat;
	uint8_t *texData;
	int32_t curPixel, row, col;
	uint8_t *dataPtr = (uint8_t*) data;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_NonES3);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETTEXTUREDATA2D;
		cmd.getTextureData2D.texture = texture;
		cmd.getTextureData2D.format = format;
		cmd.getTextureData2D.textureWidth = textureWidth;
		cmd.getTextureData2D.textureHeight = textureHeight;
		cmd.getTextureData2D.level = level;
		cmd.getTextureData2D.x = x;
		cmd.getTextureData2D.y = y;
		cmd.getTextureData2D.w = w;
		cmd.getTextureData2D.h = h;
		cmd.getTextureData2D.data = data;
		cmd.getTextureData2D.startIndex = startIndex;
		cmd.getTextureData2D.elementCount = elementCount;
		cmd.getTextureData2D.elementSizeInBytes = elementSizeInBytes;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	if (level == 0 && OPENGL_INTERNAL_ReadTargetIfApplicable(
		driverData,
		texture,
		textureWidth,
		textureHeight,
		level,
		data,
		x,
		y,
		w,
		h
	)) {
		return;
	}

	BindTexture(device, (OpenGLTexture*) texture);
	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"GetData with compressed textures unsupported!"
		);
		return;
	}
	else if (x == 0 && y == 0 && w == textureWidth && h == textureHeight)
	{
		/* Just throw the whole texture into the user array. */
		device->glGetTexImage(
			GL_TEXTURE_2D,
			level,
			glFormat,
			XNAToGL_TextureDataType[format],
			data
		);
	}
	else
	{
		/* Get the whole texture... */
		texData = (uint8_t*) SDL_malloc(
			textureWidth *
			textureHeight *
			elementSizeInBytes
		);

		device->glGetTexImage(
			GL_TEXTURE_2D,
			level,
			glFormat,
			XNAToGL_TextureDataType[format],
			texData
		);

		/* Now, blit the rect region into the user array. */
		curPixel = -1;
		for (row = y; row < y + h; row += 1)
		{
			for (col = x; col < x + w; col += 1)
			{
				curPixel += 1;
				if (curPixel < startIndex)
				{
					/* If we're not at the start yet, just keep going... */
					continue;
				}
				if (curPixel > elementCount)
				{
					/* If we're past the end, we're done! */
					return;
				}
				/* FIXME: Can we copy via pitch instead, or something? -flibit */
				SDL_memcpy(
					dataPtr + ((curPixel - startIndex) * elementSizeInBytes),
					texData + (((row * textureWidth) + col) * elementSizeInBytes),
					elementSizeInBytes
				);
			}
		}
		SDL_free(texData);
	}

}

void OPENGL_GetTextureData3D(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_NonES3);

	SDL_LogError(
		SDL_LOG_CATEGORY_APPLICATION,
		"GetTextureData3D is unsupported!"
	);
}

void OPENGL_GetTextureDataCube(
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	GLenum glFormat;
	uint8_t *texData;
	int32_t curPixel, row, col;
	uint8_t *dataPtr = (uint8_t*) data;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_NonES3);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETTEXTUREDATACUBE;
		cmd.getTextureDataCube.texture = texture;
		cmd.getTextureDataCube.format = format;
		cmd.getTextureDataCube.textureSize = textureSize;
		cmd.getTextureDataCube.cubeMapFace = cubeMapFace;
		cmd.getTextureDataCube.level = level;
		cmd.getTextureDataCube.x = x;
		cmd.getTextureDataCube.y = y;
		cmd.getTextureDataCube.w = w;
		cmd.getTextureDataCube.h = h;
		cmd.getTextureDataCube.data = data;
		cmd.getTextureDataCube.startIndex = startIndex;
		cmd.getTextureDataCube.elementCount = elementCount;
		cmd.getTextureDataCube.elementSizeInBytes = elementSizeInBytes;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindTexture(device, (OpenGLTexture*) texture);
	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"GetData with compressed textures unsupported!"
		);
		return;
	}
	else if (x == 0 && y == 0 && w == textureSize && h == textureSize)
	{
		/* Just throw the whole texture into the user array. */
		device->glGetTexImage(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeMapFace,
			level,
			glFormat,
			XNAToGL_TextureDataType[format],
			data
		);
	}
	else
	{
		/* Get the whole texture... */
		texData = (uint8_t*) SDL_malloc(
			textureSize *
			textureSize *
			elementSizeInBytes
		);

		device->glGetTexImage(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeMapFace,
			level,
			glFormat,
			XNAToGL_TextureDataType[format],
			texData
		);

		/* Now, blit the rect region into the user array. */
		curPixel = -1;
		for (row = y; row < y + h; row += 1)
		{
			for (col = x; col < x + w; col += 1)
			{
				curPixel += 1;
				if (curPixel < startIndex)
				{
					/* If we're not at the start yet, just keep going... */
					continue;
				}
				if (curPixel > elementCount)
				{
					/* If we're past the end, we're done! */
					return;
				}
				/* FIXME: Can we copy via pitch instead, or something? -flibit */
				SDL_memcpy(
					dataPtr + ((curPixel - startIndex) * elementSizeInBytes),
					texData + (((row * textureSize) + col) * elementSizeInBytes),
					elementSizeInBytes
				);
			}
		}
		SDL_free(texData);
	}
}

/* Renderbuffers */

FNA3D_Renderbuffer* OPENGL_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLRenderbuffer *renderbuffer;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENCOLORRENDERBUFFER;
		cmd.genColorRenderbuffer.width = width;
		cmd.genColorRenderbuffer.height = height;
		cmd.genColorRenderbuffer.format = format;
		cmd.genColorRenderbuffer.multiSampleCount = multiSampleCount;
		cmd.genColorRenderbuffer.texture = texture;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.genColorRenderbuffer.retval;
	}

	renderbuffer = (OpenGLRenderbuffer*) SDL_malloc(
		sizeof(OpenGLRenderbuffer)
	);

	device->glGenRenderbuffers(1, &renderbuffer->handle);
	device->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer->handle);
	if (multiSampleCount > 0)
	{
		device->glRenderbufferStorageMultisample(
			GL_RENDERBUFFER,
			multiSampleCount,
			XNAToGL_TextureInternalFormat[format],
			width,
			height
		);
	}
	else
	{
		device->glRenderbufferStorage(
			GL_RENDERBUFFER,
			XNAToGL_TextureInternalFormat[format],
			width,
			height
		);
	}
	device->glBindRenderbuffer(GL_RENDERBUFFER, device->realBackbufferRBO);

	return (FNA3D_Renderbuffer*) renderbuffer;
}

FNA3D_Renderbuffer* OPENGL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLRenderbuffer *renderbuffer;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENDEPTHRENDERBUFFER;
		cmd.genDepthStencilRenderbuffer.width = width;
		cmd.genDepthStencilRenderbuffer.height = height;
		cmd.genDepthStencilRenderbuffer.format = format;
		cmd.genDepthStencilRenderbuffer.multiSampleCount = multiSampleCount;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.genDepthStencilRenderbuffer.retval;
	}

	renderbuffer = (OpenGLRenderbuffer*) SDL_malloc(
		sizeof(OpenGLRenderbuffer)
	);

	device->glGenRenderbuffers(1, &renderbuffer->handle);
	device->glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer->handle);
	if (multiSampleCount > 0)
	{
		device->glRenderbufferStorageMultisample(
			GL_RENDERBUFFER,
			multiSampleCount,
			XNAToGL_DepthStorage[format],
			width,
			height
		);
	}
	else
	{
		device->glRenderbufferStorage(
			GL_RENDERBUFFER,
			XNAToGL_DepthStorage[format],
			width,
			height
		);
	}
	device->glBindRenderbuffer(GL_RENDERBUFFER, device->realBackbufferRBO);

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static void OPENGL_INTERNAL_DestroyRenderbuffer(
	OpenGLDevice *device,
	OpenGLRenderbuffer *renderbuffer
) {
	/* Check color attachments */
	int32_t i;
	for (i = 0; i < device->numAttachments; i += 1)
	{
		if (renderbuffer->handle == device->currentAttachments[i])
		{
			/* Force an attachment update, this no longer exists! */
			device->currentAttachments[i] = ~0;
		}
	}

	/* Check depth/stencil attachment */
	if (renderbuffer->handle == device->currentRenderbuffer)
	{
		/* Force a renderbuffer update, this no longer exists! */
		device->currentRenderbuffer = ~0;
	}

	/* Finally. */
	device->glDeleteRenderbuffers(1, &renderbuffer->handle);
	SDL_free(renderbuffer);
}

void OPENGL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLRenderbuffer *buffer = (OpenGLRenderbuffer*) renderbuffer;
	OpenGLRenderbuffer *curr;

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyRenderbuffer(device, buffer);
	}
	else
	{
		SDL_LockMutex(device->disposeRenderbuffersLock);
		LinkedList_Add(device->disposeRenderbuffers, buffer, curr);
		SDL_UnlockMutex(device->disposeRenderbuffersLock);
	}
}

/* Vertex Buffers */

FNA3D_Buffer* OPENGL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *result = NULL;
	GLuint handle;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENVERTEXBUFFER;
		cmd.genVertexBuffer.dynamic = dynamic;
		cmd.genVertexBuffer.usage = usage;
		cmd.genVertexBuffer.vertexCount = vertexCount;
		cmd.genVertexBuffer.vertexStride = vertexStride;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.genVertexBuffer.retval;
	}

	device->glGenBuffers(1, &handle);

	result = (OpenGLBuffer*) SDL_malloc(sizeof(OpenGLBuffer));
	result->handle = handle;
	result->size = (intptr_t) (vertexStride * vertexCount);
	result->dynamic = (dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);

	BindVertexBuffer(device, handle);
	device->glBufferData(
		GL_ARRAY_BUFFER,
		result->size,
		NULL,
		result->dynamic
	);

	return (FNA3D_Buffer*) result;
}

static void OPENGL_INTERNAL_DestroyVertexBuffer(
	OpenGLDevice *device,
	OpenGLBuffer *buffer
) {
	int32_t i;

	if (buffer->handle == device->currentVertexBuffer)
	{
		device->glBindBuffer(GL_ARRAY_BUFFER, 0);
		device->currentVertexBuffer = 0;
	}
	for (i = 0; i < device->numVertexAttributes; i += 1)
	{
		if (buffer->handle == device->attributes[i].currentBuffer)
		{
			/* Force the next vertex attrib update! */
			device->attributes[i].currentBuffer = UINT32_MAX;
		}
	}
	device->glDeleteBuffers(1, &buffer->handle);

	SDL_free(buffer);
}

void OPENGL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	OpenGLBuffer *curr;

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyVertexBuffer(device, glBuffer);
	}
	else
	{
		SDL_LockMutex(device->disposeVertexBuffersLock);
		LinkedList_Add(device->disposeVertexBuffers, glBuffer, curr);
		SDL_UnlockMutex(device->disposeVertexBuffersLock);
	}
}

void OPENGL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETVERTEXBUFFERDATA;
		cmd.setVertexBufferData.buffer = buffer;
		cmd.setVertexBufferData.offsetInBytes = offsetInBytes;
		cmd.setVertexBufferData.data = data;
		cmd.setVertexBufferData.dataLength = dataLength;
		cmd.setVertexBufferData.options = options;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindVertexBuffer(device, glBuffer->handle);

	if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		device->glBufferData(
			GL_ARRAY_BUFFER,
			glBuffer->size,
			NULL,
			glBuffer->dynamic
		);
	}

	device->glBufferSubData(
		GL_ARRAY_BUFFER,
		(GLintptr) offsetInBytes,
		(GLsizeiptr) dataLength,
		data
	);
}

void OPENGL_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useStagingBuffer;
	int32_t i;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_NonES3);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETVERTEXBUFFERDATA;
		cmd.getVertexBufferData.buffer = buffer;
		cmd.getVertexBufferData.offsetInBytes = offsetInBytes;
		cmd.getVertexBufferData.data = data;
		cmd.getVertexBufferData.startIndex = startIndex;
		cmd.getVertexBufferData.elementCount = elementCount;
		cmd.getVertexBufferData.elementSizeInBytes = elementSizeInBytes;
		cmd.getVertexBufferData.vertexStride = vertexStride;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	dataBytes = (uint8_t*) data;
	useStagingBuffer = elementSizeInBytes < vertexStride;
	if (useStagingBuffer)
	{
		cpy = SDL_malloc(elementCount * vertexStride);
	}
	else
	{
		cpy = dataBytes + (startIndex * elementSizeInBytes);
	}

	BindVertexBuffer(device, glBuffer->handle);

	device->glGetBufferSubData(
		GL_ARRAY_BUFFER,
		(GLintptr) offsetInBytes,
		(GLsizeiptr) (elementCount * vertexStride),
		cpy
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

FNA3D_Buffer* OPENGL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *result = NULL;
	GLuint handle;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENINDEXBUFFER;
		cmd.genIndexBuffer.dynamic = dynamic;
		cmd.genIndexBuffer.usage = usage;
		cmd.genIndexBuffer.indexCount = indexCount;
		cmd.genIndexBuffer.indexElementSize = indexElementSize;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.genIndexBuffer.retval;
	}

	device->glGenBuffers(1, &handle);

	result = (OpenGLBuffer*) SDL_malloc(sizeof(OpenGLBuffer));
	result->handle = handle;
	result->size = (intptr_t) (
		indexCount * XNAToGL_IndexSize[indexElementSize]
	);
	result->dynamic = (dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);

	BindIndexBuffer(device, handle);
	device->glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		result->size,
		NULL,
		result->dynamic
	);

	return (FNA3D_Buffer*) result;
}

static void OPENGL_INTERNAL_DestroyIndexBuffer(
	OpenGLDevice *device,
	OpenGLBuffer *buffer
) {
	if (buffer->handle == device->currentIndexBuffer)
	{
		device->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		device->currentIndexBuffer = 0;
	}
	device->glDeleteBuffers(1, &buffer->handle);
	SDL_free(buffer);
}

void OPENGL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	OpenGLBuffer *curr;

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyIndexBuffer(device, glBuffer);
	}
	else
	{
		SDL_LockMutex(device->disposeIndexBuffersLock);
		LinkedList_Add(device->disposeIndexBuffers, glBuffer, curr);
		SDL_UnlockMutex(device->disposeIndexBuffersLock);
	}
}

void OPENGL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETINDEXBUFFERDATA;
		cmd.setIndexBufferData.buffer = buffer;
		cmd.setIndexBufferData.offsetInBytes = offsetInBytes;
		cmd.setIndexBufferData.data = data;
		cmd.setIndexBufferData.dataLength = dataLength;
		cmd.setIndexBufferData.options = options;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindIndexBuffer(device, glBuffer->handle);

	if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		device->glBufferData(
			GL_ELEMENT_ARRAY_BUFFER,
			glBuffer->size,
			NULL,
			glBuffer->dynamic
		);
	}

	device->glBufferSubData(
		GL_ELEMENT_ARRAY_BUFFER,
		(GLintptr) offsetInBytes,
		(GLsizeiptr) dataLength,
		data
	);
}

void OPENGL_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLBuffer *glBuffer = (OpenGLBuffer*) buffer;
	FNA3D_Command cmd;
	SDL_sem *sem;

	SDL_assert(device->supports_NonES3);

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETINDEXBUFFERDATA;
		cmd.getIndexBufferData.buffer = buffer;
		cmd.getIndexBufferData.offsetInBytes = offsetInBytes;
		cmd.getIndexBufferData.data = data;
		cmd.getIndexBufferData.startIndex = startIndex;
		cmd.getIndexBufferData.elementCount = elementCount;
		cmd.getIndexBufferData.elementSizeInBytes = elementSizeInBytes;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return;
	}

	BindIndexBuffer(device, glBuffer->handle);

	device->glGetBufferSubData(
		GL_ELEMENT_ARRAY_BUFFER,
		(GLintptr) offsetInBytes,
		(GLsizeiptr) (elementCount * elementSizeInBytes),
		((uint8_t*) data) + (startIndex * elementSizeInBytes)
	);
}

/* Effects */

FNA3D_Effect* OPENGL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	MOJOSHADER_effect *effect;
	MOJOSHADER_glEffect *glEffect;
	OpenGLEffect *result;
	int32_t i;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATEEFFECT;
		cmd.createEffect.effectCode = effectCode;
		cmd.createEffect.effectCodeLength = effectCodeLength;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.createEffect.retval;
	}

	effect = MOJOSHADER_parseEffect(
		device->shaderProfile,
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

	/* FIXME: Needs a debug check! */
	for (i = 0; i < effect->error_count; i += 1)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"MOJOSHADER_parseEffect Error: %s",
			effect->errors[i].error
		);
	}

	glEffect = MOJOSHADER_glCompileEffect(effect);
	if (glEffect == NULL)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s", MOJOSHADER_glGetError()
		);
		SDL_assert(0);
	}

	result = (OpenGLEffect*) SDL_malloc(sizeof(OpenGLEffect));
	result->effect = effect;
	result->glEffect = glEffect;

	return (FNA3D_Effect*) result;
}

FNA3D_Effect* OPENGL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLEffect *cloneSource = (OpenGLEffect*) effect;
	MOJOSHADER_effect *effectData;
	MOJOSHADER_glEffect *glEffect;
	OpenGLEffect *result;
	FNA3D_Command cmd;
	SDL_sem *sem;

	if (device->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CLONEEFFECT;
		cmd.cloneEffect.cloneSource = effect;
		cmd.semaphore = SDL_CreateSemaphore(0);
		ForceToMainThread(device, &cmd);
		SDL_SemWait(cmd.semaphore);
		SDL_DestroySemaphore(cmd.semaphore);
		return cmd.cloneEffect.retval;
	}

	effectData = MOJOSHADER_cloneEffect(cloneSource->effect);
	glEffect = MOJOSHADER_glCompileEffect(effectData);
	if (glEffect == NULL)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			"%s", MOJOSHADER_glGetError()
		);
		SDL_assert(0);
	}

	result = (OpenGLEffect*) SDL_malloc(sizeof(OpenGLEffect));
	result->effect = effectData;
	result->glEffect = glEffect;

	return (FNA3D_Effect*) result;
}

static void OPENGL_INTERNAL_DestroyEffect(
	OpenGLDevice *device,
	OpenGLEffect *effect
) {
	MOJOSHADER_glEffect *glEffect = effect->glEffect;
	if (glEffect == device->currentEffect)
	{
		MOJOSHADER_glEffectEndPass(device->currentEffect);
		MOJOSHADER_glEffectEnd(device->currentEffect);
		device->currentEffect = NULL;
		device->currentTechnique = NULL;
		device->currentPass = 0;
	}
	MOJOSHADER_glDeleteEffect(glEffect);
	MOJOSHADER_freeEffect(effect->effect);
	SDL_free(effect);
}

void OPENGL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLEffect *fnaEffect = (OpenGLEffect*) effect;
	OpenGLEffect *curr;

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyEffect(device, fnaEffect);
	}
	else
	{
		SDL_LockMutex(device->disposeEffectsLock);
		LinkedList_Add(device->disposeEffects, fnaEffect, curr);
		SDL_UnlockMutex(device->disposeEffectsLock);
	}
}

void OPENGL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLEffect *fnaEffect = (OpenGLEffect*) effect;
	MOJOSHADER_glEffect *glEffectData = fnaEffect->glEffect;
	uint32_t whatever;

	device->effectApplied = 1;
	if (glEffectData == device->currentEffect)
	{
		if (	technique == device->currentTechnique &&
			pass == device->currentPass		)
		{
			MOJOSHADER_glEffectCommitChanges(
				device->currentEffect
			);
			return;
		}
		MOJOSHADER_glEffectEndPass(device->currentEffect);
		MOJOSHADER_glEffectBeginPass(device->currentEffect, pass);
		device->currentTechnique = technique;
		device->currentPass = pass;
		return;
	}
	else if (device->currentEffect != NULL)
	{
		MOJOSHADER_glEffectEndPass(device->currentEffect);
		MOJOSHADER_glEffectEnd(device->currentEffect);
	}
	MOJOSHADER_glEffectBegin(
		glEffectData,
		&whatever,
		0,
		stateChanges
	);
	MOJOSHADER_glEffectBeginPass(glEffectData, pass);
	device->currentEffect = glEffectData;
	device->currentTechnique = technique;
	device->currentPass = pass;
}

void OPENGL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	MOJOSHADER_glEffect *glEffectData = ((OpenGLEffect*) effect)->glEffect;
	uint32_t whatever;

	MOJOSHADER_glEffectBegin(
		glEffectData,
		&whatever,
		1,
		stateChanges
	);
	MOJOSHADER_glEffectBeginPass(glEffectData, 0);
	device->effectApplied = 1;
}

void OPENGL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	MOJOSHADER_glEffect *glEffectData = ((OpenGLEffect*) effect)->glEffect;

	MOJOSHADER_glEffectEndPass(glEffectData);
	MOJOSHADER_glEffectEnd(glEffectData);
	device->effectApplied = 1;
}

/* Queries */

FNA3D_Query* OPENGL_CreateQuery(FNA3D_Renderer *driverData)
{
	OpenGLQuery *result;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_occlusion_query);

	result = (OpenGLQuery*) SDL_malloc(sizeof(OpenGLQuery));
	device->glGenQueries(1, &result->handle);

	return (FNA3D_Query*) result;
}

static void OPENGL_INTERNAL_DestroyQuery(
	OpenGLDevice *device,
	OpenGLQuery *query
) {
	device->glDeleteQueries(
		1,
		&query->handle
	);
	SDL_free(query);
}

void OPENGL_AddDisposeQuery(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLQuery *glQuery = (OpenGLQuery*) query;
	OpenGLQuery *curr;

	SDL_assert(device->supports_ARB_occlusion_query);

	if (device->threadID == SDL_ThreadID())
	{
		OPENGL_INTERNAL_DestroyQuery(device, glQuery);
	}
	else
	{
		SDL_LockMutex(device->disposeQueriesLock);
		LinkedList_Add(device->disposeQueries, glQuery, curr);
		SDL_UnlockMutex(device->disposeQueriesLock);
	}
}

void OPENGL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLQuery *glQuery = (OpenGLQuery*) query;

	SDL_assert(device->supports_ARB_occlusion_query);

	device->glBeginQuery(
		GL_SAMPLES_PASSED,
		glQuery->handle
	);
}

void OPENGL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;

	SDL_assert(device->supports_ARB_occlusion_query);

	/* May need to check active queries...? */
	device->glEndQuery(
		GL_SAMPLES_PASSED
	);
}

uint8_t OPENGL_QueryComplete(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	GLuint result;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLQuery *glQuery = (OpenGLQuery*) query;

	SDL_assert(device->supports_ARB_occlusion_query);

	device->glGetQueryObjectuiv(
		glQuery->handle,
		GL_QUERY_RESULT_AVAILABLE,
		&result
	);
	return result != 0;
}

int32_t OPENGL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLuint result;
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	OpenGLQuery *glQuery = (OpenGLQuery*) query;

	SDL_assert(device->supports_ARB_occlusion_query);

	device->glGetQueryObjectuiv(
		glQuery->handle,
		GL_QUERY_RESULT,
		&result
	);
	return (int32_t) result;
}

/* Feature Queries */

uint8_t OPENGL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->supports_dxt1;
}

uint8_t OPENGL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->supports_s3tc;
}

uint8_t OPENGL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return (	device->supports_ARB_draw_instanced &&
			device->supports_ARB_instanced_arrays	);
}

uint8_t OPENGL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 0;
}

int32_t OPENGL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->numTextureSlots;
}

int32_t OPENGL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	return device->maxMultiSampleCount;
}

/* Debugging */

void OPENGL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	if (device->supports_GREMEDY_string_marker)
	{
		device->glStringMarkerGREMEDY(SDL_strlen(text), text);
	}
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
	return ((OpenGLBuffer*) buffer)->size;
}

/* Effect Objects */

MOJOSHADER_effect* OPENGL_GetEffectData(FNA3D_Effect *effect)
{
	return ((OpenGLEffect*) effect)->effect;
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
	uint8_t s3tc = (
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

uint8_t OPENGL_PrepareWindowAttributes(uint32_t *flags)
{
	uint8_t forceES3, forceCore, forceCompat;
	const char *osVersion;
	int32_t depthSize, stencilSize;
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

	*flags = SDL_WINDOW_OPENGL;
	return 1;
}

void OPENGL_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	/* When using OpenGL, iOS and tvOS require an active GL context to get
	 * the drawable size of the screen.
	 */
#if defined(__IPHONEOS__) || defined(__TVOS__)
	SDL_GLContext tempContext = SDL_GL_CreateContext(window);
#endif

	SDL_GL_GetDrawableSize((SDL_Window*) window, x, y);

#if defined(__IPHONEOS__) || defined(__TVOS__)
	SDL_GL_DestroyContext(tempContext);
#endif
}

FNA3D_Device* OPENGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	int32_t flags;
	int32_t depthSize, stencilSize;
	SDL_SysWMinfo wmInfo;
	const char *renderer, *version, *vendor;
	char driverInfo[256];
	int32_t i;
	int32_t numExtensions, numSamplers, numAttributes, numAttachments;
	OpenGLDevice *device;
	FNA3D_Device *result;

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(OPENGL)

	/* Init the OpenGLDevice */
	device = (OpenGLDevice*) SDL_malloc(sizeof(OpenGLDevice));
	SDL_memset(device, '\0', sizeof(OpenGLDevice));

	/* The FNA3D_Device and OpenGLDevice need to reference each other */
	device->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) device;

	/* Debug context support */
	if (debugMode)
	{
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_FLAGS,
			SDL_GL_CONTEXT_DEBUG_FLAG
		);
	}

	/* Create OpenGL context */
	device->context = SDL_GL_CreateContext(
		(SDL_Window*) presentationParameters->deviceWindowHandle
	);

	/* Check for a possible ES/Core context */
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &flags);
	device->useES3 = (flags & SDL_GL_CONTEXT_PROFILE_ES) != 0;
	device->useCoreProfile = (flags & SDL_GL_CONTEXT_PROFILE_CORE) != 0;

	/* Check for a possible debug context */
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags);
	debugMode = (flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0;

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
		"FNA3D Driver: OpenGL\n%s",
		driverInfo
	);

	/* Initialize entry points */
	LoadEntryPoints(device, driverInfo, debugMode);

	/* FIXME: REMOVE ME ASAP! TERRIBLE HACK FOR ANGLE! */
	if (SDL_strstr(renderer, "Direct3D11") != NULL)
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
		device->textures[i] = &NullTexture;
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
		device->drawBuffersArray[i] = GL_COLOR_ATTACHMENT0 + i;
	}
	device->numAttachments = numAttachments;

	device->drawBuffersArray[numAttachments] = GL_DEPTH_ATTACHMENT;
	device->drawBuffersArray[numAttachments + 1] = GL_STENCIL_ATTACHMENT;
	device->glGenFramebuffers(1, &device->targetFramebuffer);
	device->glGenFramebuffers(1, &device->resolveFramebufferRead);
	device->glGenFramebuffers(1, &device->resolveFramebufferDraw);

	if (device->useCoreProfile)
	{
		/* Generate and bind a VAO, to shut Core up */
		device->glGenVertexArrays(1, &device->vao);
		device->glBindVertexArray(device->vao);
	}
	else if (!device->useES3)
	{
		/* Compatibility contexts require that point sprites be enabled
		 * explicitly. However, Apple's drivers have a blatant spec
		 * violation that disallows a simple glEnable. So, here we are.
		 * -flibit
		 */
		device->togglePointSprite = 0;
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

	/* Initialize device members not covered by SDL_memset('\0') */
	device->dstBlend = FNA3D_BLEND_ZERO; /* ZERO is really 1. -caleb */
	device->dstBlendAlpha = FNA3D_BLEND_ZERO; /* ZERO is really 1. -caleb */
	device->colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	device->colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	device->colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	device->colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	device->multiSampleMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	device->stencilWriteMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	device->stencilMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	device->multiSampleEnable = 1;
	device->depthRangeMax = 1.0f;
	device->currentClearDepth = 1.0f;

	/* The creation thread will be the "main" thread */
	device->threadID = SDL_ThreadID();
	device->commandsLock = SDL_CreateMutex();
	device->disposeTexturesLock = SDL_CreateMutex();
	device->disposeRenderbuffersLock = SDL_CreateMutex();
	device->disposeVertexBuffersLock = SDL_CreateMutex();
	device->disposeIndexBuffersLock = SDL_CreateMutex();
	device->disposeEffectsLock = SDL_CreateMutex();
	device->disposeQueriesLock = SDL_CreateMutex();

	/* Return the FNA3D_Device */
	return result;
}

FNA3D_Driver OpenGLDriver = {
	"OpenGL",
	OPENGL_PrepareWindowAttributes,
	OPENGL_GetDrawableSize,
	OPENGL_CreateDevice
};

#endif /* FNA3D_DRIVER_OPENGL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
