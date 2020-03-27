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

#if FNA3D_DRIVER_VULKAN

#include "FNA3D_Driver.h"
#include "FNA3D_Driver_Vulkan.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include "vulkan.h"
#include <SDL_vulkan.h>

/* Internal Structures */


/* Init/Quit */

uint32_t VULKAN_PrepareWindowAttributes(uint8_t debugMode)
{
    /* TODO */
}

void VULKAN_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
    /* TODO */
}

FNA3D_Device* VULKAN_CreateDevice(
    FNA3D_PresentationParameters *presentationParameters
) {
    /* TODO */
}

void VULKAN_DestroyDevice(FNA3D_Device *device)
{
    /* TODO */
}

/* Begin/End Frame */

void VULKAN_BeginFrame(FNA3D_Device *device)
{
    /* TODO */
}

void FNA3D_SwapBuffers(
	FNA3D_Device *device,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
    /* TODO */
}

void VULKAN_SetPresentationInterval(
	FNA3D_Device *device,
	FNA3D_PresentInterval presentInterval
) {
    /* TODO */
}

/* Drawing */

void VULKAN_Clear(
	FNA3D_Device *device,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
    /* TODO */
}

void VULKAN_DrawIndexedPrimitives(
	FNA3D_Device *device,
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

void VULKAN_DrawInstancedPrimitives(
	FNA3D_Device *device,
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

void VULKAN_DrawPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
    /* TODO */
}

FNA3DAPI void VULKAN_DrawUserIndexedPrimitives(
	FNA3D_Device *device,
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

FNA3DAPI void VULKAN_DrawUserPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
    /* TODO */
}

/* Mutable Render States */

void VULKAN_SetViewport(FNA3D_Device *device, FNA3D_Viewport *viewport)
{
    /* TODO */
}

void VULKAN_SetScissorRect(FNA3D_Device *device, FNA3D_Rect *scissor)
{
    /* TODO */
}

void VULKAN_GetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
) {
    /* TODO */
}

void VULKAN_SetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
) {
    /* TODO */
}

int32_t VULKAN_GetMultiSampleMask(FNA3D_Device *device)
{
    /* TODO */
}

void VULKAN_SetMultiSampleMask(FNA3D_Device *device, int32_t mask)
{
    /* TODO */
}

int32_t VULKAN_GetReferenceStencil(FNA3D_Device *device)
{
    /* TODO */
}

void VULKAN_SetReferenceStencil(FNA3D_Device *device, int32_t ref)
{
    /* TODO */
}

/* Immutable Render States */

void VULKAN_SetBlendState(
	FNA3D_Device *device,
	FNA3D_BlendState *blendState
) {
    /* TODO */
}

void VULKAN_SetDepthStencilState(
	FNA3D_Device *device,
	FNA3D_DepthStencilState *depthStencilState
) {
    /* TODO */
}

void VULKAN_ApplyRasterizerState(
	FNA3D_Device *device,
	FNA3D_RasterizerState *rasterizerState
) {
    /* TODO */
}

void VULKAN_VerifySampler(
	FNA3D_Device *device,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
    /* TODO */
}

/* Vertex State */

void VULKAN_ApplyVertexBufferBindings(
	FNA3D_Device *device,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
    /* TODO */
}

void VULKAN_ApplyVertexDeclaration(
	FNA3D_Device *device,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
    /* TODO */
}

/* Render Targets */

void VULKAN_SetRenderTargets(
	FNA3D_Device *device,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
    /* TODO */
}

void VULKAN_ResolveTarget(
	FNA3D_Device *device,
	FNA3D_RenderTargetBinding *target
) {
    /* TODO */
}

/* Backbuffer Functions */

void VULKAN_ResetBackbuffer(
	FNA3D_Device *device,
	FNA3D_PresentationParameters *presentationParameters
) {
    /* TODO */
}

void VULKAN_ReadBackbuffer(
	FNA3D_Device *device,
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

void VULKAN_GetBackbufferSize(
	FNA3D_Device *device,
	int32_t *w,
	int32_t *h
) {
    /* TODO */
}

FNA3D_SurfaceFormat VULKAN_GetBackbufferSurfaceFormat(
	FNA3D_Device *device
) {
    /* TODO */
}

FNA3D_DepthFormat VULKAN_GetBackbufferDepthFormat(FNA3D_Device *device)
{
    /* TODO */
}

int32_t VULKAN_GetBackbufferMultiSampleCount(FNA3D_Device *device)
{
    /* TODO */
}

/* Textures */

FNA3D_Texture* VULKAN_CreateTexture2D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    /* TODO */
}

FNA3D_Texture* VULKAN_CreateTexture3D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
    /* TODO */
}

