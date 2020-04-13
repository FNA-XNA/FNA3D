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

#if FNA3D_DRIVER_MODERNGL

#include "FNA3D_Driver.h"
#include "FNA3D_CommandStream.h"
#include "FNA3D_Driver_ModernGL.h"

#include <SDL.h>

/* Internal Structures */

typedef struct ModernGLTexture ModernGLTexture;
typedef struct ModernGLRenderbuffer ModernGLRenderbuffer;
typedef struct ModernGLBuffer ModernGLBuffer;
typedef struct ModernGLEffect ModernGLEffect;
typedef struct ModernGLQuery ModernGLQuery;

struct ModernGLTexture /* Cast FNA3D_Texture* to this! */
{
	uint32_t handle;
	GLenum target;
	uint8_t hasMipmaps;
	ModernGLTexture *next;
};

static ModernGLTexture NullTexture =
{
	0,
	GL_TEXTURE_2D,
	0,
	NULL
};

struct ModernGLRenderbuffer /* Cast FNA3D_Renderbuffer* to this! */
{
	GLuint handle;
	ModernGLRenderbuffer *next;
};

struct ModernGLBuffer /* Cast FNA3D_Buffer* to this! */
{
	GLuint handle;
	intptr_t size;
	GLenum flags;
	uint8_t *pin;
	ModernGLBuffer *next;
};

struct ModernGLEffect /* Cast FNA3D_Effect* to this! */
{
	MOJOSHADER_effect *effect;
	MOJOSHADER_glEffect *glEffect;
	ModernGLEffect *next;
};

struct ModernGLQuery /* Cast FNA3D_Query* to this! */
{
	GLuint handle;
	ModernGLQuery *next;
};

typedef struct ModernGLBackbuffer
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
} ModernGLBackbuffer;

typedef struct ModernGLVertexAttribute
{
	uint32_t currentBuffer;
	void *currentPointer;
	FNA3D_VertexElementFormat currentFormat;
	uint8_t currentNormalized;
	uint32_t currentStride;
} ModernGLVertexAttribute;

typedef struct ModernGLRenderer /* Cast FNA3D_Renderer* to this! */
{
	/* Associated FNA3D_Device */
	FNA3D_Device *parentDevice;

	/* Context */
	SDL_GLContext context;
	uint8_t useCoreProfile;

	/* The Faux-Backbuffer */
	ModernGLBackbuffer *backbuffer;
	FNA3D_DepthFormat windowDepthFormat;
	GLenum backbufferScaleMode;

	/* VAO for Core Profile */
	GLuint vao;

	/* Capabilities */
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
	ModernGLTexture *textures[MAX_TEXTURE_SAMPLERS];
	GLuint samplers[MAX_TEXTURE_SAMPLERS];
	FNA3D_TextureAddressMode samplersU[MAX_TEXTURE_SAMPLERS];
	FNA3D_TextureAddressMode samplersV[MAX_TEXTURE_SAMPLERS];
	FNA3D_TextureAddressMode samplersW[MAX_TEXTURE_SAMPLERS];
	FNA3D_TextureFilter samplersFilter[MAX_TEXTURE_SAMPLERS];
	float samplersAnisotropy[MAX_TEXTURE_SAMPLERS];
	int32_t samplersMaxLevel[MAX_TEXTURE_SAMPLERS];
	float samplersLODBias[MAX_TEXTURE_SAMPLERS];
	uint8_t samplersMipped[MAX_TEXTURE_SAMPLERS];

	/* Buffer Binding Cache */
	GLuint currentVertexBuffer;
	GLuint currentIndexBuffer;

	/* ld, or LastDrawn, effect/vertex attributes */
	int32_t ldBaseVertex;
	FNA3D_VertexDeclaration *ldVertexDeclaration;
	void* ldPointer;
	MOJOSHADER_glEffect *ldEffect;
	const MOJOSHADER_effectTechnique *ldTechnique;
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
	ModernGLVertexAttribute attributes[MAX_VERTEX_ATTRIBUTES];
	uint8_t attributeEnabled[MAX_VERTEX_ATTRIBUTES];
	uint8_t previousAttributeEnabled[MAX_VERTEX_ATTRIBUTES];
	int32_t attributeDivisor[MAX_VERTEX_ATTRIBUTES];
	int32_t previousAttributeDivisor[MAX_VERTEX_ATTRIBUTES];

	/* MojoShader Interop */
	const char *shaderProfile;
	MOJOSHADER_glContext *shaderContext;
	MOJOSHADER_glEffect *currentEffect;
	const MOJOSHADER_effectTechnique *currentTechnique;
	uint32_t currentPass;
	uint8_t renderTargetBound;
	uint8_t effectApplied;

	/* Threading */
	SDL_threadID threadID;
	FNA3D_Command *commands;
	SDL_mutex *commandsLock;
	ModernGLTexture *disposeTextures;
	SDL_mutex *disposeTexturesLock;
	ModernGLRenderbuffer *disposeRenderbuffers;
	SDL_mutex *disposeRenderbuffersLock;
	ModernGLBuffer *disposeVertexBuffers;
	SDL_mutex *disposeVertexBuffersLock;
	ModernGLBuffer *disposeIndexBuffers;
	SDL_mutex *disposeIndexBuffersLock;
	ModernGLEffect *disposeEffects;
	SDL_mutex *disposeEffectsLock;
	ModernGLQuery *disposeQueries;
	SDL_mutex *disposeQueriesLock;

	/* GL entry points */
	#define GL_PROC(ret, func, parms) \
		glfntype_##func func;
	#define GL_PROC_EXT(ext, fallback, ret, func, parms) \
		glfntype_##func func;
	#include "FNA3D_Driver_ModernGL_glfuncs.h"
	#undef GL_PROC
	#undef GL_PROC_EXT
} ModernGLRenderer;

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
	GL_RED,				/* SurfaceFormat.Alpha8 */
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
	GL_RGB565,				/* SurfaceFormat.Bgr565 */
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
	GL_R8,					/* SurfaceFormat.Alpha8 */
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

#if 0 /* Unused */
static int32_t XNAToGL_DepthStencilAttachment[] =
{
	GL_ZERO,			/* NOPE */
	GL_DEPTH_ATTACHMENT,		/* DepthFormat.Depth16 */
	GL_DEPTH_ATTACHMENT,		/* DepthFormat.Depth24 */
	GL_DEPTH_STENCIL_ATTACHMENT	/* DepthFormat.Depth24Stencil8 */
};
#endif

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

static int32_t XNAToGL_Primitive[] =
{
	GL_TRIANGLES,		/* PrimitiveType.TriangleList */
	GL_TRIANGLE_STRIP,	/* PrimitiveType.TriangleStrip */
	GL_LINES,		/* PrimitiveType.LineList */
	GL_LINE_STRIP,		/* PrimitiveType.LineStrip */
	GL_POINTS		/* PrimitiveType.PointListEXT */
};

/* Inline Functions */

static inline void BindReadFramebuffer(ModernGLRenderer *renderer, GLuint handle)
{
	if (handle != renderer->currentReadFramebuffer)
	{
		renderer->glBindFramebuffer(GL_READ_FRAMEBUFFER, handle);
		renderer->currentReadFramebuffer = handle;
	}
}

static inline void BindFramebuffer(ModernGLRenderer *renderer, GLuint handle)
{
	if (	renderer->currentReadFramebuffer != handle &&
		renderer->currentDrawFramebuffer != handle	)
	{
		renderer->glBindFramebuffer(GL_FRAMEBUFFER, handle);
		renderer->currentReadFramebuffer = handle;
		renderer->currentDrawFramebuffer = handle;
	}
	else if (renderer->currentReadFramebuffer != handle)
	{
		renderer->glBindFramebuffer(GL_READ_FRAMEBUFFER, handle);
		renderer->currentReadFramebuffer = handle;
	}
	else if (renderer->currentDrawFramebuffer != handle)
	{
		renderer->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, handle);
		renderer->currentDrawFramebuffer = handle;
	}
}

static inline void BindVertexBuffer(ModernGLRenderer *renderer, GLuint handle)
{
	if (handle != renderer->currentVertexBuffer)
	{
		renderer->glBindBuffer(GL_ARRAY_BUFFER, handle);
		renderer->currentVertexBuffer = handle;
	}
}

static inline void BindIndexBuffer(ModernGLRenderer *renderer, GLuint handle)
{
	if (handle != renderer->currentIndexBuffer)
	{
		renderer->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle);
		renderer->currentIndexBuffer = handle;
	}
}

static inline void ToggleGLState(
	ModernGLRenderer *renderer,
	GLenum feature,
	uint8_t enable
) {
	if (enable)
	{
		renderer->glEnable(feature);
	}
	else
	{
		renderer->glDisable(feature);
	}
}

static inline void ForceToMainThread(
	ModernGLRenderer *renderer,
	FNA3D_Command *command
) {
	FNA3D_Command *curr;
	command->semaphore = SDL_CreateSemaphore(0);

	SDL_LockMutex(renderer->commandsLock);
	LinkedList_Add(renderer->commands, command, curr);
	SDL_UnlockMutex(renderer->commandsLock);

	SDL_SemWait(command->semaphore);
	SDL_DestroySemaphore(command->semaphore);
}

/* Forward Declarations for Internal Functions */

static void MODERNGL_INTERNAL_CreateBackbuffer(
	ModernGLRenderer *renderer,
	FNA3D_PresentationParameters *parameters
);
static void MODERNGL_INTERNAL_DisposeBackbuffer(ModernGLRenderer *renderer);
static void MODERNGL_INTERNAL_DestroyTexture(
	ModernGLRenderer *renderer,
	ModernGLTexture *texture
);
static void MODERNGL_INTERNAL_DestroyRenderbuffer(
	ModernGLRenderer *renderer,
	ModernGLRenderbuffer *renderbuffer
);
static void MODERNGL_INTERNAL_DestroyVertexBuffer(
	ModernGLRenderer *renderer,
	ModernGLBuffer *buffer
);
static void MODERNGL_INTERNAL_DestroyIndexBuffer(
	ModernGLRenderer *renderer,
	ModernGLBuffer *buffer
);
static void MODERNGL_INTERNAL_DestroyEffect(
	ModernGLRenderer *renderer,
	ModernGLEffect *effect
);
static void MODERNGL_INTERNAL_DestroyQuery(
	ModernGLRenderer *renderer,
	ModernGLQuery *query
);
static void MODERNGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
);

/* Quit */

