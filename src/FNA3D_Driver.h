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

#ifndef FNA3D_DRIVER_H
#define FNA3D_DRIVER_H

#include "FNA3D.h"

struct FNA3D_Device
{
	/* Quit */

	void (*Destroy)(void* driverData);

	/* Begin/End Frame */

	void (*BeginFrame)(void* driverData);

	void (*SwapBuffers)(
		void* driverData,
		FNA3D_Rect *sourceRectangle,
		FNA3D_Rect *destinationRectangle,
		void* overrideWindowHandle
	);

	void (*SetPresentationInterval)(
		void* driverData,
		FNA3D_PresentInterval presentInterval
	);

	/* Drawing */

	void (*Clear)(
		void* driverData,
		FNA3D_ClearOptions options,
		FNA3D_Vec4 *color,
		float depth,
		int32_t stencil
	);

	void (*DrawIndexedPrimitives)(
		void* driverData,
		FNA3D_PrimitiveType primitiveType,
		int32_t baseVertex,
		int32_t minVertexIndex,
		int32_t numVertices,
		int32_t startIndex,
		int32_t primitiveCount,
		FNA3D_Buffer *indices,
		FNA3D_IndexElementSize indexElementSize
	);
	void (*DrawInstancedPrimitives)(
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
	);
	void (*DrawPrimitives)(
		void* driverData,
		FNA3D_PrimitiveType primitiveType,
		int32_t vertexStart,
		int32_t primitiveCount
	);
	void (*DrawUserIndexedPrimitives)(
		void* driverData,
		FNA3D_PrimitiveType primitiveType,
		void* vertexData,
		int32_t vertexOffset,
		int32_t numVertices,
		void* indexData,
		int32_t indexOffset,
		FNA3D_IndexElementSize indexElementSize,
		int32_t primitiveCount
	);
	void (*DrawUserPrimitives)(
		void* driverData,
		FNA3D_PrimitiveType primitiveType,
		void* vertexData,
		int32_t vertexOffset,
		int32_t primitiveCount
	);

	/* Mutable Render States */

	void (*SetViewport)(void* driverData, FNA3D_Viewport *viewport);
	void (*SetScissorRect)(void* driverData, FNA3D_Rect *scissor);

	void (*GetBlendFactor)(
		void* driverData,
		FNA3D_Color *blendFactor
	);
	void (*SetBlendFactor)(
		void* driverData,
		FNA3D_Color *blendFactor
	);

	int32_t (*GetMultiSampleMask)(void* driverData);
	void (*SetMultiSampleMask)(void* driverData, int32_t mask);

	int32_t (*GetReferenceStencil)(void* driverData);
	void (*SetReferenceStencil)(void* driverData, int32_t ref);

	/* Immutable Render States */

	void (*SetBlendState)(
		void* driverData,
		FNA3D_BlendState *blendState
	);
	void (*SetDepthStencilState)(
		void* driverData,
		FNA3D_DepthStencilState *depthStencilState
	);
	void (*ApplyRasterizerState)(
		void* driverData,
		FNA3D_RasterizerState *rasterizerState
	);
	void (*VerifySampler)(
		void* driverData,
		int32_t index,
		FNA3D_Texture *texture,
		FNA3D_SamplerState *sampler
	);

	/* Vertex State */

	void (*ApplyVertexBufferBindings)(
		void* driverData,
		/* FIXME: Oh shit VertexBufferBinding[] bindings, */
		int32_t numBindings,
		uint8_t bindingsUpdated,
		int32_t baseVertex
	);

	void (*ApplyVertexDeclaration)(
		void* driverData,
		/* FIXME: Oh shit VertexDeclaration vertexDeclaration, */
		void* ptr,
		int32_t vertexOffset
	);

	/* Render Targets */

	void (*SetRenderTargets)(
		void* driverData,
		/* FIXME: Oh shit RenderTargetBinding[] renderTargets, */
		FNA3D_Renderbuffer *renderbuffer,
		FNA3D_DepthFormat depthFormat
	);

