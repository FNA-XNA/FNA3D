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

typedef struct OpenGLDevice /* Cast from driverData */
{
	/* Context */
	SDL_GLContext context;
	uint8_t useES3;
	uint8_t useCoreProfile;
	FNA3D_DepthFormat windowDepthFormat;
	GLuint realBackbufferFBO;
	GLuint realBackbufferRBO;

	/* Capabilities */
	uint8_t supportsBaseVertex;
	uint8_t supportsFauxBackbuffer;
	uint8_t supportsHardwareInstancing;
	uint8_t supportsMultisampling;
	uint8_t supportsFBOInvalidation;

	/* GL entry points */
	glfntype_glGetString glGetString; /* Loaded early! */
	#define GL_PROC(ext, ret, func, parms) \
	glfntype_##func func;
	#include "glfuncs.h"
	#undef GL_PROC
} OpenGLDevice;

typedef struct OpenGLTexture /* Cast from FNA3D_Texture* */
{
} OpenGLTexture;

typedef struct OpenGLBuffer /* Cast from FNA3D_Buffer* */
{
} OpenGLBuffer;

typedef struct OpenGLRenderbuffer /* Cast from FNA3D_Renderbuffer* */
{
} OpenGLRenderbuffer;

typedef struct OpenGLEffect /* Cast from FNA3D_Effect* */
{
} OpenGLEffect;

typedef struct OpenGLQuery /* Cast from FNA3D_Query* */
{
} OpenGLQuery;

#define BACKBUFFER_TYPE_OPENGL 1
#define BACKBUFFER_TYPE_NULL 0

typedef struct OpenGLBackbuffer /* Cast from FNA3D_Backbuffer */
{
	uint8_t type;
} OpenGLBackbuffer;

typedef struct NullBackbuffer /* Cast from FNA3D_Backbuffer */
{
	uint8_t type;
} NullBackbuffer;

/* Device Implementation */

/* Quit */

void OPENGL_DestroyDevice(FNA3D_Device *device)
{
	/* TODO */
	SDL_free(device);
}

/* Begin/End Frame */

void OPENGL_BeginFrame(void* driverData)
{
	/* TODO */
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
	/* TODO */
}

/* Drawing */

void OPENGL_Clear(
	void* driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	/* TODO */
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
	/* TODO */
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
	/* TODO */
}

void OPENGL_DrawPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	/* TODO */
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
	/* TODO */
}

void OPENGL_DrawUserPrimitives(
	void* driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	/* TODO */
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
	return 0;
}

void OPENGL_SetMultiSampleMask(void* driverData, int32_t mask)
{
	/* TODO */
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
}

void OPENGL_ApplyVertexDeclaration(
	void* driverData,
	/* FIXME: Oh shit VertexDeclaration vertexDeclaration, */
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
	FNA3D_Texture *textures,
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
	void* technique, /* FIXME: Should be MojoShader */
	uint32_t pass,
	void* stateChanges /* FIXME: Should be MojoShader */
) {
	/* TODO */
}

void OPENGL_BeginPassRestore(
	void* driverData,
	FNA3D_Effect *effect,
	void* stateChanges /* FIXME: Should be MojoShader */
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
	return NULL;
}

void OPENGL_AddDisposeQuery(void* driverData, FNA3D_Query *query)
{
	/* TODO */
}

void OPENGL_QueryBegin(void* driverData, FNA3D_Query *query)
{
	/* TODO */
}

void OPENGL_QueryEnd(void* driverData, FNA3D_Query *query)
{
	/* TODO */
}

uint8_t OPENGL_QueryComplete(void* driverData, FNA3D_Query *query)
{
	/* TODO */
	return 1;
}

int32_t OPENGL_QueryPixelCount(
	void* driverData,
	FNA3D_Query *query
) {
	/* TODO */
	return 0;
}

/* Feature Queries */

uint8_t OPENGL_SupportsDXT1(void* driverData)
{
	/* TODO */
	return 0;
}
uint8_t OPENGL_SupportsS3TC(void* driverData)
{
	/* TODO */
	return 0;
}
uint8_t OPENGL_SupportsHardwareInstancing(void* driverData)
{
	/* TODO */
	return 0;
}
uint8_t OPENGL_SupportsNoOverwrite(void* driverData)
{
	/* TODO */
	return 0;
}