static void MODERNGL_DestroyDevice(FNA3D_Device *device)
{
	ModernGLRenderer* renderer = (ModernGLRenderer*) device->driverData;

	if (renderer->useCoreProfile)
	{
		renderer->glBindVertexArray(0);
		renderer->glDeleteVertexArrays(1, &renderer->vao);
	}

	renderer->glDeleteSamplers(
		renderer->numTextureSlots,
		renderer->samplers
	);

	renderer->glDeleteFramebuffers(1, &renderer->resolveFramebufferRead);
	renderer->resolveFramebufferRead = 0;
	renderer->glDeleteFramebuffers(1, &renderer->resolveFramebufferDraw);
	renderer->resolveFramebufferDraw = 0;
	renderer->glDeleteFramebuffers(1, &renderer->targetFramebuffer);
	renderer->targetFramebuffer = 0;

	if (renderer->backbuffer->type == BACKBUFFER_TYPE_OPENGL)
	{
		MODERNGL_INTERNAL_DisposeBackbuffer(renderer);
	}
	SDL_free(renderer->backbuffer);
	renderer->backbuffer = NULL;

	MOJOSHADER_glMakeContextCurrent(NULL);
	MOJOSHADER_glDestroyContext(renderer->shaderContext);

	SDL_DestroyMutex(renderer->commandsLock);
	SDL_DestroyMutex(renderer->disposeTexturesLock);
	SDL_DestroyMutex(renderer->disposeRenderbuffersLock);
	SDL_DestroyMutex(renderer->disposeVertexBuffersLock);
	SDL_DestroyMutex(renderer->disposeIndexBuffersLock);
	SDL_DestroyMutex(renderer->disposeEffectsLock);
	SDL_DestroyMutex(renderer->disposeQueriesLock);

	SDL_GL_DeleteContext(renderer->context);

	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void MODERNGL_BeginFrame(FNA3D_Renderer *driverData)
{
	/* No-op */
}

static inline void ExecuteCommands(ModernGLRenderer *renderer)
{
	FNA3D_Command *cmd, *next;

	SDL_LockMutex(renderer->commandsLock);
	cmd = renderer->commands;
	while (cmd != NULL)
	{
		FNA3D_ExecuteCommand(
			renderer->parentDevice,
			cmd
		);
		next = cmd->next;
		SDL_SemPost(cmd->semaphore);
		cmd = next;
	}
	renderer->commands = NULL; /* No heap memory to free! -caleb */
	SDL_UnlockMutex(renderer->commandsLock);
}

static inline void DisposeResources(ModernGLRenderer *renderer)
{
	ModernGLTexture *tex, *texNext;
	ModernGLEffect *eff, *effNext;
	ModernGLBuffer *buf, *bufNext;
	ModernGLRenderbuffer *ren, *renNext;
	ModernGLQuery *qry, *qryNext;

	/* All heap allocations are freed by func! -caleb */
	#define DISPOSE(prefix, list, func) \
		SDL_LockMutex(list##Lock); \
		prefix = list; \
		while (prefix != NULL) \
		{ \
			prefix##Next = prefix->next; \
			MODERNGL_INTERNAL_##func(renderer, prefix); \
			prefix = prefix##Next; \
		} \
		list = NULL; \
		SDL_UnlockMutex(list##Lock);

	DISPOSE(tex, renderer->disposeTextures, DestroyTexture)
	DISPOSE(ren, renderer->disposeRenderbuffers, DestroyRenderbuffer)
	DISPOSE(buf, renderer->disposeVertexBuffers, DestroyVertexBuffer)
	DISPOSE(buf, renderer->disposeIndexBuffers, DestroyIndexBuffer)
	DISPOSE(eff, renderer->disposeEffects, DestroyEffect)
	DISPOSE(qry, renderer->disposeQueries, DestroyQuery)

	#undef DISPOSE
}

static void MODERNGL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	int32_t srcX, srcY, srcW, srcH;
	int32_t dstX, dstY, dstW, dstH;
	GLuint finalBuffer;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	/* Only the faux-backbuffer supports presenting
	 * specific regions given to Present().
	 * -flibit
	 */
	if (renderer->backbuffer->type == BACKBUFFER_TYPE_OPENGL)
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
			srcW = renderer->backbuffer->width;
			srcH = renderer->backbuffer->height;
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

		if (renderer->scissorTestEnable)
		{
			renderer->glDisable(GL_SCISSOR_TEST);
		}

		if (	renderer->backbuffer->multiSampleCount > 0 &&
			(srcX != dstX || srcY != dstY || srcW != dstW || srcH != dstH)	)
		{
			/* We have to resolve the renderbuffer to a texture first.
			 * For whatever reason, we can't blit a multisample renderbuffer
			 * to the backbuffer. Not sure why, but oh well.
			 * -flibit
			 */
			if (renderer->backbuffer->opengl.texture == 0)
			{
				renderer->glCreateTextures(
					GL_TEXTURE_2D,
					1,
					&renderer->backbuffer->opengl.texture
				);
				renderer->glTextureStorage2D(
					renderer->backbuffer->opengl.texture,
					1,
					GL_RGBA,
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			renderer->glNamedFramebufferTexture(
				renderer->backbuffer->opengl.handle,
				GL_COLOR_ATTACHMENT0,
				renderer->backbuffer->opengl.texture,
				0
			);
			renderer->glBlitNamedFramebuffer(
				renderer->backbuffer->opengl.handle,
				renderer->resolveFramebufferDraw,
				0, 0, renderer->backbuffer->width, renderer->backbuffer->height,
				0, 0, renderer->backbuffer->width, renderer->backbuffer->height,
				GL_COLOR_BUFFER_BIT,
				GL_LINEAR
			);
			/* Invalidate the MSAA faux-backbuffer */
			renderer->glInvalidateNamedFramebufferData(
				renderer->backbuffer->opengl.handle,
				renderer->numAttachments + 2,
				renderer->drawBuffersArray
			);
			finalBuffer = renderer->resolveFramebufferDraw;
		}
		else
		{
			finalBuffer = renderer->backbuffer->opengl.handle;
		}

		renderer->glBlitNamedFramebuffer(
			finalBuffer,
			0,
			srcX, srcY, srcW, srcH,
			dstX, dstY, dstW, dstH,
			GL_COLOR_BUFFER_BIT,
			renderer->backbufferScaleMode
		);
		/* Invalidate the faux-backbuffer */
		renderer->glInvalidateNamedFramebufferData(
			finalBuffer,
			renderer->numAttachments + 2,
			renderer->drawBuffersArray
		);

		if (renderer->scissorTestEnable)
		{
			renderer->glEnable(GL_SCISSOR_TEST);
		}

		SDL_GL_SwapWindow((SDL_Window*) overrideWindowHandle);
	}
	else
	{
		/* Nothing left to do, just swap! */
		SDL_GL_SwapWindow((SDL_Window*) overrideWindowHandle);
	}

	/* Run any threaded commands */
	ExecuteCommands(renderer);

	/* Destroy any disposed resources */
	DisposeResources(renderer);
}

static void MODERNGL_SetPresentationInterval(
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
				FNA3D_LogInfo(
					"Using EXT_swap_control_tear VSync!"
				);
			}
			else
			{
				FNA3D_LogInfo(
					"EXT_swap_control_tear unsupported."
					" Fall back to standard VSync."
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
		FNA3D_LogError(
			"Unrecognized PresentInterval: %d",
			presentInterval
		);
	}
}

/* Drawing */

static void MODERNGL_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	uint8_t clearTarget, clearDepth, clearStencil;
	GLenum clearMask;

	/* glClear depends on the scissor rectangle! */
	if (renderer->scissorTestEnable)
	{
		renderer->glDisable(GL_SCISSOR_TEST);
	}

	clearTarget = (options & FNA3D_CLEAROPTIONS_TARGET) != 0;
	clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) != 0;
	clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) != 0;

	/* Get the clear mask, set the clear properties if needed */
	clearMask = GL_ZERO;
	if (clearTarget)
	{
		clearMask |= GL_COLOR_BUFFER_BIT;
		if (	color->x != renderer->currentClearColor.x ||
			color->y != renderer->currentClearColor.y ||
			color->z != renderer->currentClearColor.z ||
			color->w != renderer->currentClearColor.w	)
		{
			renderer->glClearColor(
				color->x,
				color->y,
				color->z,
				color->w
			);
			renderer->currentClearColor = *color;
		}
		/* glClear depends on the color write mask! */
		if (renderer->colorWriteEnable != FNA3D_COLORWRITECHANNELS_ALL)
		{
			/* FIXME: ColorWriteChannels1/2/3? -flibit */
			renderer->glColorMask(1, 1, 1, 1);
		}
	}
	if (clearDepth)
	{
		clearMask |= GL_DEPTH_BUFFER_BIT;
		if (depth != renderer->currentClearDepth)
		{
			renderer->glClearDepth((double) depth);
			renderer->currentClearDepth = depth;
		}
		/* glClear depends on the depth write mask! */
		if (!renderer->zWriteEnable)
		{
			renderer->glDepthMask(1);
		}
	}
	if (clearStencil)
	{
		clearMask |= GL_STENCIL_BUFFER_BIT;
		if (stencil != renderer->currentClearStencil)
		{
			renderer->glClearStencil(stencil);
			renderer->currentClearStencil = stencil;
		}
		/* glClear depends on the stencil write mask! */
		if (renderer->stencilWriteMask != -1)
		{
			/* AKA 0xFFFFFFFF, ugh -flibit */
			renderer->glStencilMask(-1);
		}
	}

	/* CLEAR! */
	renderer->glClear(clearMask);

	/* Clean up after ourselves. */
	if (renderer->scissorTestEnable)
	{
		renderer->glEnable(GL_SCISSOR_TEST);
	}
	if (clearTarget && renderer->colorWriteEnable != FNA3D_COLORWRITECHANNELS_ALL)
	{
		/* FIXME: ColorWriteChannels1/2/3? -flibit */
		renderer->glColorMask(
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (clearDepth && !renderer->zWriteEnable)
	{
		renderer->glDepthMask(0);
	}
	if (clearStencil && renderer->stencilWriteMask != -1) /* AKA 0xFFFFFFFF, ugh -flibit */
	{
		renderer->glStencilMask(renderer->stencilWriteMask);
	}
}

static void MODERNGL_DrawIndexedPrimitives(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buffer = (ModernGLBuffer*) indices;

	BindIndexBuffer(renderer, buffer->handle);

	renderer->glDrawRangeElementsBaseVertex(
		XNAToGL_Primitive[primitiveType],
		minVertexIndex,
		minVertexIndex + numVertices - 1,
		PrimitiveVerts(primitiveType, primitiveCount),
		XNAToGL_IndexType[indexElementSize],
		(void*) (size_t) (startIndex * IndexSize(indexElementSize)),
		baseVertex
	);
}

static void MODERNGL_DrawInstancedPrimitives(
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

	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buffer = (ModernGLBuffer*) indices;

	BindIndexBuffer(renderer, buffer->handle);

	renderer->glDrawElementsInstancedBaseVertex(
		XNAToGL_Primitive[primitiveType],
		PrimitiveVerts(primitiveType, primitiveCount),
		XNAToGL_IndexType[indexElementSize],
		(void*) (size_t) (startIndex * IndexSize(indexElementSize)),
		instanceCount,
		baseVertex
	);
}

static void MODERNGL_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	renderer->glDrawArrays(
		XNAToGL_Primitive[primitiveType],
		vertexStart,
		PrimitiveVerts(primitiveType, primitiveCount)
	);
}

static void MODERNGL_DrawUserIndexedPrimitives(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	BindIndexBuffer(renderer, 0);

	renderer->glDrawRangeElements(
		XNAToGL_Primitive[primitiveType],
		0,
		numVertices - 1,
		PrimitiveVerts(primitiveType, primitiveCount),
		XNAToGL_IndexType[indexElementSize],
		(void*) (
			((size_t) indexData) +
			(indexOffset * IndexSize(indexElementSize))
		)
	);
}

static void MODERNGL_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	renderer->glDrawArrays(
		XNAToGL_Primitive[primitiveType],
		vertexOffset,
		PrimitiveVerts(primitiveType, primitiveCount)
	);
}

/* Mutable Render States */

static void MODERNGL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	int32_t bbw, bbh;
	FNA3D_Viewport vp = *viewport;

	/* Flip viewport when target is not bound */
	if (!renderer->renderTargetBound)
	{
		MODERNGL_GetBackbufferSize(driverData, &bbw, &bbh);
		vp.y = bbh - viewport->y - viewport->h;
	}

	if (	vp.x != renderer->viewport.x ||
		vp.y != renderer->viewport.y ||
		vp.w != renderer->viewport.w ||
		vp.h != renderer->viewport.h	)
	{
		renderer->viewport = vp;
		renderer->glViewport(
			vp.x,
			vp.y,
			vp.w,
			vp.h
		);
	}

	if (	viewport->minDepth != renderer->depthRangeMin ||
		viewport->maxDepth != renderer->depthRangeMax	)
	{
		renderer->depthRangeMin = viewport->minDepth;
		renderer->depthRangeMax = viewport->maxDepth;
		renderer->glDepthRange(
			(double) viewport->minDepth,
			(double) viewport->maxDepth
		);
	}
}

static void MODERNGL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	int32_t bbw, bbh;
	FNA3D_Rect sr = *scissor;

	/* Flip rectangle when target is not bound */
	if (!renderer->renderTargetBound)
	{
		MODERNGL_GetBackbufferSize(driverData, &bbw, &bbh);
		sr.y = bbh - scissor->y - scissor->h;
	}

	if (	sr.x != renderer->scissorRect.x ||
		sr.y != renderer->scissorRect.y ||
		sr.w != renderer->scissorRect.w ||
		sr.h != renderer->scissorRect.h	)
	{
		renderer->scissorRect = sr;
		renderer->glScissor(
			sr.x,
			sr.y,
			sr.w,
			sr.h
		);
	}
}

static void MODERNGL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	SDL_memcpy(blendFactor, &renderer->blendColor, sizeof(FNA3D_Color));
}

static void MODERNGL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	if (	renderer->blendColor.r != blendFactor->r ||
		renderer->blendColor.g != blendFactor->g ||
		renderer->blendColor.b != blendFactor->b ||
		renderer->blendColor.a != blendFactor->a	)
	{
		renderer->blendColor.r = blendFactor->r;
		renderer->blendColor.g = blendFactor->g;
		renderer->blendColor.b = blendFactor->b;
		renderer->blendColor.a = blendFactor->a;
		renderer->glBlendColor(
			renderer->blendColor.r / 255.0f,
			renderer->blendColor.g / 255.0f,
			renderer->blendColor.b / 255.0f,
			renderer->blendColor.a / 255.0f
		);
	}
}

static int32_t MODERNGL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->multiSampleMask;
}

static void MODERNGL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	if (mask != renderer->multiSampleMask)
	{
		if (mask == -1)
		{
			renderer->glDisable(GL_SAMPLE_MASK);
		}
		else
		{
			if (renderer->multiSampleMask == -1)
			{
				renderer->glEnable(GL_SAMPLE_MASK);
			}
			/* FIXME: Index...? -flibit */
			renderer->glSampleMaski(0, (GLuint) mask);
		}
		renderer->multiSampleMask = mask;
	}
}

static int32_t MODERNGL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->stencilRef;
}

static void MODERNGL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	if (ref != renderer->stencilRef)
	{
		renderer->stencilRef = ref;
		if (renderer->separateStencilEnable)
		{
			renderer->glStencilFuncSeparate(
				GL_FRONT,
				XNAToGL_CompareFunc[renderer->stencilFunc],
				renderer->stencilRef,
				renderer->stencilMask
			);
			renderer->glStencilFuncSeparate(
				GL_BACK,
				XNAToGL_CompareFunc[renderer->stencilFunc],
				renderer->stencilRef,
				renderer->stencilMask
			);
		}
		else
		{
			renderer->glStencilFunc(
				XNAToGL_CompareFunc[renderer->stencilFunc],
				renderer->stencilRef,
				renderer->stencilMask
			);
		}
	}
}

/* Immutable Render States */