	void (*ResolveTarget)(
		void* driverData
		/* FIXME: Oh shit RenderTargetBinding target */
	);

	/* Backbuffer Functions */

	FNA3D_Backbuffer* (*GetBackbuffer)(void* driverData);

	void (*ResetBackbuffer)(
		void* driverData,
		FNA3D_PresentationParameters *presentationParameters
	);

	void (*ReadBackbuffer)(
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
	);

	/* Textures */

	FNA3D_Texture* (*CreateTexture2D)(
		void* driverData,
		FNA3D_SurfaceFormat format,
		int32_t width,
		int32_t height,
		int32_t levelCount,
		uint8_t isRenderTarget
	);
	FNA3D_Texture* (*CreateTexture3D)(
		void* driverData,
		FNA3D_SurfaceFormat format,
		int32_t width,
		int32_t height,
		int32_t depth,
		int32_t levelCount
	);
	FNA3D_Texture* (*CreateTextureCube)(
		void* driverData,
		FNA3D_SurfaceFormat format,
		int32_t size,
		int32_t levelCount,
		uint8_t isRenderTarget
	);
	void (*AddDisposeTexture)(
		void* driverData,
		FNA3D_Texture *texture
	);
	void (*SetTextureData2D)(
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
	);
	void (*SetTextureData3D)(
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
	);
	void (*SetTextureDataCube)(
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
	);
	void (*SetTextureDataYUV)(
		void* driverData,
		FNA3D_Texture *textures,
		void* ptr
	);
	void (*GetTextureData2D)(
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
	);
	void (*GetTextureData3D)(
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
	);
	void (*GetTextureDataCube)(
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
	);

	/* Renderbuffers */

	FNA3D_Renderbuffer* (*GenColorRenderbuffer)(
		void* driverData,
		int32_t width,
		int32_t height,
		FNA3D_SurfaceFormat format,
		int32_t multiSampleCount,
		FNA3D_Texture *texture
	);
	FNA3D_Renderbuffer* (*GenDepthStencilRenderbuffer)(
		void* driverData,
		int32_t width,
		int32_t height,
		FNA3D_DepthFormat format,
		int32_t multiSampleCount
	);
	void (*AddDisposeRenderbuffer)(
		void* driverData,
		FNA3D_Renderbuffer *renderbuffer
	);

	/* Vertex Buffers */

	FNA3D_Buffer* (*GenVertexBuffer)(
		void* driverData,
		uint8_t dynamic,
		FNA3D_BufferUsage usage,
		int32_t vertexCount,
		int32_t vertexStride
	);
	void (*AddDisposeVertexBuffer)(
		void* driverData,
		FNA3D_Buffer *buffer
	);
	void (*SetVertexBufferData)(
		void* driverData,
		FNA3D_Buffer *buffer,
		int32_t offsetInBytes,
		void* data,
		int32_t dataLength,
		FNA3D_SetDataOptions options
	);
	void (*GetVertexBufferData)(
		void* driverData,
		FNA3D_Buffer *buffer,
		int32_t offsetInBytes,
		void* data,
		int32_t startIndex,
		int32_t elementCount,
		int32_t elementSizeInBytes,
		int32_t vertexStride
	);

	/* Index Buffers */

	FNA3D_Buffer* (*GenIndexBuffer)(
		void* driverData,
		uint8_t dynamic,
		FNA3D_BufferUsage usage,
		int32_t indexCount,
		FNA3D_IndexElementSize indexElementSize
	);
	void (*AddDisposeIndexBuffer)(
		void* driverData,
		FNA3D_Buffer *buffer
	);
	void (*SetIndexBufferData)(
		void* driverData,
		FNA3D_Buffer *buffer,
		int32_t offsetInBytes,
		void* data,
		int32_t dataLength,
		FNA3D_SetDataOptions options
	);
	void (*GetIndexBufferData)(
		void* driverData,
		FNA3D_Buffer *buffer,
		int32_t offsetInBytes,
		void* data,
		int32_t startIndex,
		int32_t elementCount,
		int32_t elementSizeInBytes
	);

