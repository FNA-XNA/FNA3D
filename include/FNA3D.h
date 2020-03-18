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

#ifndef FNA3D_H
#define FNA3D_H

#ifdef _WIN32
#define FNA3DAPI __declspec(dllexport)
#define FNA3DCALL __cdecl
#else
#define FNA3DAPI
#define FNA3DCALL
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct FNA3D_Device FNA3D_Device;
typedef struct FNA3D_Texture FNA3D_Texture;
typedef struct FNA3D_Buffer FNA3D_Buffer;
typedef struct FNA3D_Renderbuffer FNA3D_Renderbuffer;
typedef struct FNA3D_Effect FNA3D_Effect;
typedef struct FNA3D_Query FNA3D_Query;
typedef struct FNA3D_Backbuffer FNA3D_Backbuffer;

/* Enumerations, should match XNA 4.0 */

typedef enum
{
	filler1
} FNA3D_PresentInterval;

typedef enum
{
	filler2
} FNA3D_ClearOptions;

typedef enum
{
	filler3
} FNA3D_PrimitiveType;

typedef enum
{
	filler4
} FNA3D_IndexElementSize;

typedef enum
{
	filler5
} FNA3D_SurfaceFormat;

typedef enum
{
	filler6
} FNA3D_DepthFormat;

typedef enum
{
	filler7
} FNA3D_CubeMapFace;

typedef enum
{
	filler8
} FNA3D_BufferUsage;

typedef enum
{
	filler9
} FNA3D_SetDataOptions;

/* Structures, should match XNA 4.0 */

typedef struct FNA3D_Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} FNA3D_Color;

typedef struct FNA3D_Rect
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
} FNA3D_Rect;

typedef struct FNA3D_Vec4
{
	float x;
	float y;
	float z;
	float w;
} FNA3D_Vec4;

typedef struct FNA3D_Viewport
{
} FNA3D_Viewport;

typedef struct FNA3D_BlendState
{
} FNA3D_BlendState;

typedef struct FNA3D_DepthStencilState
{
} FNA3D_DepthStencilState;

typedef struct FNA3D_RasterizerState
{
} FNA3D_RasterizerState;

typedef struct FNA3D_SamplerState
{
} FNA3D_SamplerState;

/* Functions */

/* Init/Quit */

FNA3DAPI FNA3D_Device* FNA3D_CreateDevice(
	/* FIXME: Oh shit PresentationParameters presentationParameters, */
	/* FIXME: Oh shit GraphicsAdapter adapter */
);

FNA3DAPI void FNA3D_DestroyDevice(FNA3D_Device *device);

/* Begin/End Frame */

FNA3DAPI void FNA3D_BeginFrame(FNA3D_Device *device);

FNA3DAPI void FNA3D_SwapBuffers(
	FNA3D_Device *device,
	FNA3D_Rect* sourceRectangle,
	FNA3D_Rect* destinationRectangle,
	void* overrideWindowHandle
);

FNA3DAPI void FNA3D_SetPresentationInterval(
	FNA3D_Device *device,
	FNA3D_PresentInterval presentInterval
);

/* Drawing */

FNA3DAPI void FNA3D_Clear(
	FNA3D_Device *device,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
);

FNA3DAPI void FNA3D_DrawIndexedPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	int32_t baseVertex,
	int32_t minVertexIndex,
	int32_t numVertices,
	int32_t startIndex,
	int32_t primitiveCount,
	FNA3D_Buffer *indices,
	FNA3D_IndexElementSize indexElementSize
);
FNA3DAPI void FNA3D_DrawInstancedPrimitives(
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
);
FNA3DAPI void FNA3D_DrawPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
);
FNA3DAPI void FNA3D_DrawUserIndexedPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t numVertices,
	void* indexData,
	int32_t indexOffset,
	FNA3D_IndexElementSize indexElementSize,
	int32_t primitiveCount
);
FNA3DAPI void FNA3D_DrawUserPrimitives(
	FNA3D_Device *device,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
);

/* Mutable Render States */

FNA3DAPI void FNA3D_SetViewport(FNA3D_Device *device, FNA3D_Viewport *viewport);
FNA3DAPI void FNA3D_SetScissorRect(FNA3D_Device *device, FNA3D_Rect *scissor);