static void MODERNGL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	uint8_t newEnable = !(
		blendState->colorSourceBlend == FNA3D_BLEND_ONE &&
		blendState->colorDestinationBlend == FNA3D_BLEND_ZERO &&
		blendState->alphaSourceBlend == FNA3D_BLEND_ONE &&
		blendState->alphaDestinationBlend == FNA3D_BLEND_ZERO
	);

	if (newEnable != renderer->alphaBlendEnable)
	{
		renderer->alphaBlendEnable = newEnable;
		ToggleGLState(renderer, GL_BLEND, renderer->alphaBlendEnable);
	}

	if (renderer->alphaBlendEnable)
	{
		if (	blendState->blendFactor.r != renderer->blendColor.r ||
			blendState->blendFactor.g != renderer->blendColor.g ||
			blendState->blendFactor.b != renderer->blendColor.b ||
			blendState->blendFactor.a != renderer->blendColor.a	)
		{
			renderer->blendColor = blendState->blendFactor;
			renderer->glBlendColor(
				renderer->blendColor.r / 255.0f,
				renderer->blendColor.g / 255.0f,
				renderer->blendColor.b / 255.0f,
				renderer->blendColor.a / 255.0f
			);
		}

		if (	blendState->colorSourceBlend != renderer->srcBlend ||
			blendState->colorDestinationBlend != renderer->dstBlend ||
			blendState->alphaSourceBlend != renderer->srcBlendAlpha ||
			blendState->alphaDestinationBlend != renderer->dstBlendAlpha	)
		{
			renderer->srcBlend = blendState->colorSourceBlend;
			renderer->dstBlend = blendState->colorDestinationBlend;
			renderer->srcBlendAlpha = blendState->alphaSourceBlend;
			renderer->dstBlendAlpha = blendState->alphaDestinationBlend;
			renderer->glBlendFuncSeparate(
				XNAToGL_BlendMode[renderer->srcBlend],
				XNAToGL_BlendMode[renderer->dstBlend],
				XNAToGL_BlendMode[renderer->srcBlendAlpha],
				XNAToGL_BlendMode[renderer->dstBlendAlpha]
			);
		}

		if (	blendState->colorBlendFunction != renderer->blendOp ||
			blendState->alphaBlendFunction != renderer->blendOpAlpha	)
		{
			renderer->blendOp = blendState->colorBlendFunction;
			renderer->blendOpAlpha = blendState->alphaBlendFunction;
			renderer->glBlendEquationSeparate(
				XNAToGL_BlendEquation[renderer->blendOp],
				XNAToGL_BlendEquation[renderer->blendOpAlpha]
			);
		}
	}

	if (blendState->colorWriteEnable != renderer->colorWriteEnable)
	{
		renderer->colorWriteEnable = blendState->colorWriteEnable;
		renderer->glColorMask(
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(renderer->colorWriteEnable & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
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
	if (blendState->colorWriteEnable1 != renderer->colorWriteEnable1)
	{
		renderer->colorWriteEnable1 = blendState->colorWriteEnable1;
		renderer->glColorMaski(
			1,
			(renderer->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(renderer->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(renderer->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(renderer->colorWriteEnable1 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (blendState->colorWriteEnable2 != renderer->colorWriteEnable2)
	{
		renderer->colorWriteEnable2 = blendState->colorWriteEnable2;
		renderer->glColorMaski(
			2,
			(renderer->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(renderer->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(renderer->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(renderer->colorWriteEnable2 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}
	if (blendState->colorWriteEnable3 != renderer->colorWriteEnable3)
	{
		renderer->colorWriteEnable3 = blendState->colorWriteEnable3;
		renderer->glColorMaski(
			3,
			(renderer->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_RED) != 0,
			(renderer->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_GREEN) != 0,
			(renderer->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_BLUE) != 0,
			(renderer->colorWriteEnable3 & FNA3D_COLORWRITECHANNELS_ALPHA) != 0
		);
	}

	if (blendState->multiSampleMask != renderer->multiSampleMask)
	{
		if (blendState->multiSampleMask == -1)
		{
			renderer->glDisable(GL_SAMPLE_MASK);
		}
		else
		{
			if (renderer->multiSampleMask == -1)
			{
				renderer->glEnable(GL_SAMPLE_MASK);
			}
			/* FIXME: index...? -flibit */
			renderer->glSampleMaski(0, (uint32_t) blendState->multiSampleMask);
		}
		renderer->multiSampleMask = blendState->multiSampleMask;
	}
}

static void MODERNGL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	if (depthStencilState->depthBufferEnable != renderer->zEnable)
	{
		renderer->zEnable = depthStencilState->depthBufferEnable;
		ToggleGLState(renderer, GL_DEPTH_TEST, renderer->zEnable);
	}

	if (renderer->zEnable)
	{
		if (depthStencilState->depthBufferWriteEnable != renderer->zWriteEnable)
		{
			renderer->zWriteEnable = depthStencilState->depthBufferWriteEnable;
			renderer->glDepthMask(renderer->zWriteEnable);
		}

		if (depthStencilState->depthBufferFunction != renderer->depthFunc)
		{
			renderer->depthFunc = depthStencilState->depthBufferFunction;
			renderer->glDepthFunc(XNAToGL_CompareFunc[renderer->depthFunc]);
		}
	}

	if (depthStencilState->stencilEnable != renderer->stencilEnable)
	{
		renderer->stencilEnable = depthStencilState->stencilEnable;
		ToggleGLState(renderer, GL_STENCIL_TEST, renderer->stencilEnable);
	}

	if (renderer->stencilEnable)
	{
		if (depthStencilState->stencilWriteMask != renderer->stencilWriteMask)
		{
			renderer->stencilWriteMask = depthStencilState->stencilWriteMask;
			renderer->glStencilMask(renderer->stencilWriteMask);
		}

		/* TODO: Can we split up StencilFunc/StencilOp nicely? -flibit */
		if (	depthStencilState->twoSidedStencilMode != renderer->separateStencilEnable ||
			depthStencilState->referenceStencil != renderer->stencilRef ||
			depthStencilState->stencilMask != renderer->stencilMask ||
			depthStencilState->stencilFunction != renderer->stencilFunc ||
			depthStencilState->ccwStencilFunction != renderer->ccwStencilFunc ||
			depthStencilState->stencilFail != renderer->stencilFail ||
			depthStencilState->stencilDepthBufferFail != renderer->stencilZFail ||
			depthStencilState->stencilPass != renderer->stencilPass ||
			depthStencilState->ccwStencilFail != renderer->ccwStencilFail ||
			depthStencilState->ccwStencilDepthBufferFail != renderer->ccwStencilZFail ||
			depthStencilState->ccwStencilPass != renderer->ccwStencilPass			)
		{
			renderer->separateStencilEnable = depthStencilState->twoSidedStencilMode;
			renderer->stencilRef = depthStencilState->referenceStencil;
			renderer->stencilMask = depthStencilState->stencilMask;
			renderer->stencilFunc = depthStencilState->stencilFunction;
			renderer->stencilFail = depthStencilState->stencilFail;
			renderer->stencilZFail = depthStencilState->stencilDepthBufferFail;
			renderer->stencilPass = depthStencilState->stencilPass;
			if (renderer->separateStencilEnable)
			{
				renderer->ccwStencilFunc = depthStencilState->ccwStencilFunction;
				renderer->ccwStencilFail = depthStencilState->ccwStencilFail;
				renderer->ccwStencilZFail = depthStencilState->ccwStencilDepthBufferFail;
				renderer->ccwStencilPass = depthStencilState->ccwStencilPass;
				renderer->glStencilFuncSeparate(
					GL_FRONT,
					XNAToGL_CompareFunc[renderer->stencilFunc],
					renderer->stencilRef,
					renderer->stencilMask
				);
				renderer->glStencilFuncSeparate(
					GL_BACK,
					XNAToGL_CompareFunc[renderer->ccwStencilFunc],
					renderer->stencilRef,
					renderer->stencilMask
				);
				renderer->glStencilOpSeparate(
					GL_FRONT,
					XNAToGL_GLStencilOp[renderer->stencilFail],
					XNAToGL_GLStencilOp[renderer->stencilZFail],
					XNAToGL_GLStencilOp[renderer->stencilPass]
				);
				renderer->glStencilOpSeparate(
					GL_BACK,
					XNAToGL_GLStencilOp[renderer->ccwStencilFail],
					XNAToGL_GLStencilOp[renderer->ccwStencilZFail],
					XNAToGL_GLStencilOp[renderer->ccwStencilPass]
				);
			}
			else
			{
				renderer->glStencilFunc(
					XNAToGL_CompareFunc[renderer->stencilFunc],
					renderer->stencilRef,
					renderer->stencilMask
				);
				renderer->glStencilOp(
					XNAToGL_GLStencilOp[renderer->stencilFail],
					XNAToGL_GLStencilOp[renderer->stencilZFail],
					XNAToGL_GLStencilOp[renderer->stencilPass]
				);
			}
		}
	}
}

static void MODERNGL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	FNA3D_CullMode actualMode;
	float realDepthBias;

	if (rasterizerState->scissorTestEnable != renderer->scissorTestEnable)
	{
		renderer->scissorTestEnable = rasterizerState->scissorTestEnable;
		ToggleGLState(renderer, GL_SCISSOR_TEST, renderer->scissorTestEnable);
	}

	if (renderer->renderTargetBound)
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
	if (actualMode != renderer->cullFrontFace)
	{
		if ((actualMode == FNA3D_CULLMODE_NONE) != (renderer->cullFrontFace == FNA3D_CULLMODE_NONE))
		{
			ToggleGLState(renderer, GL_CULL_FACE, actualMode != FNA3D_CULLMODE_NONE);
		}
		renderer->cullFrontFace = actualMode;
		if (renderer->cullFrontFace != FNA3D_CULLMODE_NONE)
		{
			renderer->glFrontFace(XNAToGL_FrontFace[renderer->cullFrontFace]);
		}
	}

	if (rasterizerState->fillMode != renderer->fillMode)
	{
		renderer->fillMode = rasterizerState->fillMode;
		renderer->glPolygonMode(
			GL_FRONT_AND_BACK,
			XNAToGL_GLFillMode[renderer->fillMode]
		);
	}

	realDepthBias = rasterizerState->depthBias * XNAToGL_DepthBiasScale[
		renderer->renderTargetBound ?
			renderer->currentDepthStencilFormat :
			renderer->backbuffer->depthFormat
	];
	if (	realDepthBias != renderer->depthBias ||
		rasterizerState->slopeScaleDepthBias != renderer->slopeScaleDepthBias	)
	{
		if (	realDepthBias == 0.0f &&
			rasterizerState->slopeScaleDepthBias == 0.0f)
		{
			/* We're changing to disabled bias, disable! */
			renderer->glDisable(GL_POLYGON_OFFSET_FILL);
		}
		else
		{
			if (renderer->depthBias == 0.0f && renderer->slopeScaleDepthBias == 0.0f)
			{
				/* We're changing away from disabled bias, enable! */
				renderer->glEnable(GL_POLYGON_OFFSET_FILL);
			}
			renderer->glPolygonOffset(
				rasterizerState->slopeScaleDepthBias,
				realDepthBias
			);
		}
		renderer->depthBias = realDepthBias;
		renderer->slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
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
	if (rasterizerState->multiSampleAntiAlias != renderer->multiSampleEnable)
	{
		renderer->multiSampleEnable = rasterizerState->multiSampleAntiAlias;
		ToggleGLState(renderer, GL_MULTISAMPLE, renderer->multiSampleEnable);
	}
}

static void MODERNGL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	GLuint slot;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *tex = (ModernGLTexture*) texture;

	if (texture == NULL)
	{
		if (renderer->textures[index] != &NullTexture)
		{
			renderer->glBindTextureUnit(index, 0);
			renderer->textures[index] = &NullTexture;
		}
		return;
	}

	/* Bind the correct texture */
	if (tex != renderer->textures[index])
	{
		if (tex->target != renderer->textures[index]->target)
		{
			/* If we're changing targets, unbind the old texture first! */
			renderer->glBindTextureUnit(index, 0);
		}
		renderer->glBindTextureUnit(index, tex->handle);
		renderer->textures[index] = tex;
	}

	/* Apply the sampler states */
	slot = renderer->samplers[index];

	if (sampler->addressU != renderer->samplersU[index])
	{
		renderer->samplersU[index] = sampler->addressU;
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_WRAP_S,
			XNAToGL_Wrap[sampler->addressU]
		);
	}
	if (sampler->addressV != renderer->samplersV[index])
	{
		renderer->samplersV[index] = sampler->addressV;
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_WRAP_T,
			XNAToGL_Wrap[sampler->addressV]
		);
	}
	if (sampler->addressW != renderer->samplersW[index])
	{
		renderer->samplersW[index] = sampler->addressW;
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_WRAP_R,
			XNAToGL_Wrap[sampler->addressW]
		);
	}
	if (	sampler->filter != renderer->samplersFilter[index] ||
		sampler->maxAnisotropy != renderer->samplersAnisotropy[index] ||
		tex->hasMipmaps != renderer->samplersMipped[index]	)
	{
		renderer->samplersFilter[index] = sampler->filter;
		renderer->samplersAnisotropy[index] = (float) sampler->maxAnisotropy;
		renderer->samplersMipped[index] = tex->hasMipmaps;
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_MAG_FILTER,
			XNAToGL_MagFilter[sampler->filter]
		);
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_MIN_FILTER,
			tex->hasMipmaps ?
				XNAToGL_MinMipFilter[sampler->filter] :
				XNAToGL_MinFilter[sampler->filter]
		);
		renderer->glSamplerParameterf(
			slot,
			GL_TEXTURE_MAX_ANISOTROPY_EXT,
			(sampler->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC) ?
				SDL_max(renderer->samplersAnisotropy[index], 1.0f) :
				1.0f
		);
	}
	if (sampler->maxMipLevel != renderer->samplersMaxLevel[index])
	{
		renderer->samplersMaxLevel[index] = sampler->maxMipLevel;
		renderer->glSamplerParameteri(
			slot,
			GL_TEXTURE_BASE_LEVEL,
			sampler->maxMipLevel
		);
	}
	if (sampler->mipMapLevelOfDetailBias != renderer->samplersLODBias[index])
	{
		renderer->samplersLODBias[index] = sampler->mipMapLevelOfDetailBias;
		renderer->glSamplerParameterf(
			slot,
			GL_TEXTURE_LOD_BIAS,
			sampler->mipMapLevelOfDetailBias
		);
	}
}

/* Vertex State */

static inline void MODERNGL_INTERNAL_FlushGLVertexAttributes(
	ModernGLRenderer *renderer
) {
	int32_t i, divisor;
	for (i = 0; i < renderer->numVertexAttributes; i += 1)
	{
		if (renderer->attributeEnabled[i])
		{
			renderer->attributeEnabled[i] = 0;
			if (!renderer->previousAttributeEnabled[i])
			{
				renderer->glEnableVertexAttribArray(i);
				renderer->previousAttributeEnabled[i] = 1;
			}
		}
		else if (renderer->previousAttributeEnabled[i])
		{
			renderer->glDisableVertexAttribArray(i);
			renderer->previousAttributeEnabled[i] = 0;
		}

		divisor = renderer->attributeDivisor[i];
		if (divisor != renderer->previousAttributeDivisor[i])
		{
			renderer->glVertexAttribDivisor(i, divisor);
			renderer->previousAttributeDivisor[i] = divisor;
		}
	}
}

static void MODERNGL_ApplyVertexBufferBindings(
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
	ModernGLVertexAttribute *attr;
	ModernGLBuffer *buffer;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	if (	bindingsUpdated ||
		baseVertex != renderer->ldBaseVertex ||
		renderer->currentEffect != renderer->ldEffect ||
		renderer->currentTechnique != renderer->ldTechnique ||
		renderer->currentPass != renderer->ldPass ||
		renderer->effectApplied	)
	{
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
			buffer = (ModernGLBuffer*) bindings[i].vertexBuffer;
			BindVertexBuffer(renderer, buffer->handle);
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
				attribLoc = MOJOSHADER_glGetVertexAttribLocation(
					VertexAttribUsage(usage),
					index
				);
				if (attribLoc == -1)
				{
					/* Stream not in use! */
					continue;
				}
				renderer->attributeEnabled[attribLoc] = 1;
				attr = &renderer->attributes[attribLoc];
				ptr = basePtr + element->offset;
				normalized = XNAToGL_VertexAttribNormalized(element);
				if (	attr->currentBuffer != buffer->handle ||
					attr->currentPointer != ptr ||
					attr->currentFormat != element->vertexElementFormat ||
					attr->currentNormalized != normalized ||
					attr->currentStride != vertexDeclaration->vertexStride	)
				{
					renderer->glVertexAttribPointer(
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
				renderer->attributeDivisor[attribLoc] = bindings[i].instanceFrequency;
			}
		}
		MODERNGL_INTERNAL_FlushGLVertexAttributes(renderer);

		renderer->ldBaseVertex = baseVertex;
		renderer->ldEffect = renderer->currentEffect;
		renderer->ldTechnique = renderer->currentTechnique;
		renderer->ldPass = renderer->currentPass;
		renderer->effectApplied = 0;
		renderer->ldVertexDeclaration = NULL;
		renderer->ldPointer = NULL;
	}

	MOJOSHADER_glProgramReady();
	MOJOSHADER_glProgramViewportInfo(
		renderer->viewport.w, renderer->viewport.h,
		renderer->backbuffer->width, renderer->backbuffer->height,
		renderer->renderTargetBound
	);
}

static void MODERNGL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* vertexData,
	int32_t vertexOffset
) {
	int32_t usage, index, attribLoc, i, j;
	FNA3D_VertexElement *element;
	ModernGLVertexAttribute *attr;
	uint8_t normalized;
	uint8_t *finalPtr;
	uint8_t *basePtr = (uint8_t*) vertexData;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	BindVertexBuffer(renderer, 0);
	basePtr += (vertexDeclaration->vertexStride * vertexOffset);

	if (	vertexDeclaration != renderer->ldVertexDeclaration ||
		basePtr != renderer->ldPointer ||
		renderer->currentEffect != renderer->ldEffect ||
		renderer->currentTechnique != renderer->ldTechnique ||
		renderer->currentPass != renderer->ldPass ||
		renderer->effectApplied	)
	{
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
			element = &vertexDeclaration->elements[i];
			usage = element->vertexElementUsage;
			index = element->usageIndex;
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
			attribLoc = MOJOSHADER_glGetVertexAttribLocation(
				VertexAttribUsage(usage),
				index
			);
			if (attribLoc == -1)
			{
				/* Stream not used! */
				continue;
			}
			renderer->attributeEnabled[attribLoc] = 1;
			attr = &renderer->attributes[attribLoc];
			finalPtr = basePtr + element->offset;
			normalized = XNAToGL_VertexAttribNormalized(element);
			if (	attr->currentBuffer != 0 ||
				attr->currentPointer != finalPtr ||
				attr->currentFormat != element->vertexElementFormat ||
				attr->currentNormalized != normalized ||
				attr->currentStride != vertexDeclaration->vertexStride	)
			{
				renderer->glVertexAttribPointer(
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
			renderer->attributeDivisor[attribLoc] = 0;
		}
		MODERNGL_INTERNAL_FlushGLVertexAttributes(renderer);

		renderer->ldVertexDeclaration = vertexDeclaration;
		renderer->ldPointer = vertexData;
		renderer->ldEffect = renderer->currentEffect;
		renderer->ldTechnique = renderer->currentTechnique;
		renderer->ldPass = renderer->currentPass;
		renderer->effectApplied = 0;
		renderer->ldBaseVertex = -1;
	}

	MOJOSHADER_glProgramReady();
	MOJOSHADER_glProgramViewportInfo(
		renderer->viewport.w, renderer->viewport.h,
		renderer->backbuffer->width, renderer->backbuffer->height,
		renderer->renderTargetBound
	);
}

/* Render Targets */

static void MODERNGL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLRenderbuffer *rb = (ModernGLRenderbuffer*) depthStencilBuffer;
	FNA3D_RenderTargetBinding *rt;
	int32_t i;
	GLuint handle;

	/* Bind the right framebuffer, if needed */
	if (renderTargets == NULL)
	{
		BindFramebuffer(
			renderer,
			renderer->backbuffer->type == BACKBUFFER_TYPE_OPENGL ?
				renderer->backbuffer->opengl.handle :
				0
		);
		renderer->renderTargetBound = 0;
		return;
	}
	else
	{
		BindFramebuffer(renderer, renderer->targetFramebuffer);
		renderer->renderTargetBound = 1;
	}

	for (i = 0; i < numRenderTargets; i += 1)
	{
		rt = &renderTargets[i];
		if (rt->colorBuffer != NULL)
		{
			renderer->attachments[i] = ((ModernGLRenderbuffer*) rt->colorBuffer)->handle;
			renderer->attachmentTypes[i] = GL_RENDERBUFFER;
		}
		else
		{
			renderer->attachments[i] = ((ModernGLTexture*) rt->texture)->handle;
			if (rt->type == FNA3D_RENDERTARGET_TYPE_2D)
			{
				renderer->attachmentTypes[i] = GL_TEXTURE_2D;
			}
			else
			{
				renderer->attachmentTypes[i] = GL_TEXTURE_CUBE_MAP_POSITIVE_X + rt->cube.face;
			}
		}
	}

	/* Update the color attachments, DrawBuffers state */
	for (i = 0; i < numRenderTargets; i += 1)
	{
		if (renderer->attachments[i] != renderer->currentAttachments[i])
		{
			if (renderer->currentAttachments[i] != 0)
			{
				if (	renderer->attachmentTypes[i] != GL_RENDERBUFFER &&
					renderer->currentAttachmentTypes[i] == GL_RENDERBUFFER	)
				{
					renderer->glNamedFramebufferRenderbuffer(
						renderer->targetFramebuffer,
						GL_COLOR_ATTACHMENT0 + i,
						GL_RENDERBUFFER,
						0
					);
				}
				else if (	renderer->attachmentTypes[i] == GL_RENDERBUFFER &&
						renderer->currentAttachmentTypes[i] != GL_RENDERBUFFER	)
				{
					if (renderer->currentAttachmentTypes[i] == GL_TEXTURE_2D)
					{
						renderer->glNamedFramebufferTexture(
							renderer->targetFramebuffer,
							GL_COLOR_ATTACHMENT0 + i,
							0,
							0
						);
					}
					else
					{
						renderer->glNamedFramebufferTextureLayer(
							renderer->targetFramebuffer,
							GL_COLOR_ATTACHMENT0 + i,
							0,
							0,
							renderer->currentAttachmentTypes[i] - GL_TEXTURE_CUBE_MAP_POSITIVE_X
						);
					}
				}
			}
			if (renderer->attachmentTypes[i] == GL_RENDERBUFFER)
			{
				renderer->glNamedFramebufferRenderbuffer(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					GL_RENDERBUFFER,
					renderer->attachments[i]
				);
			}
			else if (renderer->attachmentTypes[i] == GL_TEXTURE_2D)
			{
				renderer->glNamedFramebufferTexture(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					renderer->attachments[i],
					0
				);
			}
			else
			{
				renderer->glNamedFramebufferTextureLayer(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					renderer->attachments[i],
					0,
					renderer->attachmentTypes[i] - GL_TEXTURE_CUBE_MAP_POSITIVE_X
				);
			}
			renderer->currentAttachments[i] = renderer->attachments[i];
			renderer->currentAttachmentTypes[i] = renderer->attachmentTypes[i];
		}
		else if (renderer->attachmentTypes[i] != renderer->currentAttachmentTypes[i])
		{
			/* Texture cube face change! */
			renderer->glNamedFramebufferTextureLayer(
				renderer->targetFramebuffer,
				GL_COLOR_ATTACHMENT0 + i,
				renderer->attachments[i],
				0,
				renderer->attachmentTypes[i] - GL_TEXTURE_CUBE_MAP_POSITIVE_X
			);
			renderer->currentAttachmentTypes[i] = renderer->attachmentTypes[i];
		}
	}
	while (i < renderer->numAttachments)
	{
		if (renderer->currentAttachments[i] != 0)
		{
			if (renderer->currentAttachmentTypes[i] == GL_RENDERBUFFER)
			{
				renderer->glNamedFramebufferRenderbuffer(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					GL_RENDERBUFFER,
					0
				);
			}
			else if (renderer->currentAttachmentTypes[i] == GL_TEXTURE_2D)
			{
				renderer->glNamedFramebufferTexture(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					0,
					0
				);
			}
			else
			{
				renderer->glNamedFramebufferTextureLayer(
					renderer->targetFramebuffer,
					GL_COLOR_ATTACHMENT0 + i,
					0,
					0,
					renderer->currentAttachmentTypes[i] - GL_TEXTURE_CUBE_MAP_POSITIVE_X
				);
			}
			renderer->currentAttachments[i] = 0;
			renderer->currentAttachmentTypes[i] = GL_TEXTURE_2D;
		}
		i += 1;
	}
	if (numRenderTargets != renderer->currentDrawBuffers)
	{
		renderer->glNamedFramebufferDrawBuffers(
			renderer->targetFramebuffer,
			numRenderTargets,
			renderer->drawBuffersArray
		);
		renderer->currentDrawBuffers = numRenderTargets;
	}

	/* Update the depth/stencil attachment */
	/* FIXME: Notice that we do separate attach calls for the stencil.
	 * We _should_ be able to do a single attach for depthstencil, but
	 * some drivers (like Mesa) cannot into GL_DEPTH_STENCIL_ATTACHMENT.
	 * Use XNAToGL.DepthStencilAttachment when this isn't a problem.
	 * -flibit
	 */
	if (depthStencilBuffer == NULL)
	{
		handle = 0;
	}
	else
	{
		handle = rb->handle;
	}
	if (handle != renderer->currentRenderbuffer)
	{
		if (renderer->currentDepthStencilFormat == FNA3D_DEPTHFORMAT_D24S8)
		{
			renderer->glNamedFramebufferRenderbuffer(
				renderer->targetFramebuffer,
				GL_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER,
				0
			);
		}
		renderer->currentDepthStencilFormat = depthFormat;
		renderer->glNamedFramebufferRenderbuffer(
			renderer->targetFramebuffer,
			GL_DEPTH_ATTACHMENT,
			GL_RENDERBUFFER,
			handle
		);
		if (renderer->currentDepthStencilFormat == FNA3D_DEPTHFORMAT_D24S8)
		{
			renderer->glNamedFramebufferRenderbuffer(
				renderer->targetFramebuffer,
				GL_STENCIL_ATTACHMENT,
				GL_RENDERBUFFER,
				handle
			);
		}
		renderer->currentRenderbuffer = handle;
	}
}

static void MODERNGL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *rtTex = (ModernGLTexture*) target->texture;
	int32_t width, height;

	if (target->multiSampleCount > 0)
	{
		/* Set up the texture framebuffer */
		if (target->type == FNA3D_RENDERTARGET_TYPE_2D)
		{
			renderer->glNamedFramebufferTexture(
				renderer->resolveFramebufferDraw,
				GL_COLOR_ATTACHMENT0,
				rtTex->handle,
				0
			);
			width = target->twod.width;
			height = target->twod.height;
		}
		else
		{
			renderer->glNamedFramebufferTextureLayer(
				renderer->resolveFramebufferDraw,
				GL_COLOR_ATTACHMENT0,
				rtTex->handle,
				0,
				target->cube.face
			);
			width = target->cube.size;
			height = target->cube.size;
		}

		/* Set up the renderbuffer framebuffer */
		renderer->glNamedFramebufferRenderbuffer(
			renderer->resolveFramebufferRead,
			GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER,
			((ModernGLRenderbuffer*) target->colorBuffer)->handle
		);

		/* Blit! */
		if (renderer->scissorTestEnable)
		{
			renderer->glDisable(GL_SCISSOR_TEST);
		}
		renderer->glBlitNamedFramebuffer(
			renderer->resolveFramebufferRead,
			renderer->resolveFramebufferDraw,
			0, 0, width, height,
			0, 0, width, height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR
		);
		/* Invalidate the MSAA buffer */
		renderer->glInvalidateNamedFramebufferData(
			renderer->resolveFramebufferRead,
			renderer->numAttachments + 2,
			renderer->drawBuffersArray
		);
		if (renderer->scissorTestEnable)
		{
			renderer->glEnable(GL_SCISSOR_TEST);
		}
	}

	/* If the target has mipmaps, regenerate them now */
	if (target->levelCount > 1)
	{
		renderer->glGenerateTextureMipmap(rtTex->handle);
	}
}

/* Backbuffer Functions */

static void MODERNGL_INTERNAL_CreateBackbuffer(
	ModernGLRenderer *renderer,
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
		if (	renderer->backbuffer == NULL ||
			renderer->backbuffer->type == BACKBUFFER_TYPE_NULL	)
		{
			if (renderer->backbuffer != NULL)
			{
				SDL_free(renderer->backbuffer);
			}
			renderer->backbuffer = (ModernGLBackbuffer*) SDL_malloc(
				sizeof(ModernGLBackbuffer)
			);
			renderer->backbuffer->type = BACKBUFFER_TYPE_OPENGL;

			renderer->backbuffer->width = parameters->backBufferWidth;
			renderer->backbuffer->height = parameters->backBufferHeight;
			renderer->backbuffer->depthFormat = parameters->depthStencilFormat;
			renderer->backbuffer->multiSampleCount = parameters->multiSampleCount;
			renderer->backbuffer->opengl.texture = 0;

			/* Generate and bind the FBO. */
			renderer->glCreateFramebuffers(
				1,
				&renderer->backbuffer->opengl.handle
			);
			BindFramebuffer(
				renderer,
				renderer->backbuffer->opengl.handle
			);

			/* Create and attach the color buffer */
			renderer->glCreateRenderbuffers(
				1,
				&renderer->backbuffer->opengl.colorAttachment
			);
			if (renderer->backbuffer->multiSampleCount > 0)
			{
				renderer->glNamedRenderbufferStorageMultisample(
					renderer->backbuffer->opengl.colorAttachment,
					renderer->backbuffer->multiSampleCount,
					GL_RGBA8,
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			else
			{
				renderer->glNamedRenderbufferStorage(
					renderer->backbuffer->opengl.colorAttachment,
					GL_RGBA8,
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			renderer->glNamedFramebufferRenderbuffer(
				renderer->backbuffer->opengl.handle,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				renderer->backbuffer->opengl.colorAttachment
			);

			if (renderer->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				/* Don't bother creating a DS buffer */
				renderer->backbuffer->opengl.depthStencilAttachment = 0;
				return;
			}

			renderer->glCreateRenderbuffers(
				1,
				&renderer->backbuffer->opengl.depthStencilAttachment
			);
			if (renderer->backbuffer->multiSampleCount > 0)
			{
				renderer->glNamedRenderbufferStorageMultisample(
					renderer->backbuffer->opengl.depthStencilAttachment,
					renderer->backbuffer->multiSampleCount,
					XNAToGL_DepthStorage[
						renderer->backbuffer->depthFormat
					],
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			else
			{
				renderer->glNamedRenderbufferStorage(
					renderer->backbuffer->opengl.depthStencilAttachment,
					XNAToGL_DepthStorage[
						renderer->backbuffer->depthFormat
					],
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			renderer->glNamedFramebufferRenderbuffer(
				renderer->backbuffer->opengl.handle,
				GL_DEPTH_ATTACHMENT,
				GL_RENDERBUFFER,
				renderer->backbuffer->opengl.depthStencilAttachment
			);
			if (renderer->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
			{
				renderer->glNamedFramebufferRenderbuffer(
					renderer->backbuffer->opengl.handle,
					GL_STENCIL_ATTACHMENT,
					GL_RENDERBUFFER,
					renderer->backbuffer->opengl.depthStencilAttachment
				);
			}
		}
		else
		{
			renderer->backbuffer->width = parameters->backBufferWidth;
			renderer->backbuffer->height = parameters->backBufferHeight;
			renderer->backbuffer->multiSampleCount = parameters->multiSampleCount;
			if (renderer->backbuffer->opengl.texture != 0)
			{
				renderer->glDeleteTextures(
					1,
					&renderer->backbuffer->opengl.texture
				);
				renderer->backbuffer->opengl.texture = 0;
			}

			/* Detach color attachment */
			renderer->glNamedFramebufferRenderbuffer(
				renderer->backbuffer->opengl.handle,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				0
			);

			/* Detach depth/stencil attachment, if applicable */
			if (renderer->backbuffer->opengl.depthStencilAttachment != 0)
			{
				renderer->glNamedFramebufferRenderbuffer(
					renderer->backbuffer->opengl.handle,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					0
				);
				if (renderer->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
				{
					renderer->glNamedFramebufferRenderbuffer(
						renderer->backbuffer->opengl.handle,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						0
					);
				}
			}

			/* Update our color attachment to the new resolution. */
			if (renderer->backbuffer->multiSampleCount > 0)
			{
				renderer->glNamedRenderbufferStorageMultisample(
					renderer->backbuffer->opengl.colorAttachment,
					renderer->backbuffer->multiSampleCount,
					GL_RGBA8,
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			else
			{
				renderer->glNamedRenderbufferStorage(
					renderer->backbuffer->opengl.colorAttachment,
					GL_RGBA8,
					renderer->backbuffer->width,
					renderer->backbuffer->height
				);
			}
			renderer->glNamedFramebufferRenderbuffer(
				renderer->backbuffer->opengl.handle,
				GL_COLOR_ATTACHMENT0,
				GL_RENDERBUFFER,
				renderer->backbuffer->opengl.colorAttachment
			);

			/* Generate/Delete depth/stencil attachment, if needed */
			if (parameters->depthStencilFormat == FNA3D_DEPTHFORMAT_NONE)
			{
				if (renderer->backbuffer->opengl.depthStencilAttachment != 0)
				{
					renderer->glDeleteRenderbuffers(
						1,
						&renderer->backbuffer->opengl.depthStencilAttachment
					);
					renderer->backbuffer->opengl.depthStencilAttachment = 0;
				}
			}
			else if (renderer->backbuffer->opengl.depthStencilAttachment == 0)
			{
				renderer->glCreateRenderbuffers(
					1,
					&renderer->backbuffer->opengl.depthStencilAttachment
				);
			}

			/* Update the depth/stencil buffer, if applicable */
			renderer->backbuffer->depthFormat = parameters->depthStencilFormat;
			if (renderer->backbuffer->opengl.depthStencilAttachment != 0)
			{
				if (renderer->backbuffer->multiSampleCount > 0)
				{
					renderer->glNamedRenderbufferStorageMultisample(
						renderer->backbuffer->opengl.depthStencilAttachment,
						renderer->backbuffer->multiSampleCount,
						XNAToGL_DepthStorage[renderer->backbuffer->depthFormat],
						renderer->backbuffer->width,
						renderer->backbuffer->height
					);
				}
				else
				{
					renderer->glNamedRenderbufferStorage(
						renderer->backbuffer->opengl.depthStencilAttachment,
						XNAToGL_DepthStorage[renderer->backbuffer->depthFormat],
						renderer->backbuffer->width,
						renderer->backbuffer->height
					);
				}
				renderer->glNamedFramebufferRenderbuffer(
					renderer->backbuffer->opengl.handle,
					GL_DEPTH_ATTACHMENT,
					GL_RENDERBUFFER,
					renderer->backbuffer->opengl.depthStencilAttachment
				);
				if (renderer->backbuffer->depthFormat == FNA3D_DEPTHFORMAT_D24S8)
				{
					renderer->glNamedFramebufferRenderbuffer(
						renderer->backbuffer->opengl.handle,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						renderer->backbuffer->opengl.depthStencilAttachment
					);
				}
			}
		}
	}
	else
	{
		if (	renderer->backbuffer == NULL ||
			renderer->backbuffer->type == BACKBUFFER_TYPE_OPENGL	)
		{
			if (renderer->backbuffer != NULL)
			{
				MODERNGL_INTERNAL_DisposeBackbuffer(renderer);
				SDL_free(renderer->backbuffer);
			}
			renderer->backbuffer = (ModernGLBackbuffer*) SDL_malloc(
				sizeof(ModernGLBackbuffer)
			);
			renderer->backbuffer->type = BACKBUFFER_TYPE_NULL;
		}
		renderer->backbuffer->width = parameters->backBufferWidth;
		renderer->backbuffer->height = parameters->backBufferHeight;
		renderer->backbuffer->depthFormat = renderer->windowDepthFormat;
	}
}

static void MODERNGL_INTERNAL_DisposeBackbuffer(ModernGLRenderer *renderer)
{
	#define GLBACKBUFFER renderer->backbuffer->opengl

	BindFramebuffer(renderer, 0);
	renderer->glDeleteFramebuffers(1, &GLBACKBUFFER.handle);
	renderer->glDeleteRenderbuffers(1, &GLBACKBUFFER.colorAttachment);
	if (GLBACKBUFFER.depthStencilAttachment != 0)
	{
		renderer->glDeleteRenderbuffers(1, &GLBACKBUFFER.depthStencilAttachment);
	}
	if (GLBACKBUFFER.texture != 0)
	{
		renderer->glDeleteTextures(1, &GLBACKBUFFER.texture);
	}
	GLBACKBUFFER.handle = 0;

	#undef GLBACKBUFFER
}

static uint8_t MODERNGL_INTERNAL_ReadTargetIfApplicable(
	FNA3D_Renderer *driverData,
	FNA3D_Texture* textureIn,
	int32_t level,
	void* data,
	int32_t subX,
	int32_t subY,
	int32_t subW,
	int32_t subH
) {
	GLuint prevReadBuffer;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *texture = (ModernGLTexture*) textureIn;
	uint8_t texUnbound = (	renderer->currentDrawBuffers != 1 ||
				renderer->currentAttachments[0] != texture->handle	);
	if (texUnbound)
	{
		return 0;
	}

	prevReadBuffer = renderer->currentReadFramebuffer;
	BindReadFramebuffer(renderer, renderer->targetFramebuffer);

	/* glReadPixels should be faster than reading
	 * back from the render target if we are already bound.
	 */
	renderer->glReadPixels(
		subX,
		subY,
		subW,
		subH,
		GL_RGBA, /* FIXME: Assumption! */
		GL_UNSIGNED_BYTE,
		data
	);

	BindReadFramebuffer(renderer, prevReadBuffer);
	return 1;
}

static void MODERNGL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	MODERNGL_INTERNAL_CreateBackbuffer(renderer, presentationParameters);
}

static void MODERNGL_ReadBackbuffer(
	FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
	GLuint prevReadBuffer;
	int32_t pitch, row;
	uint8_t *temp;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	uint8_t *dataPtr = (uint8_t*) data;

	prevReadBuffer = renderer->currentReadFramebuffer;

	if (renderer->backbuffer->multiSampleCount > 0)
	{
		/* We have to resolve the renderbuffer to a texture first. */
		if (renderer->backbuffer->opengl.texture == 0)
		{
			renderer->glCreateTextures(
				GL_TEXTURE_2D,
				1,
				&renderer->backbuffer->opengl.texture
			);
			renderer->glTextureStorage2D(
				renderer->backbuffer->opengl.texture,
				1,
				GL_RGBA,
				renderer->backbuffer->width,
				renderer->backbuffer->height
			);
		}
		renderer->glNamedFramebufferTexture(
			renderer->backbuffer->opengl.handle,
			GL_COLOR_ATTACHMENT0,
			renderer->backbuffer->opengl.texture,
			0
		);
		renderer->glBlitNamedFramebuffer(
			renderer->backbuffer->opengl.handle,
			renderer->resolveFramebufferDraw,
			0, 0, renderer->backbuffer->width, renderer->backbuffer->height,
			0, 0, renderer->backbuffer->width, renderer->backbuffer->height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR
		);
		/* Don't invalidate the backbuffer here! */
		BindReadFramebuffer(renderer, renderer->resolveFramebufferDraw);
	}
	else
	{
		BindReadFramebuffer(
			renderer,
			(renderer->backbuffer->type == BACKBUFFER_TYPE_OPENGL) ?
				renderer->backbuffer->opengl.handle :
				0
		);
	}

	renderer->glReadPixels(
		x,
		y,
		w,
		h,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		data
	);

	BindReadFramebuffer(renderer, prevReadBuffer);

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

static void MODERNGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	*w = renderer->backbuffer->width;
	*h = renderer->backbuffer->height;
}

static FNA3D_SurfaceFormat MODERNGL_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	return FNA3D_SURFACEFORMAT_COLOR;
}

static FNA3D_DepthFormat MODERNGL_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->backbuffer->depthFormat;
}

static int32_t MODERNGL_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->backbuffer->multiSampleCount;
}

/* Textures */

static inline ModernGLTexture* MODERNGL_INTERNAL_CreateTexture(
	ModernGLRenderer *renderer,
	GLenum target,
	int32_t levelCount
) {
	ModernGLTexture *result = (ModernGLTexture*) SDL_malloc(sizeof(ModernGLTexture));
	result->target = target;
	result->hasMipmaps = levelCount > 1;
	renderer->glCreateTextures(target, 1, &result->handle);
	return result;
}

static inline int32_t MODERNGL_INTERNAL_Texture_GetPixelStoreAlignment(
	FNA3D_SurfaceFormat format
) {
	/* https://github.com/FNA-XNA/FNA/pull/238
	 * https://www.khronos.org/registry/OpenGL/specs/gl/glspec21.pdf
	 * OpenGL 2.1 Specification, section 3.6.1, table 3.1 specifies that
	 * the pixelstorei alignment cannot exceed 8
	 */
	return SDL_min(8, Texture_GetFormatSize(format));
}

static FNA3D_Texture* MODERNGL_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURE2D;
		cmd.createTexture2D.format = format;
		cmd.createTexture2D.width = width;
		cmd.createTexture2D.height = height;
		cmd.createTexture2D.levelCount = levelCount;
		cmd.createTexture2D.isRenderTarget = isRenderTarget;
		ForceToMainThread(renderer, &cmd);
		return cmd.createTexture2D.retval;
	}

	result = MODERNGL_INTERNAL_CreateTexture(
		renderer,
		GL_TEXTURE_2D,
		levelCount
	);

	renderer->glTextureStorage2D(
		result->handle,
		levelCount,
		XNAToGL_TextureInternalFormat[format],
		width,
		height
	);

	if (format == FNA3D_SURFACEFORMAT_ALPHA8)
	{
		/* Alpha8 needs a swizzle, since GL_ALPHA is unsupported */
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_R,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_G,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_B,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_A,
			GL_RED
		);
	}

	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* MODERNGL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURE3D;
		cmd.createTexture3D.format = format;
		cmd.createTexture3D.width = width;
		cmd.createTexture3D.height = height;
		cmd.createTexture3D.depth = depth;
		cmd.createTexture3D.levelCount = levelCount;
		ForceToMainThread(renderer, &cmd);
		return cmd.createTexture3D.retval;
	}

	result = MODERNGL_INTERNAL_CreateTexture(
		renderer,
		GL_TEXTURE_3D,
		levelCount
	);

	renderer->glTextureStorage3D(
		result->handle,
		levelCount,
		XNAToGL_TextureInternalFormat[format],
		width,
		height,
		depth
	);

	if (format == FNA3D_SURFACEFORMAT_ALPHA8)
	{
		/* Alpha8 needs a swizzle, since GL_ALPHA is unsupported */
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_R,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_G,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_B,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_A,
			GL_RED
		);
	}

	return (FNA3D_Texture*) result;
}

static FNA3D_Texture* MODERNGL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATETEXTURECUBE;
		cmd.createTextureCube.format = format;
		cmd.createTextureCube.size = size;
		cmd.createTextureCube.levelCount = levelCount;
		cmd.createTextureCube.isRenderTarget = isRenderTarget;
		ForceToMainThread(renderer, &cmd);
		return cmd.createTextureCube.retval;
	}

	result = MODERNGL_INTERNAL_CreateTexture(
		renderer,
		GL_TEXTURE_CUBE_MAP,
		levelCount
	);

	renderer->glTextureStorage2D(
		result->handle,
		levelCount,
		XNAToGL_TextureInternalFormat[format],
		size,
		size
	);

	if (format == FNA3D_SURFACEFORMAT_ALPHA8)
	{
		/* Alpha8 needs a swizzle, since GL_ALPHA is unsupported */
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_R,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_G,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_B,
			GL_ZERO
		);
		renderer->glTextureParameteri(
			result->handle,
			GL_TEXTURE_SWIZZLE_A,
			GL_RED
		);
	}

	return (FNA3D_Texture*) result;
}

static void MODERNGL_INTERNAL_DestroyTexture(
	ModernGLRenderer *renderer,
	ModernGLTexture *texture
) {
	int32_t i;
	for (i = 0; i < renderer->numAttachments; i += 1)
	{
		if (texture->handle == renderer->currentAttachments[i])
		{
			/* Force an attachment update, this no longer exists! */
			renderer->currentAttachments[i] = UINT32_MAX;
		}
	}
	for (i = 0; i < renderer->numTextureSlots; i += 1)
	{
		if (renderer->textures[i] == texture)
		{
			/* Remove this texture from the sampler cache */
			renderer->textures[i] = &NullTexture;
		}
	}
	renderer->glDeleteTextures(1, &texture->handle);
	SDL_free(texture);
}

static void MODERNGL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLTexture *glTexture = (ModernGLTexture*) texture;
	ModernGLTexture *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyTexture(renderer, glTexture);
	}
	else
	{
		SDL_LockMutex(renderer->disposeTexturesLock);
		LinkedList_Add(renderer->disposeTextures, glTexture, curr);
		SDL_UnlockMutex(renderer->disposeTexturesLock);
	}
}

static void MODERNGL_SetTextureData2D(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	GLenum glFormat;
	int32_t packSize;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
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
		ForceToMainThread(renderer, &cmd);
		return;
	}

	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		/* Note that we're using glInternalFormat, not glFormat.
		 * In this case, they should actually be the same thing,
		 * but we use glFormat somewhat differently for
		 * compressed textures.
		 * -flibit
		 */
		renderer->glCompressedTextureSubImage2D(
			((ModernGLTexture*) texture)->handle,
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
		packSize = MODERNGL_INTERNAL_Texture_GetPixelStoreAlignment(format);
		if (packSize != 4)
		{
			renderer->glPixelStorei(
				GL_UNPACK_ALIGNMENT,
				packSize
			);
		}

		renderer->glTextureSubImage2D(
			((ModernGLTexture*) texture)->handle,
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
			renderer->glPixelStorei(
				GL_UNPACK_ALIGNMENT,
				4
			);
		}
	}
}

