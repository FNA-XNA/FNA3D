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
	uint8_t supports_GL_EXT_draw_buffers2;
	uint8_t supports_ARB_texture_multisample;
	uint8_t supports_KHR_debug;
	uint8_t supports_GREMEDY_string_marker;

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

typedef struct OpenGLTexture /* Cast from FNA3D_Texture* */
{
	uint8_t filler;
} OpenGLTexture;

typedef struct OpenGLBuffer /* Cast from FNA3D_Buffer* */
{
	uint8_t filler;
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_draw_elements_base_vertex);
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
	OpenGLDevice *device = (OpenGLDevice*) driverData;
	SDL_assert(device->supports_ARB_draw_instanced);
	SDL_assert(device->supports_ARB_draw_elements_base_vertex);
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
	SDL_assert(device->supports_GL_EXT_draw_buffers2);
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
	device->supports_GL_EXT_draw_buffers2 = 1;
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
	int flags;
	int depthSize, stencilSize;
	SDL_SysWMinfo wmInfo;
	const char *renderer, *version, *vendor;
	char driverInfo[256];
	OpenGLDevice *device;
	FNA3D_Device *result;

	/* Init the OpenGLDevice */
	device = (OpenGLDevice*) SDL_malloc(sizeof(OpenGLDevice));

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
	else
#endif /* SDL_VIDEO_UIKIT */
	{
		device->realBackbufferFBO = 0;
		device->realBackbufferRBO = 0;
	}

	/* TODO: Init threaded GL crap where applicable */

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
		"IGLDevice: OpenGLDevice"
	);
	SDL_LogInfo(
		SDL_LOG_CATEGORY_APPLICATION,
		driverInfo
	);

	/* Initialize entry points */
	LoadEntryPoints(device, driverInfo, 0); /* FIXME: Debug context check */

	/* FIXME: REMOVE ME ASAP! TERRIBLE HACK FOR ANGLE! */
	if (!SDL_strstr(renderer, "Direct3D11"))
	{
		device->supports_ARB_draw_elements_base_vertex = 0;
	}

	/* TODO: The rest of the OpenGLDevice constructor */
	if (!device->supports_EXT_framebuffer_multisample)
	{
		/* MaxMultiSampleCount = 0; */
	}

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