FNA3DAPI void FNA3D_GetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
);
FNA3DAPI void FNA3D_SetBlendFactor(
	FNA3D_Device *device,
	FNA3D_Color *blendFactor
);

FNA3DAPI int32_t FNA3D_GetMultiSampleMask(FNA3D_Device *device);
FNA3DAPI void FNA3D_SetMultiSampleMask(FNA3D_Device *device, int32_t mask);

FNA3DAPI int32_t FNA3D_GetReferenceStencil(FNA3D_Device *device);
FNA3DAPI void FNA3D_SetReferenceStencil(FNA3D_Device *device, int32_t ref);

/* Immutable Render States */

FNA3DAPI void FNA3D_SetBlendState(
	FNA3D_Device *device,
	FNA3D_BlendState *blendState
);
FNA3DAPI void FNA3D_SetDepthStencilState(
	FNA3D_Device *device,
	FNA3D_DepthStencilState *depthStencilState
);
FNA3DAPI void FNA3D_ApplyRasterizerState(
	FNA3D_Device *device,
	FNA3D_RasterizerState *rasterizerState
);
FNA3DAPI void VerifySampler(
	FNA3D_Device *device,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
);

/* Vertex State */

FNA3DAPI void FNA3D_ApplyVertexBufferBindings(
	FNA3D_Device *device,
	/* FIXME: Oh shit VertexBufferBinding[] bindings, */
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
);

FNA3DAPI void FNA3D_ApplyVertexDeclaration(
	FNA3D_Device *device,
	/* FIXME: Oh shit VertexDeclaration vertexDeclaration, */
	void* ptr,
	int32_t vertexOffset
);

/* Render Targets */

FNA3DAPI void FNA3D_SetRenderTargets(
	FNA3D_Device *device,
	/* FIXME: Oh shit RenderTargetBinding[] renderTargets, */
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
);

FNA3DAPI void FNA3D_ResolveTarget(
	FNA3D_Device *device
	/* FIXME: Oh shit RenderTargetBinding target */
);

/* Backbuffer Functions */

FNA3DAPI FNA3D_Backbuffer* FNA3D_GetBackbuffer(FNA3D_Device *device);

FNA3DAPI void FNA3D_ResetBackbuffer(
	FNA3D_Device *device
	/* FIXME: Oh shit PresentationParameters presentationParameters, */
	/* FIXME: Oh shit GraphicsAdapter adapter */
);

FNA3DAPI void FNA3D_ReadBackbuffer(
	FNA3D_Device *device,
	void* data,
	int32_t dataLen,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t subX,
	int32_t subY,
	int32_t subW,
	int32_t subH
);

/* Textures */


FNA3DAPI FNA3D_Texture* FNA3D_CreateTexture2D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
);
FNA3DAPI FNA3D_Texture* FNA3D_CreateTexture3D(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
);
FNA3DAPI FNA3D_Texture* FNA3D_CreateTextureCube(
	FNA3D_Device *device,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
);
FNA3DAPI void FNA3D_AddDisposeTexture(
	FNA3D_Device *device,
	FNA3D_Texture *texture
);
FNA3DAPI void FNA3D_SetTextureData2D(
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
);
FNA3DAPI void FNA3D_SetTextureData3D(
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
);
FNA3DAPI void FNA3D_SetTextureDataCube(
	FNA3D_Device *device,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t xOffset,
	int32_t yOffset,
	int32_t width,
	int32_t height,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
);
FNA3DAPI void FNA3D_SetTextureDataYUV(
	FNA3D_Device *device,
	FNA3D_Texture* textures,
	void* ptr
);
FNA3DAPI void FNA3D_GetTextureData2D(
	FNA3D_Device *device,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t level,
	int32_t subX,
	int32_t subY,
	int32_t subW,
	int32_t subH,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
);
FNA3DAPI void FNA3D_GetTextureData3D(
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
);
FNA3DAPI void FNA3D_GetTextureDataCube(
	FNA3D_Device *device,
	FNA3D_Texture *texture,
	FNA3D_SurfaceFormat format,
	int32_t size,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	int32_t subX,
	int32_t subY,
	int32_t subW,
	int32_t subH,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
);

/* Renderbuffers */