static void MODERNGL_SetTextureData3D(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_SETTEXTUREDATA3D;
		cmd.setTextureData3D.texture = texture;
		cmd.setTextureData3D.format = format;
		cmd.setTextureData3D.x = x;
		cmd.setTextureData3D.y = y;
		cmd.setTextureData3D.z = z;
		cmd.setTextureData3D.w = w;
		cmd.setTextureData3D.h = h;
		cmd.setTextureData3D.d = d;
		cmd.setTextureData3D.level = level;
		cmd.setTextureData3D.data = data;
		cmd.setTextureData3D.dataLength = dataLength;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	renderer->glTextureSubImage3D(
		((ModernGLTexture*) texture)->handle,
		level,
		x,
		y,
		z,
		w,
		h,
		d,
		XNAToGL_TextureFormat[format],
		XNAToGL_TextureDataType[format],
		data
	);
}

static void MODERNGL_SetTextureDataCube(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	GLenum glFormat;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
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
		ForceToMainThread(renderer, &cmd);
		return;
	}

	glFormat = XNAToGL_TextureFormat[format];
	if (glFormat == GL_COMPRESSED_TEXTURE_FORMATS)
	{
		/* Note that we're using glInternalFormat, not glFormat.
		 * In this case, they should actually be the same thing,
		 * but we use glFormat somewhat differently for
		 * compressed textures.
		 * -flibit
		 */
		renderer->glCompressedTextureSubImage3D(
			((ModernGLTexture*) texture)->handle,
			level,
			x,
			y,
			cubeMapFace,
			w,
			h,
			1,
			XNAToGL_TextureInternalFormat[format],
			dataLength,
			data
		);
	}
	else
	{
		renderer->glTextureSubImage3D(
			((ModernGLTexture*) texture)->handle,
			level,
			x,
			y,
			cubeMapFace,
			w,
			h,
			1,
			glFormat,
			XNAToGL_TextureDataType[format],
			data
		);
	}
}

