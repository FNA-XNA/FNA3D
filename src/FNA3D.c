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

#include "FNA3D.h"

#include <SDL.h>

/* Init/Quit */

uint32_t FNA3D_PrepareWindowAttributes(uint8_t debugMode)
{
	/* TODO */
}

FNA3D_Device* FNA3D_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
	return NULL;
}

void FNA3D_DestroyDevice(FNA3D_Device *device)
{
	/* TODO */
}

/* Begin/End Frame */

void FNA3D_BeginFrame(FNA3D_Device *device)
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

void FNA3D_SetPresentationInterval(
	FNA3D_Device *device,
	FNA3D_PresentInterval presentInterval
) {
	/* TODO */
}

/* Drawing */

void FNA3D_Clear(
	FNA3D_Device *device,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	/* TODO */
}

void FNA3D_DrawIndexedPrimitives(
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

void FNA3D_DrawInstancedPrimitives(
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

void FNA3D_DrawPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	/* TODO */
}

void FNA3D_DrawUserIndexedPrimitives(
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

void FNA3D_DrawUserPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	/* TODO */
}

/* Mutable Render States */

void FNA3D_SetViewport(FNA3D_Device *device, FNA3D_Viewport *viewport)
{
	/* TODO */
}

void FNA3D_SetScissorRect(FNA3D_Device *device, FNA3D_Rect *scissor)
{
	/* TODO */
}

void FNA3D_GetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
) {
	/* TODO */
}

void FNA3D_SetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
) {
	/* TODO */
}

int32_t FNA3D_GetMultiSampleMask(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

void FNA3D_SetMultiSampleMask(FNA3D_Device *device, int32_t mask)
{
	/* TODO */
}

int32_t FNA3D_GetReferenceStencil(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}
void FNA3D_SetReferenceStencil(FNA3D_Device *device, int32_t ref)
{
	/* TODO */
}

/* Immutable Render States */

void FNA3D_SetBlendState(
	FNA3D_Device *device,
	FNA3D_BlendState *blendState
) {
	/* TODO */
}

void FNA3D_SetDepthStencilState(
	FNA3D_Device *device,
	FNA3D_DepthStencilState *depthStencilState
) {
	/* TODO */
}

void FNA3D_ApplyRasterizerState(
	FNA3D_Device *device,
	FNA3D_RasterizerState *rasterizerState
) {
	/* TODO */
}

void VerifySampler(
	FNA3D_Device *device,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	/* TODO */
}

/* Vertex State */

void FNA3D_ApplyVertexBufferBindings(
	FNA3D_Device *device,
	/* FIXME: Oh shit VertexBufferBinding[] bindings, */
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	/* TODO */
}

void FNA3D_ApplyVertexDeclaration(
	FNA3D_Device *device,
	/* FIXME: Oh shit VertexDeclaration vertexDeclaration, */
	void* ptr,
	int32_t vertexOffset
) {
	/* TODO */
}

/* Render Targets */

void FNA3D_SetRenderTargets(
	FNA3D_Device *device,
	/* FIXME: Oh shit RenderTargetBinding[] renderTargets, */
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	/* TODO */
}

void FNA3D_ResolveTarget(
	FNA3D_Device *device
	/* FIXME: Oh shit RenderTargetBinding target */
) {
	/* TODO */
}

/* Backbuffer Functions */

FNA3D_Backbuffer* FNA3D_GetBackbuffer(FNA3D_Device *device)
{
	/* TODO */
	return NULL;
}

void FNA3D_ResetBackbuffer(
	FNA3D_Device *device,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
}

void FNA3D_ReadBackbuffer(
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

/* Textures */

FNA3D_Texture* FNA3D_CreateTexture2D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
	return NULL;
}

FNA3D_Texture* FNA3D_CreateTexture3D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	/* TODO */
	return NULL;
}