FNA3DAPI FNA3D_Renderbuffer* FNA3D_GenColorRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
);
FNA3DAPI FNA3D_Renderbuffer* FNA3D_GenDepthStencilRenderbuffer(
	FNA3D_Device *device,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
);
FNA3DAPI void FNA3D_AddDisposeRenderbuffer(
	FNA3D_Device *device,
	FNA3D_Renderbuffer *renderbuffer
);

/* Vertex Buffers */

FNA3DAPI FNA3D_Buffer* FNA3D_GenVertexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
);
FNA3DAPI void FNA3D_AddDisposeVertexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
);
FNA3DAPI void FNA3D_SetVertexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
);
FNA3DAPI void FNA3D_GetVertexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
);

/* Index Buffers */

FNA3DAPI FNA3D_Buffer* FNA3D_GenIndexBuffer(
	FNA3D_Device *device,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
);
FNA3DAPI void FNA3D_AddDisposeIndexBuffer(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
);
FNA3DAPI void FNA3D_SetIndexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
);
FNA3DAPI void FNA3D_GetIndexBufferData(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t startIndex,
	int32_t elementCount,
	int32_t elementSizeInBytes
);

/* Effects */

FNA3DAPI FNA3D_Effect* FNA3D_CreateEffect(
	FNA3D_Device *device,
	uint8_t *effectCode
);
FNA3DAPI FNA3D_Effect* FNA3D_CloneEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);
FNA3DAPI void FNA3D_AddDisposeEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);
FNA3DAPI void FNA3D_ApplyEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	void* technique, /* FIXME: Should be MojoShader */
	uint32_t pass,
	void* stateChanges /* FIXME: Should be MojoShader */
);
FNA3DAPI void FNA3D_BeginPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	void* stateChanges /* FIXME: Should be MojoShader */
);
FNA3DAPI void FNA3D_EndPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);
#endif

/* Queries */

FNA3DAPI FNA3D_Query* FNA3D_CreateQuery(FNA3D_Device *device);
FNA3DAPI void FNA3D_AddDisposeQuery(FNA3D_Device *device, FNA3D_Query *query);
FNA3DAPI void FNA3D_QueryBegin(FNA3D_Device *device, FNA3D_Query *query);
FNA3DAPI void FNA3D_QueryEnd(FNA3D_Device *device, FNA3D_Query *query);
FNA3DAPI uint8_t FNA3D_QueryComplete(FNA3D_Device *device, FNA3D_Query *query);
FNA3DAPI int32_t FNA3D_QueryPixelCount(
	FNA3D_Device *device,
	FNA3D_Query *query
);

/* Feature Queries */

FNA3DAPI uint8_t FNA3D_SupportsDXT1(FNA3D_Device *device);
FNA3DAPI uint8_t FNA3D_SupportsS3TC(FNA3D_Device *device);
FNA3DAPI uint8_t FNA3D_SupportsHardwareInstancing(FNA3D_Device *device);
FNA3DAPI uint8_t FNA3D_SupportsNoOverwrite(FNA3D_Device *device);

FNA3DAPI int32_t FNA3D_GetMaxTextureSlots(FNA3D_Device *device);
FNA3DAPI int32_t FNA3D_GetMaxMultiSampleCount(FNA3D_Device *device);

/* Debugging */

FNA3DAPI void FNA3D_SetStringMarker(FNA3D_Device *device, const char *text);

/* TODO: Debug callback function...? */

/* Buffer Objects */

FNA3DAPI intptr_t FNA3D_GetBufferSize(
	FNA3D_Device *device,
	FNA3D_Buffer *buffer
);

/* Effect Objects */

FNA3DAPI void* FNA3D_GetEffectData(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);

/* Backbuffer Objects */

FNA3DAPI int32_t FNA3D_GetBackbufferWidth(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
);
FNA3DAPI int32_t FNA3D_GetBackbufferHeight(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
);
FNA3DAPI FNA3D_DepthFormat FNA3D_GetBackbufferDepthFormat(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
);
FNA3DAPI int32_t FNA3D_GetBackbufferMultiSampleCount(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
);
FNA3DAPI void FNA3D_ResetFramebuffer(
	FNA3D_Device *device,
	FNA3D_Backbuffer *backbuffer
	/* FIXME: Oh shit PresentationParameters */
);

#ifdef __cplusplus
}
#endif /* __cplusplus */