static void MODERNGL_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	uint8_t *dataPtr = (uint8_t*) ptr;

	renderer->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	renderer->glTextureSubImage2D(
		((ModernGLTexture*) y)->handle,
		0,
		0,
		0,
		w,
		h,
		GL_RED,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	dataPtr += (w * h);
	renderer->glTextureSubImage2D(
		((ModernGLTexture*) u)->handle,
		0,
		0,
		0,
		w / 2,
		h / 2,
		GL_RED,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	dataPtr += (w / 2) * (h / 2);
	renderer->glTextureSubImage2D(
		((ModernGLTexture*) v)->handle,
		0,
		0,
		0,
		w / 2,
		h / 2,
		GL_RED,
		GL_UNSIGNED_BYTE,
		dataPtr
	);
	renderer->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void MODERNGL_GetTextureData2D(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETTEXTUREDATA2D;
		cmd.getTextureData2D.texture = texture;
		cmd.getTextureData2D.format = format;
		cmd.getTextureData2D.x = x;
		cmd.getTextureData2D.y = y;
		cmd.getTextureData2D.w = w;
		cmd.getTextureData2D.h = h;
		cmd.getTextureData2D.level = level;
		cmd.getTextureData2D.data = data;
		cmd.getTextureData2D.dataLength = dataLength;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	if (level == 0 && MODERNGL_INTERNAL_ReadTargetIfApplicable(
		driverData,
		texture,
		level,
		data,
		x,
		y,
		w,
		h
	)) {
		return;
	}

	renderer->glGetTextureSubImage(
		((ModernGLTexture*) texture)->handle,
		level,
		x,
		y,
		0,
		w,
		h,
		1,
		XNAToGL_TextureFormat[format],
		XNAToGL_TextureDataType[format],
		dataLength,
		data
	);
}

static void MODERNGL_GetTextureData3D(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETTEXTUREDATA3D;
		cmd.getTextureData3D.texture = texture;
		cmd.getTextureData3D.format = format;
		cmd.getTextureData3D.x = x;
		cmd.getTextureData3D.y = y;
		cmd.getTextureData3D.z = z;
		cmd.getTextureData3D.w = w;
		cmd.getTextureData3D.h = h;
		cmd.getTextureData3D.d = d;
		cmd.getTextureData3D.level = level;
		cmd.getTextureData3D.data = data;
		cmd.getTextureData3D.dataLength = dataLength;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	renderer->glGetTextureSubImage(
		((ModernGLTexture*) texture)->handle,
		level,
		x,
		y,
		z,
		w,
		h,
		d,
		XNAToGL_TextureFormat[format],
		XNAToGL_TextureDataType[format],
		dataLength,
		data
	);
}

static void MODERNGL_GetTextureDataCube(
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
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GETTEXTUREDATACUBE;
		cmd.getTextureDataCube.texture = texture;
		cmd.getTextureDataCube.format = format;
		cmd.getTextureDataCube.x = x;
		cmd.getTextureDataCube.y = y;
		cmd.getTextureDataCube.w = w;
		cmd.getTextureDataCube.h = h;
		cmd.getTextureDataCube.cubeMapFace = cubeMapFace;
		cmd.getTextureDataCube.level = level;
		cmd.getTextureDataCube.data = data;
		cmd.getTextureDataCube.dataLength = dataLength;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	renderer->glGetTextureSubImage(
		((ModernGLTexture*) texture)->handle,
		level,
		x,
		y,
		cubeMapFace,
		w,
		h,
		1,
		XNAToGL_TextureFormat[format],
		XNAToGL_TextureDataType[format],
		dataLength,
		data
	);
}

/* Renderbuffers */

static FNA3D_Renderbuffer* MODERNGL_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLRenderbuffer *renderbuffer;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENCOLORRENDERBUFFER;
		cmd.genColorRenderbuffer.width = width;
		cmd.genColorRenderbuffer.height = height;
		cmd.genColorRenderbuffer.format = format;
		cmd.genColorRenderbuffer.multiSampleCount = multiSampleCount;
		cmd.genColorRenderbuffer.texture = texture;
		ForceToMainThread(renderer, &cmd);
		return cmd.genColorRenderbuffer.retval;
	}

	renderbuffer = (ModernGLRenderbuffer*) SDL_malloc(
		sizeof(ModernGLRenderbuffer)
	);
	renderbuffer->next = NULL;

	renderer->glCreateRenderbuffers(1, &renderbuffer->handle);
	if (multiSampleCount > 0)
	{
		renderer->glNamedRenderbufferStorageMultisample(
			renderbuffer->handle,
			multiSampleCount,
			XNAToGL_TextureInternalFormat[format],
			width,
			height
		);
	}
	else
	{
		renderer->glNamedRenderbufferStorage(
			renderbuffer->handle,
			XNAToGL_TextureInternalFormat[format],
			width,
			height
		);
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static FNA3D_Renderbuffer* MODERNGL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLRenderbuffer *renderbuffer;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENDEPTHRENDERBUFFER;
		cmd.genDepthStencilRenderbuffer.width = width;
		cmd.genDepthStencilRenderbuffer.height = height;
		cmd.genDepthStencilRenderbuffer.format = format;
		cmd.genDepthStencilRenderbuffer.multiSampleCount = multiSampleCount;
		ForceToMainThread(renderer, &cmd);
		return cmd.genDepthStencilRenderbuffer.retval;
	}

	renderbuffer = (ModernGLRenderbuffer*) SDL_malloc(
		sizeof(ModernGLRenderbuffer)
	);
	renderbuffer->next = NULL;

	renderer->glCreateRenderbuffers(1, &renderbuffer->handle);
	if (multiSampleCount > 0)
	{
		renderer->glNamedRenderbufferStorageMultisample(
			renderbuffer->handle,
			multiSampleCount,
			XNAToGL_DepthStorage[format],
			width,
			height
		);
	}
	else
	{
		renderer->glNamedRenderbufferStorage(
			renderbuffer->handle,
			XNAToGL_DepthStorage[format],
			width,
			height
		);
	}

	return (FNA3D_Renderbuffer*) renderbuffer;
}

