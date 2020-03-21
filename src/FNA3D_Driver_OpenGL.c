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

#include <SDL.h>
#include "FNA3D_Driver_OpenGL.h"

/* Internal Structures */

typedef struct OpenGLDevice /* Cast from driverData */
{
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

/* TODO: Debug callback function...? */

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

static void LoadEntryPoints(OpenGLDevice *device)
{
	// TODO: The rest of the owl.
	/* GL entry points */
	#define GL_PROC(ext, ret, func, parms) \
		glfntype_##func func;
	#include "glfuncs.h"
	#undef GL_PROC

	#define GL_PROC(ext, ret, func, parms) \
		func = (glfntype_##func) SDL_GL_GetProcAddress(#func);
		// if (device->func == NULL) SDL_assert(0);
	#include "glfuncs.h"
	#undef GL_PROC
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
	FNA3D_Device *result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ASSIGN_DRIVER(OPENGL)
	result->driverData = NULL; /* TODO */
	return result;
}

FNA3D_Driver OpenGLDriver = {
	"OpenGL",
	OPENGL_PrepareWindowAttributes,
	OPENGL_CreateDevice
};

#endif /* FNA3D_DRIVER_OPENGL */