FNA3D_Texture* VULKAN_CreateTextureCube(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    /* TODO */
}

void VULKAN_AddDisposeTexture(
	FNA3D_Device *device,
	FNA3D_Texture *texture
) {
    /* TODO */
}

void VULKAN_SetTextureData2D(
	FNA3D_Device *device,
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

void VULKAN_SetTextureData3D(
	FNA3D_Device *device,
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

void VULKAN_SetTextureDataCube(
	FNA3D_Device *device,
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

void VULKAN_SetTextureDataYUV(
	FNA3D_Device *device,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
    /* TODO */
}

void VULKAN_GetTextureData2D(
	FNA3D_Device *device,
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

void VULKAN_GetTextureData3D(
	FNA3D_Device *device,
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

void VULKAN_GetTextureDataCube(
	FNA3D_Device *device,
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

FNA3D_Renderbuffer* VULKAN_GenColorRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
    /* TODO */
}

FNA3D_Renderbuffer* VULKAN_GenDepthStencilRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
    /* TODO */
}

void VULKAN_AddDisposeRenderbuffer(
	FNA3D_Device *device,
	FNA3D_Renderbuffer *renderbuffer
) {
    /* TODO */
}

/* Vertex Buffers */

FNA3D_Buffer* VULKAN_GenVertexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
    /* TODO */
}
void VULKAN_AddDisposeVertexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

void VULKAN_SetVertexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
    /* TODO */
}

void VULKAN_GetVertexBufferData(
	FNA3D_Device *device,
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

FNA3D_Buffer* VULKAN_GenIndexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
    /* TODO */
}

FNA3DAPI void VULKAN_AddDisposeIndexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

void VULKAN_SetIndexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
    /* TODO */
}

void VULKAN_GetIndexBufferData(
	FNA3D_Device *device,
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

typedef struct MOJOSHADER_effect MOJOSHADER_effect;
typedef struct MOJOSHADER_effectTechnique MOJOSHADER_effectTechnique;
typedef struct MOJOSHADER_effectStateChanges MOJOSHADER_effectStateChanges;

FNA3D_Effect* VULKAN_CreateEffect(
	FNA3D_Device *device,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
    /* TODO */
}

FNA3D_Effect* VULKAN_CloneEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
    /* TODO */
}

void VULKAN_AddDisposeEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
    /* TODO */
}

void VULKAN_ApplyEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    /* TODO */
}

void VULKAN_BeginPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    /* TODO */
}

void VULKAN_EndPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
    /* TODO */
}

/* Queries */

FNA3D_Query* VULKAN_CreateQuery(FNA3D_Device *device)
{
    /* TODO */
}

void VULKAN_AddDisposeQuery(FNA3D_Device *device, FNA3D_Query *query)
{
    /* TODO */
}

void VULKAN_QueryBegin(FNA3D_Device *device, FNA3D_Query *query)
{
    /* TODO */
}

void VULKAN_QueryEnd(FNA3D_Device *device, FNA3D_Query *query)
{
    /* TODO */
}

uint8_t VULKAN_QueryComplete(FNA3D_Device *device, FNA3D_Query *query)
{
    /* TODO */
}

int32_t VULKAN_QueryPixelCount(
	FNA3D_Device *device,
	FNA3D_Query *query
) {
    /* TODO */
}

/* Feature Queries */

uint8_t VULKAN_SupportsDXT1(FNA3D_Device *device)
{
    /* TODO */
}

uint8_t VULKAN_SupportsS3TC(FNA3D_Device *device)
{
    /* TODO */
}

uint8_t VULKAN_SupportsHardwareInstancing(FNA3D_Device *device)
{
    /* TODO */
}

uint8_t VULKAN_SupportsNoOverwrite(FNA3D_Device *device)
{
    /* TODO */
}

int32_t VULKAN_GetMaxTextureSlots(FNA3D_Device *device)
{
    /* TODO */
}

int32_t VULKAN_GetMaxMultiSampleCount(FNA3D_Device *device)
{
    /* TODO */
}

/* Debugging */

FNA3DAPI void VULKAN_SetStringMarker(FNA3D_Device *device, const char *text)
{
    /* TODO */
}

/* Buffer Objects */

intptr_t VULKAN_GetBufferSize(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
    /* TODO */
}

/* Effect Objects */

MOJOSHADER_effect* VULKAN_GetEffectData(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
    /* TODO */
}

FNA3D_Driver VulkanDriver = {
	"Vulkan",
	VULKAN_PrepareWindowAttributes,
	VULKAN_GetDrawableSize,
	VULKAN_CreateDevice
};

#endif /* FNA_3D_DRIVER_VULKAN */