static void MODERNGL_INTERNAL_DestroyRenderbuffer(
	ModernGLRenderer *renderer,
	ModernGLRenderbuffer *renderbuffer
) {
	/* Check color attachments */
	int32_t i;
	for (i = 0; i < renderer->numAttachments; i += 1)
	{
		if (renderbuffer->handle == renderer->currentAttachments[i])
		{
			/* Force an attachment update, this no longer exists! */
			renderer->currentAttachments[i] = UINT32_MAX;
		}
	}

	/* Check depth/stencil attachment */
	if (renderbuffer->handle == renderer->currentRenderbuffer)
	{
		/* Force a renderbuffer update, this no longer exists! */
		renderer->currentRenderbuffer = UINT32_MAX;
	}

	/* Finally. */
	renderer->glDeleteRenderbuffers(1, &renderbuffer->handle);
	SDL_free(renderbuffer);
}

static void MODERNGL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLRenderbuffer *buffer = (ModernGLRenderbuffer*) renderbuffer;
	ModernGLRenderbuffer *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyRenderbuffer(renderer, buffer);
	}
	else
	{
		SDL_LockMutex(renderer->disposeRenderbuffersLock);
		LinkedList_Add(renderer->disposeRenderbuffers, buffer, curr);
		SDL_UnlockMutex(renderer->disposeRenderbuffersLock);
	}
}

/* Vertex Buffers */

static FNA3D_Buffer* MODERNGL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENVERTEXBUFFER;
		cmd.genVertexBuffer.dynamic = dynamic;
		cmd.genVertexBuffer.usage = usage;
		cmd.genVertexBuffer.vertexCount = vertexCount;
		cmd.genVertexBuffer.vertexStride = vertexStride;
		ForceToMainThread(renderer, &cmd);
		return cmd.genVertexBuffer.retval;
	}

	result = (ModernGLBuffer*) SDL_malloc(sizeof(ModernGLBuffer));
	renderer->glCreateBuffers(1, &result->handle);
	result->size = vertexStride * vertexCount;
	result->flags = (
		GL_MAP_PERSISTENT_BIT |
		GL_MAP_COHERENT_BIT |
		GL_MAP_WRITE_BIT
	);
	if (usage == FNA3D_BUFFERUSAGE_NONE)
	{
		result->flags |= GL_MAP_READ_BIT;
	}

	if (dynamic)
	{
		renderer->glNamedBufferStorage(
			result->handle,
			result->size,
			NULL,
			result->flags | GL_DYNAMIC_STORAGE_BIT
		);

		result->pin = (uint8_t*) renderer->glMapNamedBufferRange(
			result->handle,
			0,
			result->size,
			result->flags
		);
	}
	else
	{
		renderer->glNamedBufferData(
			result->handle,
			result->size,
			NULL,
			GL_STATIC_DRAW
		);
	}

	return (FNA3D_Buffer*) result;
}

static void MODERNGL_INTERNAL_DestroyVertexBuffer(
	ModernGLRenderer *renderer,
	ModernGLBuffer *buffer
) {
	int32_t i;

	if (buffer->handle == renderer->currentVertexBuffer)
	{
		renderer->glBindBuffer(GL_ARRAY_BUFFER, 0);
		renderer->currentVertexBuffer = 0;
	}
	for (i = 0; i < renderer->numVertexAttributes; i += 1)
	{
		if (buffer->handle == renderer->attributes[i].currentBuffer)
		{
			/* Force the next vertex attrib update! */
			renderer->attributes[i].currentBuffer = UINT32_MAX;
		}
	}
	renderer->glDeleteBuffers(1, &buffer->handle);

	SDL_free(buffer);
}

static void MODERNGL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *glBuffer = (ModernGLBuffer*) buffer;
	ModernGLBuffer *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyVertexBuffer(renderer, glBuffer);
	}
	else
	{
		SDL_LockMutex(renderer->disposeVertexBuffersLock);
		LinkedList_Add(renderer->disposeVertexBuffers, glBuffer, curr);
		SDL_UnlockMutex(renderer->disposeVertexBuffersLock);
	}
}

static void MODERNGL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buf = (ModernGLBuffer*) buffer;
	FNA3D_Command cmd;

	if (	(	options == FNA3D_SETDATAOPTIONS_NONE ||
			options == FNA3D_SETDATAOPTIONS_DISCARD	) &&
		renderer->threadID != SDL_ThreadID()	)
	{
		cmd.type = FNA3D_COMMAND_SETVERTEXBUFFERDATA;
		cmd.setVertexBufferData.buffer = buffer;
		cmd.setVertexBufferData.offsetInBytes = offsetInBytes;
		cmd.setVertexBufferData.data = data;
		cmd.setVertexBufferData.elementCount = elementCount;
		cmd.setVertexBufferData.elementSizeInBytes = elementSizeInBytes;
		cmd.setVertexBufferData.vertexStride = vertexStride;
		cmd.setVertexBufferData.options = options;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	/* FIXME: Staging buffer for elementSizeInBytes < vertexStride! */

	if (options == FNA3D_SETDATAOPTIONS_NONE)
	{
		/* For static buffers this is the only path,
		 * and it should be "fast" enough over there.
		 * If you are hitting this with a dynamic buffer
		 * you are using dynamic buffers incorrectly.
		 * -flibit
		 */
		renderer->glNamedBufferSubData(
			buf->handle,
			offsetInBytes,
			elementCount * vertexStride,
			data
		);
		return;
	}
	else if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		renderer->glUnmapNamedBuffer(buf->handle);
		renderer->glInvalidateBufferData(buf->handle);
		buf->pin = (uint8_t*) renderer->glMapNamedBufferRange(
			buf->handle,
			0,
			buf->size,
			buf->flags
		);
	}

	SDL_memcpy(
		buf->pin + offsetInBytes,
		data,
		elementCount * vertexStride
	);
}

