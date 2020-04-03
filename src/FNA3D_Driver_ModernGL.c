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

#include <SDL.h>

/* Internal Structures */

typedef struct ModernGLTexture /* Cast FNA3D_Texture* to this! */
{
	uint8_t filler;
} ModernGLTexture;

typedef struct ModernGLRenderbuffer /* Cast FNA3D_Renderbuffer* to this! */
{
	uint8_t filler;
} ModernGLRenderbuffer;

typedef struct ModernGLBuffer /* Cast FNA3D_Buffer* to this! */
{
	intptr_t size;
} ModernGLBuffer;

typedef struct ModernGLEffect /* Cast FNA3D_Effect* to this! */
{
	MOJOSHADER_effect *effect;
} ModernGLEffect;

typedef struct ModernGLQuery /* Cast FNA3D_Query* to this! */
{
	uint8_t filler;
} ModernGLQuery;

typedef struct ModernGLRenderer /* Cast FNA3D_Renderer* to this! */
{
	uint8_t filler;
} ModernGLRenderer;

/* Quit */

static void MODERNGL_DestroyDevice(FNA3D_Device *device)
{
	ModernGLRenderer* renderer = (ModernGLRenderer*) device->driverData;
	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void MODERNGL_BeginFrame(FNA3D_Renderer *driverData)
{
}

static void MODERNGL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
}

static void MODERNGL_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
}

/* Drawing */

static void MODERNGL_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
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
}

static void MODERNGL_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
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
}

static void MODERNGL_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
}

/* Mutable Render States */

static void MODERNGL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
}

static void MODERNGL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
}

static void MODERNGL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
}

static void MODERNGL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
}

static int32_t MODERNGL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	return 0;
}

static void MODERNGL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
}

static int32_t MODERNGL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	return 0;
}

static void MODERNGL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
}

/* Immutable Render States */

static void MODERNGL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
}

static void MODERNGL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
}

static void MODERNGL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
}

static void MODERNGL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
}

/* Vertex State */

static void MODERNGL_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
}

static void MODERNGL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
}

/* Render Targets */

static void MODERNGL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
}

static void MODERNGL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
}

/* Backbuffer Functions */

static void MODERNGL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
}

static void MODERNGL_ReadBackbuffer(
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
}

static void MODERNGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
}

static FNA3D_SurfaceFormat MODERNGL_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	return FNA3D_SURFACEFORMAT_COLOR;
}

static FNA3D_DepthFormat MODERNGL_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	return FNA3D_DEPTHFORMAT_NONE;
}

static int32_t MODERNGL_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	return 0;
}

/* Textures */

static FNA3D_Texture* MODERNGL_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	return NULL;
}

static FNA3D_Texture* MODERNGL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	return NULL;
}

static FNA3D_Texture* MODERNGL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	return NULL;
}

static void MODERNGL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
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
}

static void MODERNGL_SetTextureData3D(
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
}

static void MODERNGL_GetTextureData2D(
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
}

static void MODERNGL_GetTextureData3D(
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
}

static void MODERNGL_GetTextureDataCube(
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
	return NULL;
}

static FNA3D_Renderbuffer* MODERNGL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	return NULL;
}

static void MODERNGL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
}

/* Vertex Buffers */

static FNA3D_Buffer* MODERNGL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	return NULL;
}

static void MODERNGL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
}

static void MODERNGL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
}

static void MODERNGL_GetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
}

/* Index Buffers */

static FNA3D_Buffer* MODERNGL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	return NULL;
}

static void MODERNGL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
}

static void MODERNGL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
}

static void MODERNGL_GetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
) {
}

/* Effects */

static FNA3D_Effect* MODERNGL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
	return NULL;
}

static FNA3D_Effect* MODERNGL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	return NULL;
}

static void MODERNGL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
}

static void MODERNGL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
}

static void MODERNGL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
}

static void MODERNGL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
}

/* Queries */

static FNA3D_Query* MODERNGL_CreateQuery(FNA3D_Renderer *driverData)
{
	return NULL;
}

static void MODERNGL_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
}

static void MODERNGL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
}

static void MODERNGL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
}

static uint8_t MODERNGL_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	return 1;
}

static int32_t MODERNGL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	return 0;
}

/* Feature Queries */

static uint8_t MODERNGL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t MODERNGL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t MODERNGL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t MODERNGL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 0;
}

static int32_t MODERNGL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	return 0;
}

static int32_t MODERNGL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	return 0;
}

/* Debugging */

static void MODERNGL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
}

/* Buffer Objects */

static intptr_t MODERNGL_GetBufferSize(FNA3D_Buffer *buffer)
{
	return ((ModernGLBuffer*) buffer)->size;
}

/* Effect Objects */

static MOJOSHADER_effect* MODERNGL_GetEffectData(FNA3D_Effect *effect)
{
	return ((ModernGLEffect*) effect)->effect;
}

/* Driver */

static uint8_t MODERNGL_PrepareWindowAttributes(uint32_t *flags)
{
	return 0; /* Set this to 1 when the driver is usable! */
}

static void MODERNGL_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
}

static FNA3D_Device* MODERNGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	FNA3D_Device *result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	ModernGLRenderer *renderer = (ModernGLRenderer*) SDL_malloc(
		sizeof(ModernGLRenderer)
	);
	result->driverData = (FNA3D_Renderer*) renderer;
	ASSIGN_DRIVER(MODERNGL)
	return result;
}

FNA3D_Driver ModernGLDriver = {
	"ModernGL",
	MODERNGL_PrepareWindowAttributes,
	MODERNGL_GetDrawableSize,
	MODERNGL_CreateDevice
};

#endif /* FNA3D_DRIVER_MODERNGL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