FNA3D_Texture* FNA3D_CreateTextureCube(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeTexture(
	FNA3D_Device *device,
	FNA3D_Texture *texture
) {
	/* TODO */
}

void FNA3D_SetTextureData2D(
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

void FNA3D_SetTextureData3D(
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

void FNA3D_SetTextureDataCube(
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

void FNA3D_SetTextureDataYUV(
	FNA3D_Device *device,
	FNA3D_Texture *textures,
	void* ptr
) {
	/* TODO */
}

void FNA3D_GetTextureData2D(
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

void FNA3D_GetTextureData3D(
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

void FNA3D_GetTextureDataCube(
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

FNA3D_Renderbuffer* FNA3D_GenColorRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	/* TODO */
	return NULL;
}

FNA3D_Renderbuffer* FNA3D_GenDepthStencilRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeRenderbuffer(
	FNA3D_Device *device,
	FNA3D_Renderbuffer *renderbuffer
) {
	/* TODO */
}

/* Vertex Buffers */

FNA3D_Buffer* FNA3D_GenVertexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeVertexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

void FNA3D_SetVertexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

void FNA3D_GetVertexBufferData(
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

FNA3D_Buffer* FNA3D_GenIndexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeIndexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
	/* TODO */
}

void FNA3D_SetIndexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	/* TODO */
}

void FNA3D_GetIndexBufferData(
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

FNA3D_Effect* FNA3D_CreateEffect(
	FNA3D_Device *device,
	uint8_t *effectCode
) {
	/* TODO */
	return NULL;
}

FNA3D_Effect* FNA3D_CloneEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
	/* TODO */
}

void FNA3D_ApplyEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	void* technique, /* FIXME: Should be MojoShader */
	uint32_t pass,
	void* stateChanges /* FIXME: Should be MojoShader */
) {
	/* TODO */
}

void FNA3D_BeginPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	void* stateChanges /* FIXME: Should be MojoShader */
) {
	/* TODO */
}

void FNA3D_EndPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
	/* TODO */
}

/* Queries */

FNA3D_Query* FNA3D_CreateQuery(FNA3D_Device *device)
{
	/* TODO */
	return NULL;
}

void FNA3D_AddDisposeQuery(FNA3D_Device *device, FNA3D_Query *query)
{
	/* TODO */
}

void FNA3D_QueryBegin(FNA3D_Device *device, FNA3D_Query *query)
{
	/* TODO */
}

void FNA3D_QueryEnd(FNA3D_Device *device, FNA3D_Query *query)
{
	/* TODO */
}

uint8_t FNA3D_QueryComplete(FNA3D_Device *device, FNA3D_Query *query)
{
	/* TODO */
	return 1;
}

int32_t FNA3D_QueryPixelCount(
	FNA3D_Device *device,
	FNA3D_Query *query
) {
	/* TODO */
	return 0;
}

/* Feature Queries */

uint8_t FNA3D_SupportsDXT1(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

uint8_t FNA3D_SupportsS3TC(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

uint8_t FNA3D_SupportsHardwareInstancing(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

uint8_t FNA3D_SupportsNoOverwrite(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

int32_t FNA3D_GetMaxTextureSlots(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

int32_t FNA3D_GetMaxMultiSampleCount(FNA3D_Device *device)
{
	/* TODO */
	return 0;
}

/* Debugging */

void FNA3D_SetStringMarker(FNA3D_Device *device, const char *text)
{
	/* TODO */
}

/* TODO: Debug callback function...? */

/* Buffer Objects */

intptr_t FNA3D_GetBufferSize(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
) {
	/* TODO */
	return 0;
}

/* Effect Objects */

void* FNA3D_GetEffectData(
	FNA3D_Device *device,
	FNA3D_Effect *effect
) {
	/* TODO */
	return NULL;
}

/* Backbuffer Objects */

int32_t FNA3D_GetBackbufferWidth(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

int32_t FNA3D_GetBackbufferHeight(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

FNA3D_DepthFormat FNA3D_GetBackbufferDepthFormat(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return FNA3D_DEPTHFORMAT_NONE;
}

int32_t FNA3D_GetBackbufferMultiSampleCount(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
) {
	/* TODO */
	return 0;
}

void FNA3D_ResetFramebuffer(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer,
	FNA3D_PresentationParameters *presentationParameters
) {
	/* TODO */
}