static void MODERNGL_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buf = (ModernGLBuffer*) buffer;
	uint8_t *dataBytes, *cpy, *src, *dst;
	uint8_t useStagingBuffer;
	int32_t i;
	FNA3D_Command cmd;

	if (	buf->pin == NULL &&
		renderer->threadID != SDL_ThreadID()	)
	{
		cmd.type = FNA3D_COMMAND_GETVERTEXBUFFERDATA;
		cmd.getVertexBufferData.buffer = buffer;
		cmd.getVertexBufferData.offsetInBytes = offsetInBytes;
		cmd.getVertexBufferData.data = data;
		cmd.getVertexBufferData.elementCount = elementCount;
		cmd.getVertexBufferData.elementSizeInBytes = elementSizeInBytes;
		cmd.getVertexBufferData.vertexStride = vertexStride;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	dataBytes = (uint8_t*) data;
	useStagingBuffer = elementSizeInBytes < vertexStride;
	if (useStagingBuffer)
	{
		cpy = (uint8_t*) SDL_malloc(elementCount * vertexStride);
	}
	else
	{
		cpy = dataBytes;
	}

	if (buf->pin != NULL)
	{
		/* Buffers can't get written to by anyone other than the
		 * application, so we can just memcpy here... right?
		 */
		SDL_memcpy(
			cpy,
			buf->pin + offsetInBytes,
			elementCount * elementSizeInBytes
		);
	}
	else
	{
		renderer->glGetNamedBufferSubData(
			buf->handle,
			(GLintptr) offsetInBytes,
			(GLsizeiptr) (elementCount * vertexStride),
			cpy
		);
	}

	if (useStagingBuffer)
	{
		src = cpy;
		dst = dataBytes;
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

static FNA3D_Buffer* MODERNGL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_GENINDEXBUFFER;
		cmd.genIndexBuffer.dynamic = dynamic;
		cmd.genIndexBuffer.usage = usage;
		cmd.genIndexBuffer.indexCount = indexCount;
		cmd.genIndexBuffer.indexElementSize = indexElementSize;
		ForceToMainThread(renderer, &cmd);
		return cmd.genIndexBuffer.retval;
	}

	result = (ModernGLBuffer*) SDL_malloc(sizeof(ModernGLBuffer));
	renderer->glCreateBuffers(1, &result->handle);
	result->size = indexCount * IndexSize(indexElementSize);
	result->flags = (
		GL_MAP_PERSISTENT_BIT |
		GL_MAP_COHERENT_BIT |
		GL_MAP_WRITE_BIT
	);
	if (usage == FNA3D_BUFFERUSAGE_NONE)
	{
		result->flags |= GL_MAP_READ_BIT;
	}

	if (dynamic)
	{
		renderer->glNamedBufferStorage(
			result->handle,
			result->size,
			NULL,
			result->flags | GL_DYNAMIC_STORAGE_BIT
		);

		result->pin = (uint8_t*) renderer->glMapNamedBufferRange(
			result->handle,
			0,
			result->size,
			result->flags
		);
	}
	else
	{
		renderer->glNamedBufferData(
			result->handle,
			result->size,
			NULL,
			GL_STATIC_DRAW
		);
	}

	return (FNA3D_Buffer*) result;
}

static void MODERNGL_INTERNAL_DestroyIndexBuffer(
	ModernGLRenderer *renderer,
	ModernGLBuffer *buffer
) {
	if (buffer->handle == renderer->currentIndexBuffer)
	{
		renderer->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		renderer->currentIndexBuffer = 0;
	}
	renderer->glDeleteBuffers(1, &buffer->handle);
	SDL_free(buffer);
}

static void MODERNGL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *glBuffer = (ModernGLBuffer*) buffer;
	ModernGLBuffer *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyIndexBuffer(renderer, glBuffer);
	}
	else
	{
		SDL_LockMutex(renderer->disposeIndexBuffersLock);
		LinkedList_Add(renderer->disposeIndexBuffers, glBuffer, curr);
		SDL_UnlockMutex(renderer->disposeIndexBuffersLock);
	}
}

static void MODERNGL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buf = (ModernGLBuffer*) buffer;
	FNA3D_Command cmd;

	if (	(	options == FNA3D_SETDATAOPTIONS_NONE ||
			options == FNA3D_SETDATAOPTIONS_DISCARD	) &&
		renderer->threadID != SDL_ThreadID()	)
	{
		cmd.type = FNA3D_COMMAND_SETINDEXBUFFERDATA;
		cmd.setIndexBufferData.buffer = buffer;
		cmd.setIndexBufferData.offsetInBytes = offsetInBytes;
		cmd.setIndexBufferData.data = data;
		cmd.setIndexBufferData.dataLength = dataLength;
		cmd.setIndexBufferData.options = options;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	if (options == FNA3D_SETDATAOPTIONS_NONE)
	{
		/* For static buffers this is the only path,
		 * and it should be "fast" enough over there.
		 * If you are hitting this with a dynamic buffer
		 * you are using dynamic buffers incorrectly.
		 * -flibit
		 */
		renderer->glNamedBufferSubData(
			buf->handle,
			offsetInBytes,
			dataLength,
			data
		);
		return;
	}
	else if (options == FNA3D_SETDATAOPTIONS_DISCARD)
	{
		renderer->glUnmapNamedBuffer(buf->handle);
		renderer->glInvalidateBufferData(buf->handle);
		buf->pin = (uint8_t*) renderer->glMapNamedBufferRange(
			buf->handle,
			0,
			buf->size,
			buf->flags
		);
	}

	SDL_memcpy(
		buf->pin + offsetInBytes,
		data,
		dataLength
	);
}

static void MODERNGL_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLBuffer *buf = (ModernGLBuffer*) buffer;
	FNA3D_Command cmd;

	if (	buf->pin == NULL &&
		renderer->threadID != SDL_ThreadID()	)
	{
		cmd.type = FNA3D_COMMAND_GETINDEXBUFFERDATA;
		cmd.getIndexBufferData.buffer = buffer;
		cmd.getIndexBufferData.offsetInBytes = offsetInBytes;
		cmd.getIndexBufferData.data = data;
		cmd.getIndexBufferData.dataLength = dataLength;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	if (buf->pin != NULL)
	{
		/* Buffers can't get written to by anyone other than the
		 * application, so we can just memcpy here... right?
		 */
		SDL_memcpy(data, buf->pin + offsetInBytes, dataLength);
	}
	else
	{
		renderer->glGetNamedBufferSubData(
			buf->handle,
			(GLintptr) offsetInBytes,
			(GLsizeiptr) dataLength,
			data
		);
	}
}

/* Effects */

static void MODERNGL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	MOJOSHADER_glEffect *glEffect;
	ModernGLEffect *result;
	int32_t i;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CREATEEFFECT;
		cmd.createEffect.effectCode = effectCode;
		cmd.createEffect.effectCodeLength = effectCodeLength;
		cmd.createEffect.effect = effect;
		cmd.createEffect.effectData = effectData;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	*effectData = MOJOSHADER_parseEffect(
		renderer->shaderProfile,
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

	for (i = 0; i < (*effectData)->error_count; i += 1)
	{
		FNA3D_LogError(
			"MOJOSHADER_parseEffect Error: %s",
			(*effectData)->errors[i].error
		);
	}

	glEffect = MOJOSHADER_glCompileEffect(*effectData);
	if (glEffect == NULL)
	{
		FNA3D_LogError(
			"%s", MOJOSHADER_glGetError()
		);
	}

	result = (ModernGLEffect*) SDL_malloc(sizeof(ModernGLEffect));
	result->effect = *effectData;
	result->glEffect = glEffect;
	result->next = NULL;
	*effect = (FNA3D_Effect*) result;
}

static void MODERNGL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLEffect *glCloneSource = (ModernGLEffect*) cloneSource;
	MOJOSHADER_glEffect *glEffect;
	ModernGLEffect *result;
	FNA3D_Command cmd;

	if (renderer->threadID != SDL_ThreadID())
	{
		cmd.type = FNA3D_COMMAND_CLONEEFFECT;
		cmd.cloneEffect.cloneSource = cloneSource;
		cmd.cloneEffect.effect = effect;
		cmd.cloneEffect.effectData = effectData;
		ForceToMainThread(renderer, &cmd);
		return;
	}

	*effectData = MOJOSHADER_cloneEffect(glCloneSource->effect);
	glEffect = MOJOSHADER_glCompileEffect(*effectData);
	if (glEffect == NULL)
	{
		FNA3D_LogError(
			"%s", MOJOSHADER_glGetError()
		);
	}

	result = (ModernGLEffect*) SDL_malloc(sizeof(ModernGLEffect));
	result->effect = *effectData;
	result->glEffect = glEffect;
	result->next = NULL;
	*effect = (FNA3D_Effect*) result;
}

static void MODERNGL_INTERNAL_DestroyEffect(
	ModernGLRenderer *renderer,
	ModernGLEffect *effect
) {
	MOJOSHADER_glEffect *glEffect = effect->glEffect;
	if (glEffect == renderer->currentEffect)
	{
		MOJOSHADER_glEffectEndPass(renderer->currentEffect);
		MOJOSHADER_glEffectEnd(renderer->currentEffect);
		renderer->currentEffect = NULL;
		renderer->currentTechnique = NULL;
		renderer->currentPass = 0;
	}
	MOJOSHADER_glDeleteEffect(glEffect);
	MOJOSHADER_freeEffect(effect->effect);
	SDL_free(effect);
}

static void MODERNGL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLEffect *fnaEffect = (ModernGLEffect*) effect;
	ModernGLEffect *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyEffect(renderer, fnaEffect);
	}
	else
	{
		SDL_LockMutex(renderer->disposeEffectsLock);
		LinkedList_Add(renderer->disposeEffects, fnaEffect, curr);
		SDL_UnlockMutex(renderer->disposeEffectsLock);
	}
}

static void MODERNGL_SetEffectTechnique(
	FNA3D_Renderer *renderer,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	/* FIXME: Why doesn't this function do anything? */
	ModernGLEffect *fnaEffect = (ModernGLEffect*) effect;
	MOJOSHADER_effectSetTechnique(fnaEffect->effect, technique);
}

static void MODERNGL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLEffect *fnaEffect = (ModernGLEffect*) effect;
	MOJOSHADER_glEffect *glEffectData = fnaEffect->glEffect;
	const MOJOSHADER_effectTechnique *technique = fnaEffect->effect->current_technique;
	uint32_t whatever;

	renderer->effectApplied = 1;
	if (glEffectData == renderer->currentEffect)
	{
		if (	technique == renderer->currentTechnique &&
			pass == renderer->currentPass		)
		{
			MOJOSHADER_glEffectCommitChanges(
				renderer->currentEffect
			);
			return;
		}
		MOJOSHADER_glEffectEndPass(renderer->currentEffect);
		MOJOSHADER_glEffectBeginPass(renderer->currentEffect, pass);
		renderer->currentTechnique = technique;
		renderer->currentPass = pass;
		return;
	}
	else if (renderer->currentEffect != NULL)
	{
		MOJOSHADER_glEffectEndPass(renderer->currentEffect);
		MOJOSHADER_glEffectEnd(renderer->currentEffect);
	}
	MOJOSHADER_glEffectBegin(
		glEffectData,
		&whatever,
		0,
		stateChanges
	);
	MOJOSHADER_glEffectBeginPass(glEffectData, pass);
	renderer->currentEffect = glEffectData;
	renderer->currentTechnique = technique;
	renderer->currentPass = pass;
}

static void MODERNGL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	MOJOSHADER_glEffect *glEffectData = ((ModernGLEffect*) effect)->glEffect;
	uint32_t whatever;

	MOJOSHADER_glEffectBegin(
		glEffectData,
		&whatever,
		1,
		stateChanges
	);
	MOJOSHADER_glEffectBeginPass(glEffectData, 0);
	renderer->effectApplied = 1;
}

static void MODERNGL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	MOJOSHADER_glEffect *glEffectData = ((ModernGLEffect*) effect)->glEffect;

	MOJOSHADER_glEffectEndPass(glEffectData);
	MOJOSHADER_glEffectEnd(glEffectData);
	renderer->effectApplied = 1;
}

/* Queries */

static FNA3D_Query* MODERNGL_CreateQuery(FNA3D_Renderer *driverData)
{
	ModernGLQuery *result;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	result = (ModernGLQuery*) SDL_malloc(sizeof(ModernGLQuery));
	renderer->glGenQueries(1, &result->handle);
	result->next = NULL;

	return (FNA3D_Query*) result;
}

static void MODERNGL_INTERNAL_DestroyQuery(
	ModernGLRenderer *renderer,
	ModernGLQuery *query
) {
	renderer->glDeleteQueries(
		1,
		&query->handle
	);
	SDL_free(query);
}

static void MODERNGL_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLQuery *glQuery = (ModernGLQuery*) query;
	ModernGLQuery *curr;

	if (renderer->threadID == SDL_ThreadID())
	{
		MODERNGL_INTERNAL_DestroyQuery(renderer, glQuery);
	}
	else
	{
		SDL_LockMutex(renderer->disposeQueriesLock);
		LinkedList_Add(renderer->disposeQueries, glQuery, curr);
		SDL_UnlockMutex(renderer->disposeQueriesLock);
	}
}

static void MODERNGL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLQuery *glQuery = (ModernGLQuery*) query;

	renderer->glBeginQuery(
		GL_SAMPLES_PASSED,
		glQuery->handle
	);
}

static void MODERNGL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;

	/* May need to check active queries...? */
	renderer->glEndQuery(
		GL_SAMPLES_PASSED
	);
}

static uint8_t MODERNGL_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLuint result;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLQuery *glQuery = (ModernGLQuery*) query;

	renderer->glGetQueryObjectuiv(
		glQuery->handle,
		GL_QUERY_RESULT_AVAILABLE,
		&result
	);
	return result != 0;
}

static int32_t MODERNGL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLuint result;
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	ModernGLQuery *glQuery = (ModernGLQuery*) query;

	renderer->glGetQueryObjectuiv(
		glQuery->handle,
		GL_QUERY_RESULT,
		&result
	);
	return (int32_t) result;
}

/* Feature Queries */

static uint8_t MODERNGL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->supports_dxt1;
}

static uint8_t MODERNGL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->supports_s3tc;
}

static uint8_t MODERNGL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 1;
}

static uint8_t MODERNGL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 1;
}

static int32_t MODERNGL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->numTextureSlots;
}

static int32_t MODERNGL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	return renderer->maxMultiSampleCount;
}

/* Debugging */