int32_t OPENGL_GetMaxTextureSlots(void* driverData)
{
	/* TODO */
	return 0;
}
int32_t OPENGL_GetMaxMultiSampleCount(void* driverData)
{
	/* TODO */
	return 0;
}

/* Debugging */

void OPENGL_SetStringMarker(void* driverData, const char *text)
{
	/* TODO */
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

intptr_t OPENGL_GetBufferSize(
	void* driverData,
	FNA3D_Buffer *buffer
) {
	/* TODO */
	return 0;
}

/* Effect Objects */

void* OPENGL_GetEffectData(
	void* driverData,
	FNA3D_Effect *effect
) {
	/* TODO */
	return NULL;
}

/* Backbuffer Objects */

int32_t OPENGL_GetBackbufferWidth(
	void* driverData,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

int32_t OPENGL_GetBackbufferHeight(
	void* driverData,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

FNA3D_DepthFormat OPENGL_GetBackbufferDepthFormat(
	void* driverData,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return FNA3D_DEPTHFORMAT_NONE;
}

int32_t OPENGL_GetBackbufferMultiSampleCount(
	void* driverData,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

void OPENGL_ResetFramebuffer(
	void* driverData,
	FNA3D_Backbuffer *backbuffer,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
}

/* Fallback GL Functions */

/* Use this instead of device->glDrawRangeElementsBaseVertex */
static void DrawRangeElementsBaseVertex(
	OpenGLDevice *device,
	GLenum mode,
	GLuint start,
	GLuint end,
	GLsizei count,
	GLenum type,
	const GLvoid *indices,
	GLint basevertex
) {
	if (device->glDrawRangeElementsBaseVertex)
	{
		device->glDrawRangeElementsBaseVertex(
			mode,
			start,
			end,
			count,
			type,
			indices,
			basevertex
		);
	}
	else if (device->glDrawRangeElements)
	{
		device->glDrawRangeElements(
			mode,
			start,
			end,
			count,
			type,
			indices
		);
	}
	else
	{
		device->glDrawElements(
			mode,
			count,
			type,
			indices
		);
	}
}

/* Use this instead of device->glDrawElementsInstancedBaseVertex */
static void DrawElementsInstancedBaseVertex(
	OpenGLDevice *device,
	GLenum mode,
	GLsizei count,
	GLenum type,
	GLvoid *indices,
	GLint instance,
	GLsizei instancecount,
	GLint baseVertex
) {
	if (device->glDrawElementsInstancedBaseVertex)
	{
		device->glDrawElementsInstancedBaseVertex(
			mode,
			count,
			type,
			indices,
			instancecount,
			baseVertex
		);
	}
	else
	{
		device->glDrawElementsInstanced(
			mode,
			count,
			type,
			indices,
			instancecount
		);
	}
}

static void PolygonModeESError(GLenum a, GLenum b)
{
	SDL_assert(0 && "glPolygonMode is not available in ES!");
}

static void GetTexImageESError(GLenum a, GLint b, GLenum c, GLenum d, GLvoid *e)
{
	SDL_assert(0 && "glGetTexImage is not available in ES!");
}

static void GetBufferSubDataESError(GLenum a, GLintptr b, GLsizeiptr c, GLvoid *d)
{
	SDL_assert(0 && "glGetBufferSubData is not available in ES!");
}

/* Use this instead of device->glClearDepth */
static void ClearDepth(OpenGLDevice *device, GLdouble depth)
{
	if (device->glClearDepth)
	{
		device->glClearDepth(depth);
	}
	else
	{
		device->glClearDepthf((float) depth);
	}
}

/* Use this instead of device->glDepthRange */
static void DepthRange(OpenGLDevice *device, GLdouble nearDepth, GLdouble farDepth)
{
	if (device->glDepthRange)
	{
		device->glDepthRange(nearDepth, farDepth);
	}
	else
	{
		device->glDepthRangef((float) nearDepth, (float) farDepth);
	}
}

/* Load GL Entry Points */

static void LoadGLGetString(OpenGLDevice *device)
{
	device->glGetString = (glfntype_glGetString) SDL_GL_GetProcAddress("glGetString");
	if (!device->glGetString)
	{
		SDL_assert(0 && "GRAPHICS DRIVER IS EXTREMELY BROKEN!");
	}
}

static void LoadEntryPoints(
	OpenGLDevice *device,
	const char *driverInfo,
	uint8_t BUG_HACK_NOTANGLE
) {
	char errorMessage[256];
	const char *baseErrorString = (
		device->useES3 ?
		"OpenGL ES 3.0 support is required!" :
		"OpenGL 2.1 support is required!"
	);

	#define GL_FAIL(func) \
		SDL_snprintf( \
			errorMessage, 256, \
			"%s\nEntry point: %s\n%s", \
			baseErrorString, func, driverInfo \
		); \
		SDL_assert(0 && errorMessage);

	#define INIT_CATEGORY(ext) \
		int supports##ext = 1; \
		char* firstFailed##ext = NULL;
	INIT_CATEGORY(BaseGL)
	INIT_CATEGORY(OptES3)
	INIT_CATEGORY(NonES3)
	INIT_CATEGORY(FBO)
	INIT_CATEGORY(CoreGL)
	INIT_CATEGORY(MiscGL)
	#undef INIT_CATEGORY

	#define GL_PROC(ext, ret, func, parms) \
		device->func = (glfntype_##func) SDL_GL_GetProcAddress(#func); \
		if (!device->func) \
		{ \
			supports##ext = 0; \
			if (firstFailed##ext == NULL) \
			{ \
				firstFailed##ext = #func; \
			} \
		}
	#include "glfuncs.h"
	#undef GL_PROC

	/* Basic entry points. If you don't have these, you're screwed. */
	if (!supportsBaseGL)
	{
		GL_FAIL(firstFailedBaseGL);
	}

	/* ARB_draw_elements_base_vertex is ideal! */
	if (!device->glDrawRangeElementsBaseVertex)
	{
		device->glDrawRangeElementsBaseVertex = (glfntype_glDrawRangeElementsBaseVertex) SDL_GL_GetProcAddress(
			"glDrawRangeElementsBaseVertexOES"
		);
	}
	device->supportsBaseVertex = (
		device->glDrawRangeElementsBaseVertex &&
		BUG_HACK_NOTANGLE
	);
	if (	!device->supportsBaseVertex &&
		!device->glDrawRangeElements &&
		!device->glDrawElements		)
	{
		GL_FAIL("glDrawElements");
	}

	/* These functions are NOT supported in ES.
	 * NVIDIA or desktop ES might, but real scenarios where you need ES
	 * will certainly not have these.
	 * -flibit
	 */
	if (device->useES3)
	{
		if (!device->glPolygonMode)
		{
			device->glPolygonMode = (glfntype_glPolygonMode) PolygonModeESError;
		}
		if (!device->glGetTexImage)
		{
			device->glGetTexImage = (glfntype_glGetTexImage) GetTexImageESError;
		}
		if (!device->glGetBufferSubData)
		{
			device->glGetBufferSubData = (glfntype_glGetBufferSubData) GetBufferSubDataESError;
		}
	}
	else if (!supportsOptES3)
	{
		GL_FAIL(firstFailedOptES3);
	}

	/* We need _some_ form of depth range, ES... */
	if (!device->glDepthRange && !device->glDepthRangef)
	{
		GL_FAIL("glDepthRangef");
	}
	if (!device->glClearDepth && !device->glClearDepthf)
	{
		GL_FAIL("glClearDepthf");
	}

	/* Silently fail if using GLES. You didn't need these, right...? >_> */
	if (!supportsNonES3)
	{
		if (device->useES3)
		{
			SDL_LogWarn(
				SDL_LOG_CATEGORY_APPLICATION,
				"Some non-ES functions failed to load. Beware..."
			);
		}
		else
		{
			GL_FAIL(firstFailedNonES3);
		}
	}

	/* ARB_framebuffer_object. We're flexible, but not _that_ flexible. */
	if (!supportsFBO)
	{
		SDL_assert(0 && "OpenGL framebuffer support is required!");
	}

	/* EXT_framebuffer_blit (or ARB_framebuffer_object) is needed by the faux-backbuffer. */
	device->supportsFauxBackbuffer = (device->glBlitFramebuffer != NULL);

	/* EXT_framebuffer_multisample (or ARB_framebuffer_object) is glitter */
	device->supportsMultisampling = (device->glRenderbufferStorageMultisample != NULL);

	/* ARB_instanced_arrays/ARB_draw_instanced are almost optional. */
	device->supportsHardwareInstancing = device->glVertexAttribDivisor && (
		(device->supportsBaseVertex && device->glDrawElementsInstancedBaseVertex) ||
		(!device->supportsBaseVertex && device->glDrawElementsInstanced)
	);

	/* ARB_invalidate_subdata makes target swaps faster on mobile targets */
	device->supportsFBOInvalidation = device->useES3; // FIXME: Does desktop benefit from this?
	if (!device->glInvalidateFramebuffer && device->useES3)
	{
		/* ES2 has EXT_discard_framebuffer as a fallback */
		device->glInvalidateFramebuffer = (glfntype_glInvalidateFramebuffer) SDL_GL_GetProcAddress(
			"glDiscardFramebufferEXT"
		);
		if (!device->glInvalidateFramebuffer)
		{
			device->supportsFBOInvalidation = 0;
		}
	}

	/* Indexed color mask is a weird thing.
	 * IndexedEXT was introduced in EXT_draw_buffers2, then
	 * it was introduced in GL 3.0 as "ColorMaski" with no
	 * extension at all, and OpenGL ES introduced it as
	 * ColorMaskiEXT via EXT_draw_buffers_indexed and AGAIN
	 * as ColorMaskiOES via OES_draw_buffers_indexed at the
	 * exact same time. WTF.
	 * -flibit
	 */
	if (!device->glColorMaski)
	{
		device->glColorMaski = (glfntype_glColorMaski) SDL_GL_GetProcAddress("glColorMaskIndexedEXT");
	}
	if (!device->glColorMaski)
	{
		device->glColorMaski = (glfntype_glColorMaski) SDL_GL_GetProcAddress("glColorMaskIndexediOES");
	}
	if (!device->glColorMaski)
	{
		device->glColorMaski = (glfntype_glColorMaski) SDL_GL_GetProcAddress("glColorMaskiEXT");
	}
	if (!device->glColorMaski)
	{
		// FIXME: SupportsIndependentWriteMasks? -flibit
	}

	/* ARB_texture_multisample is probably used by nobody. */
	if (!device->glSampleMaski)
	{
		// FIXME: SupportsMultisampleMasks? -flibit
	}

	/* We need OpenGL 3.2+ for Core contexts. */
	if (device->useCoreProfile && !supportsCoreGL)
	{
		SDL_assert(0 && "OpenGL 3.2 support is required!");
	}

	/* Nothing beyond this can fail. */
	#undef GL_FAIL

	uint8_t supportsDebug = 1;

	/* Try KHR_debug first...
	 *
	 * "NOTE: when implemented in an OpenGL ES context, all entry points defined
	 * by this extension must have a "KHR" suffix. When implemented in an
	 * OpenGL context, all entry points must have NO suffix, as shown below."
	 * https://www.khronos.org/registry/OpenGL/extensions/KHR/KHR_debug.txt
	 */
	if (device->useES3)
	{
		device->glDebugMessageCallback = (glfntype_glDebugMessageCallback) SDL_GL_GetProcAddress("glDebugMessageCallbackKHR");
		device->glDebugMessageControl = (glfntype_glDebugMessageControl) SDL_GL_GetProcAddress("glDebugMessageControlKHR");
	}
	if (!device->glDebugMessageCallback || !device->glDebugMessageControl)
	{
		/* ... then try ARB_debug_output. */
		device->glDebugMessageCallback = (glfntype_glDebugMessageCallback) SDL_GL_GetProcAddress("glDebugMessageCallbackARB");
		device->glDebugMessageControl = (glfntype_glDebugMessageControl) SDL_GL_GetProcAddress("glDebugMessageControlARB");
	}
	if (!device->glDebugMessageCallback || !device->glDebugMessageControl)
	{
		supportsDebug = 0;
	}

	/* Android developers are incredibly stupid and export stub functions */
	if (device->useES3)
	{
		if (	!SDL_GL_ExtensionSupported("GL_KHR_debug") &&
			!SDL_GL_ExtensionSupported("GL_ARB_debug_output")	)
		{
			supportsDebug = 0;
		}
	}

	/* Set the callback, finally. */
	if (!supportsDebug)
	{
		SDL_LogWarn(
			SDL_LOG_CATEGORY_APPLICATION,
			"ARB_debug_output/KHR_debug not supported!"
		);
	}
	else
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

	/* GREMEDY_string_marker, for apitrace */
	if (!device->glStringMarkerGREMEDY)
	{
		SDL_LogWarn(
			SDL_LOG_CATEGORY_APPLICATION,
			"GREMEDY_string_marker not supported!"
		);
	}
}

/* Driver */

uint8_t OPENGL_PrepareWindowAttributes(uint8_t debugMode, uint32_t *flags)
{
	/* TODO: See SDL2_FNAPlatform PrepareGLAttributes */
	*flags = SDL_WINDOW_OPENGL;
	return 1;
}

FNA3D_Device* OPENGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters
) {
	/* Init the OpenGLDevice */
	OpenGLDevice *device = (OpenGLDevice*) SDL_malloc(sizeof(OpenGLDevice));

	/* Create OpenGL context */
	device->context = SDL_GL_CreateContext(
		(SDL_Window*) presentationParameters->deviceWindowHandle
	);

	/* Check for a possible ES context */
	int flags;
	int es3Flag = SDL_GL_CONTEXT_PROFILE_ES;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &flags);
	device->useES3 = (flags & es3Flag) == es3Flag;

	/* Check for a possible Core context */
	int coreFlag = SDL_GL_CONTEXT_PROFILE_CORE;
	device->useCoreProfile = (flags & coreFlag) == coreFlag;

	/* Check the window's depth/stencil format */
	int depthSize, stencilSize;
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
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(
		(SDL_Window*) presentationParameters->deviceWindowHandle,
		&wmInfo
	);
	if (wmInfo.subsystem == SDL_SYSWM_UIKIT)
	{
		/* FIXME: The uikit union is behind an #ifdef SDL_VIDEO_UIKIT.
		 * What should we do here...?
		 * -caleb
		 */
		// device->realBackbufferFBO = wmInfo.info.uikit.framebuffer;
		// device->realBackbufferRBO = wmInfo.info.uikit.colorbuffer;
	}
	else
	{
		device->realBackbufferFBO = 0;
		device->realBackbufferRBO = 0;
	}

	/* TODO: Init threaded GL crap where applicable */

	/* Print GL information */
	LoadGLGetString(device);
	const char* renderer	= (const char*) device->glGetString(GL_RENDERER);
	const char* version	= (const char*) device->glGetString(GL_VERSION);
	const char* vendor	= (const char*) device->glGetString(GL_VENDOR);
	char driverInfo[256];
	SDL_snprintf(
		driverInfo, 256,
		"OpenGL Device: %s\nOpenGL Driver: %s\nOpenGL Vendor: %s",
		renderer, version, vendor
	);
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		"IGLDevice: OpenGLDevice"
	);
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		driverInfo
	);

	// FIXME: REMOVE ME ASAP!
	uint8_t BUG_HACK_NOTANGLE = !SDL_strstr(
		(const char*) renderer,
		"Direct3D11"
	);

	/* Initialize entry points */
	LoadEntryPoints(device, driverInfo, BUG_HACK_NOTANGLE);

	/* TODO: The rest of the OpenGLDevice constructor */

	/* Set up and return the FNA3D_Device */
	FNA3D_Device *result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
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