	/* Effects */

	FNA3D_Effect* (*CreateEffect)(
		void* driverData,
		uint8_t *effectCode
	);
	FNA3D_Effect* (*CloneEffect)(
		void* driverData,
		FNA3D_Effect *effect
	);
	void (*AddDisposeEffect)(
		void* driverData,
		FNA3D_Effect *effect
	);
	void (*ApplyEffect)(
		void* driverData,
		FNA3D_Effect *effect,
		void* technique, /* FIXME: Should be MojoShader */
		uint32_t pass,
		void* stateChanges /* FIXME: Should be MojoShader */
	);
	void (*BeginPassRestore)(
		void* driverData,
		FNA3D_Effect *effect,
		void* stateChanges /* FIXME: Should be MojoShader */
	);
	void (*EndPassRestore)(
		void* driverData,
		FNA3D_Effect *effect
	);

	/* Queries */

	FNA3D_Query* (*CreateQuery)(void* driverData);
	void (*AddDisposeQuery)(void* driverData, FNA3D_Query *query);
	void (*QueryBegin)(void* driverData, FNA3D_Query *query);
	void (*QueryEnd)(void* driverData, FNA3D_Query *query);
	uint8_t (*QueryComplete)(void* driverData, FNA3D_Query *query);
	int32_t (*QueryPixelCount)(
		void* driverData,
		FNA3D_Query *query
	);

	/* Feature Queries */

	uint8_t (*SupportsDXT1)(void* driverData);
	uint8_t (*SupportsS3TC)(void* driverData);
	uint8_t (*SupportsHardwareInstancing)(void* driverData);
	uint8_t (*SupportsNoOverwrite)(void* driverData);

	int32_t (*GetMaxTextureSlots)(void* driverData);
	int32_t (*GetMaxMultiSampleCount)(void* driverData);

	/* Debugging */

	void (*SetStringMarker)(void* driverData, const char *text);

	/* TODO: Debug callback function...? */

	/* Buffer Objects */

	intptr_t (*GetBufferSize)(
		void* driverData,
		FNA3D_Buffer *buffer
	);

	/* Effect Objects */

	void* (*GetEffectData)(
		void* driverData,
		FNA3D_Effect *effect
	);

	/* Backbuffer Objects */

	int32_t (*GetBackbufferWidth)(
		void* driverData,
		FNA3D_Backbuffer *backbuffer
	);
	int32_t (*GetBackbufferHeight)(
		void* driverData,
		FNA3D_Backbuffer *backbuffer
	);
	FNA3D_DepthFormat (*GetBackbufferDepthFormat)(
		void* driverData,
		FNA3D_Backbuffer *backbuffer
	);
	int32_t (*GetBackbufferMultiSampleCount)(
		void* driverData,
		FNA3D_Backbuffer *backbuffer
	);
	void (*ResetFramebuffer)(
		void* driverData,
		FNA3D_Backbuffer *backbuffer,
		FNA3D_PresentationParameters *presentationParameters
	);

	/* Opaque pointer for the Driver */
	void* driverData;
};

typedef struct FNA3D_Driver
{
	uint8_t (*PrepareWindowAttributes)(uint8_t debugMode, uint32_t *flags);
	FNA3D_Device* (*CreateDevice)(FNA3D_PresentationParameters *presentationParameters);
} FNA3D_Driver;

extern FNA3D_Driver VulkanDriver;
extern FNA3D_Driver D3D11Driver;
extern FNA3D_Driver MetalDriver;
extern FNA3D_Driver OpenGLDriver;
extern FNA3D_Driver ModernGLDriver;
extern FNA3D_Driver GNMXDriver;

#endif /* FNA3D_DRIVER_H */