static void MODERNGL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	ModernGLRenderer *renderer = (ModernGLRenderer*) driverData;
	if (renderer->supports_GREMEDY_string_marker)
	{
		renderer->glStringMarkerGREMEDY(SDL_strlen(text), text);
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
	if (type == GL_DEBUG_TYPE_ERROR)
	{
		FNA3D_LogError(
			"%s\n\tSource: %s\n\tType: %s\n\tSeverity: %s",
			message,
			debugSourceStr[source - GL_DEBUG_SOURCE_API],
			debugTypeStr[type - GL_DEBUG_TYPE_ERROR],
			debugSeverityStr[severity - GL_DEBUG_SEVERITY_HIGH]
		);
	}
	else
	{
		FNA3D_LogWarn(
			"%s\n\tSource: %s\n\tType: %s\n\tSeverity: %s",
			message,
			debugSourceStr[source - GL_DEBUG_SOURCE_API],
			debugTypeStr[type - GL_DEBUG_TYPE_ERROR],
			debugSeverityStr[severity - GL_DEBUG_SEVERITY_HIGH]
		);
	}
}

/* Driver */

static void LoadEntryPoints(
	ModernGLRenderer *renderer,
	const char *driverInfo,
	uint8_t debugMode
) {
	renderer->supports_KHR_debug = 1;
	renderer->supports_GREMEDY_string_marker = 1;

	#define GL_PROC(ret, func, parms) \
		renderer->func = (glfntype_##func) SDL_GL_GetProcAddress(#func); \
		if (renderer->func == NULL) \
		{ \
			FNA3D_LogError("OpenGL 4.6 support is required!"); \
			return; \
		}
	#define GL_PROC_EXT(ext, fallback, ret, func, parms) \
		renderer->func = (glfntype_##func) SDL_GL_GetProcAddress(#func); \
		if (renderer->func == NULL) \
		{ \
			renderer->func = (glfntype_##func) SDL_GL_GetProcAddress(#func #fallback); \
			if (renderer->func == NULL) \
			{ \
				renderer->supports_##ext = 0; \
			} \
		}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	#include "FNA3D_Driver_ModernGL_glfuncs.h"
#pragma GCC diagnostic pop
	#undef GL_PROC
	#undef GL_PROC_EXT

	/* Everything below this check is for debug contexts */
	if (!debugMode)
	{
		return;
	}

	if (renderer->supports_KHR_debug)
	{
		renderer->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DONT_CARE,
			GL_DONT_CARE,
			0,
			NULL,
			GL_TRUE
		);
		renderer->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DEBUG_TYPE_OTHER,
			GL_DEBUG_SEVERITY_LOW,
			0,
			NULL,
			GL_FALSE
		);
		renderer->glDebugMessageControl(
			GL_DONT_CARE,
			GL_DEBUG_TYPE_OTHER,
			GL_DEBUG_SEVERITY_NOTIFICATION,
			0,
			NULL,
			GL_FALSE
		);
		renderer->glDebugMessageCallback(DebugCall, NULL);
	}
	else
	{
		FNA3D_LogWarn(
			"ARB_debug_output/KHR_debug not supported!"
		);
	}

	if (!renderer->supports_GREMEDY_string_marker)
	{
		FNA3D_LogWarn(
			"GREMEDY_string_marker not supported!"
		);
	}
}

static void* MOJOSHADERCALL GLGetProcAddress(const char *ep, void* d)
{
	return SDL_GL_GetProcAddress(ep);
}

static inline void CheckExtensions(
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

static uint8_t MODERNGL_PrepareWindowAttributes(uint32_t *flags)
{
	uint8_t forceCore, forceCompat;
	int32_t depthSize, stencilSize;
	const char *depthFormatHint;

	/* GLContext environment variables */
	forceCore = SDL_GetHintBoolean("FNA_OPENGL_FORCE_CORE_PROFILE", 0);
	forceCompat = SDL_GetHintBoolean("FNA_OPENGL_FORCE_COMPATIBILITY_PROFILE", 0);

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
	if (forceCore)
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
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_PROFILE_MASK,
			SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
		);
	}

	*flags = SDL_WINDOW_OPENGL;
	return 1;
}

static void MODERNGL_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	SDL_GL_GetDrawableSize((SDL_Window*) window, x, y);
}

static FNA3D_Device* MODERNGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	int32_t flags;
	int32_t depthSize, stencilSize;
	const char *rendererStr, *versionStr, *vendorStr;
	char driverInfo[256];
	int32_t i;
	int32_t numExtensions, numSamplers, numAttributes, numAttachments;
	ModernGLRenderer *renderer;
	FNA3D_Device *result;

	/* Create the FNA3D_Device */
	result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(MODERNGL)

	/* Init the ModernGLRenderer */
	renderer = (ModernGLRenderer*) SDL_malloc(sizeof(ModernGLRenderer));
	SDL_memset(renderer, '\0', sizeof(ModernGLRenderer));

	/* The FNA3D_Device and ModernGLRenderer need to reference each other */
	renderer->parentDevice = result;
	result->driverData = (FNA3D_Renderer*) renderer;

	/* Debug context support */
	if (debugMode)
	{
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_FLAGS,
			SDL_GL_CONTEXT_DEBUG_FLAG
		);
	}

	/* Create OpenGL context */
	renderer->context = SDL_GL_CreateContext(
		(SDL_Window*) presentationParameters->deviceWindowHandle
	);

	/* Check for a possible ES/Core context */
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &flags);
	renderer->useCoreProfile = (flags & SDL_GL_CONTEXT_PROFILE_CORE) != 0;

	/* Check for a possible debug context */
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags);
	debugMode = (flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0;

	/* Check the window's depth/stencil format */
	SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthSize);
	SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencilSize);
	if (depthSize == 0 && stencilSize == 0)
	{
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_NONE;
	}
	else if (depthSize == 16 && stencilSize == 0)
	{
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_D16;
	}
	else if (depthSize == 24 && stencilSize == 0)
	{
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_D24;
	}
	else if (depthSize == 24 && stencilSize == 8)
	{
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_D24S8;
	}
	else if (depthSize == 32 && stencilSize == 8)
	{
		/* There's like a 99% chance this is GDI, expect a
		 * NoSuitableGraphicsDevice soon after this line...
		 */
		FNA3D_LogWarn("Non-standard D32S8 window depth format!");
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_D24S8;
	}
	else
	{
		FNA3D_LogError(
			"Unrecognized window depth/stencil format: %d %d",
			depthSize,
			stencilSize
		);
		renderer->windowDepthFormat = FNA3D_DEPTHFORMAT_D24S8;
	}

	/* Initialize entry points */
	LoadEntryPoints(renderer, driverInfo, debugMode);

	/* Print GL information */
	rendererStr =	(const char*) renderer->glGetString(GL_RENDERER);
	versionStr =	(const char*) renderer->glGetString(GL_VERSION);
	vendorStr =	(const char*) renderer->glGetString(GL_VENDOR);
	SDL_snprintf(
		driverInfo, sizeof(driverInfo),
		"OpenGL renderer: %s\nOpenGL Driver: %s\nOpenGL Vendor: %s",
		rendererStr, versionStr, vendorStr
	);
	FNA3D_LogInfo(
		"FNA3D Driver: ModernGL\n%s",
		driverInfo
	);

	/* Initialize shader context */
	renderer->shaderProfile = SDL_GetHint("FNA3D_MOJOSHADER_PROFILE");
	if (renderer->shaderProfile == NULL || renderer->shaderProfile[0] == '\0')
	{
		renderer->shaderProfile = MOJOSHADER_glBestProfile(
			GLGetProcAddress,
			NULL,
			NULL,
			NULL,
			NULL
		);

		/* SPIR-V is very new and not really necessary. */
		if (	(SDL_strcmp(renderer->shaderProfile, "glspirv") == 0) &&
			!renderer->useCoreProfile	)
		{
			renderer->shaderProfile = "glsl120";
		}
	}
	renderer->shaderContext = MOJOSHADER_glCreateContext(
		renderer->shaderProfile,
		GLGetProcAddress,
		NULL,
		NULL,
		NULL,
		NULL
	);
	MOJOSHADER_glMakeContextCurrent(renderer->shaderContext);

	/* Some users might want pixely upscaling... */
	renderer->backbufferScaleMode = SDL_GetHintBoolean(
		"FNA3D_BACKBUFFER_SCALE_NEAREST", 0
	) ? GL_NEAREST : GL_LINEAR;

	/* Load the extension list, initialize extension-dependent components */
	renderer->supports_s3tc = 0;
	renderer->supports_dxt1 = 0;
	if (renderer->useCoreProfile)
	{
		renderer->glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
		for (i = 0; i < numExtensions; i += 1)
		{
			CheckExtensions(
				(const char*) renderer->glGetStringi(GL_EXTENSIONS, i),
				&renderer->supports_s3tc,
				&renderer->supports_dxt1
			);

			if (renderer->supports_s3tc && renderer->supports_dxt1)
			{
				/* No need to look further. */
				break;
			}
		}
	}
	else
	{
		CheckExtensions(
			(const char*) renderer->glGetString(GL_EXTENSIONS),
			&renderer->supports_s3tc,
			&renderer->supports_dxt1
		);
	}

	/* Check the max multisample count, override parameters if necessary */
	renderer->glGetIntegerv(
		GL_MAX_SAMPLES,
		&renderer->maxMultiSampleCount
	);
	presentationParameters->multiSampleCount = SDL_min(
		presentationParameters->multiSampleCount,
		renderer->maxMultiSampleCount
	);

	/* Initialize the faux backbuffer */
	MODERNGL_INTERNAL_CreateBackbuffer(renderer, presentationParameters);

	/* Initialize texture collection array */
	renderer->glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &numSamplers);
	numSamplers = SDL_min(numSamplers, MAX_TEXTURE_SAMPLERS);
	renderer->glCreateSamplers(numSamplers, renderer->samplers);
	for (i = 0; i < numSamplers; i += 1)
	{
		renderer->textures[i] = &NullTexture;
		renderer->samplersU[i] = FNA3D_TEXTUREADDRESSMODE_WRAP;
		renderer->samplersV[i] = FNA3D_TEXTUREADDRESSMODE_WRAP;
		renderer->samplersW[i] = FNA3D_TEXTUREADDRESSMODE_WRAP;
		renderer->samplersFilter[i] = FNA3D_TEXTUREFILTER_LINEAR;
		renderer->samplersAnisotropy[i] = 4.0f;
		renderer->samplersMaxLevel[i] = 0;
		renderer->samplersLODBias[i] = 0.0f;
		renderer->samplersMipped[i] = 0;
		renderer->glBindSampler(i, renderer->samplers[i]);
	}
	renderer->numTextureSlots = numSamplers;

	/* Initialize vertex attribute state arrays */
	renderer->ldBaseVertex = -1;
	renderer->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &numAttributes);
	numAttributes = SDL_min(numAttributes, MAX_VERTEX_ATTRIBUTES);
	for (i = 0; i < numAttributes; i += 1)
	{
		renderer->attributes[i].currentBuffer = 0;
		renderer->attributes[i].currentPointer = NULL;
		renderer->attributes[i].currentFormat = FNA3D_VERTEXELEMENTFORMAT_SINGLE;
		renderer->attributes[i].currentNormalized = 0;
		renderer->attributes[i].currentStride = 0;

		renderer->attributeEnabled[i] = 0;
		renderer->previousAttributeEnabled[i] = 0;
		renderer->attributeDivisor[i] = 0;
		renderer->previousAttributeDivisor[i] = 0;
	}
	renderer->numVertexAttributes = numAttributes;

	/* Initialize render target FBO and state arrays */
	renderer->glGetIntegerv(GL_MAX_DRAW_BUFFERS, &numAttachments);
	numAttachments = SDL_min(numAttachments, MAX_RENDERTARGET_BINDINGS);
	for (i = 0; i < numAttachments; i += 1)
	{
		renderer->attachments[i] = 0;
		renderer->attachmentTypes[i] = 0;
		renderer->currentAttachments[i] = 0;
		renderer->currentAttachmentTypes[i] = GL_TEXTURE_2D;
		renderer->drawBuffersArray[i] = GL_COLOR_ATTACHMENT0 + i;
	}
	renderer->numAttachments = numAttachments;

	renderer->drawBuffersArray[numAttachments] = GL_DEPTH_ATTACHMENT;
	renderer->drawBuffersArray[numAttachments + 1] = GL_STENCIL_ATTACHMENT;
	renderer->glCreateFramebuffers(1, &renderer->targetFramebuffer);
	renderer->glCreateFramebuffers(1, &renderer->resolveFramebufferRead);
	renderer->glCreateFramebuffers(1, &renderer->resolveFramebufferDraw);

	if (renderer->useCoreProfile)
	{
		/* Generate and bind a VAO, to shut Core up */
		renderer->glGenVertexArrays(1, &renderer->vao);
		renderer->glBindVertexArray(renderer->vao);
	}
	else
	{
		/* Compat-only, but needed for PSIZE0 accuracy */
		renderer->glEnable(GL_POINT_SPRITE);
		renderer->glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, 1);
	}

	/* Initialize renderer members not covered by SDL_memset('\0') */
	renderer->dstBlend = FNA3D_BLEND_ZERO; /* ZERO is really 1. -caleb */
	renderer->dstBlendAlpha = FNA3D_BLEND_ZERO; /* ZERO is really 1. -caleb */
	renderer->colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	renderer->colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	renderer->colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	renderer->colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	renderer->multiSampleMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	renderer->stencilWriteMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	renderer->stencilMask = -1; /* AKA 0xFFFFFFFF, ugh -flibit */
	renderer->multiSampleEnable = 1;
	renderer->depthRangeMax = 1.0f;
	renderer->currentClearDepth = 1.0f;

	/* The creation thread will be the "main" thread */
	renderer->threadID = SDL_ThreadID();
	renderer->commandsLock = SDL_CreateMutex();
	renderer->disposeTexturesLock = SDL_CreateMutex();
	renderer->disposeRenderbuffersLock = SDL_CreateMutex();
	renderer->disposeVertexBuffersLock = SDL_CreateMutex();
	renderer->disposeIndexBuffersLock = SDL_CreateMutex();
	renderer->disposeEffectsLock = SDL_CreateMutex();
	renderer->disposeQueriesLock = SDL_CreateMutex();

	/* Return the FNA3D_Device */
	return result;
}

FNA3D_Driver ModernGLDriver = {
	"ModernGL",
	MODERNGL_PrepareWindowAttributes,
	MODERNGL_GetDrawableSize,
	MODERNGL_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_MODERNGL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
