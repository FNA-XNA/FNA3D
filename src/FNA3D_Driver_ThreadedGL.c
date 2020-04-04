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

#if FNA3D_DRIVER_THREADEDGL

#if !FNA3D_DRIVER_MODERNGL
#error ThreadedGL requires ModernGL. Fix your build.
#endif

#include "FNA3D_Driver.h"

#include <SDL.h>

/* -Wpedantic nameless union/struct silencing */
#ifndef FNA3DNAMELESS
#ifdef __GNUC__
#define FNA3DNAMELESS __extension__
#else
#define FNA3DNAMELESS
#endif /* __GNUC__ */
#endif /* FNA3DNAMELESS */

/* Internal Structures */

typedef struct GLThreadCommand GLThreadCommand;
struct GLThreadCommand
{
	uint8_t type;
	SDL_sem *semaphore;
	GLThreadCommand *next;

	#define COMMAND_CREATEDEVICE			0
	#define COMMAND_BEGINFRAME			1
	#define COMMAND_SWAPBUFFERS			2
	#define COMMAND_SETPRESENTATIONINTERVAL		3
	#define COMMAND_CLEAR				4
	#define COMMAND_DRAWINDEXEDPRIMITIVES		5
	#define COMMAND_DRAWINSTANCEDPRIMITIVES		6
	#define COMMAND_DRAWPRIMITIVES			7
	#define COMMAND_DRAWUSERINDEXEDPRIMITIVES	8
	#define COMMAND_DRAWUSERPRIMITIVES		9
	#define COMMAND_SETVIEWPORT			10
	#define COMMAND_SETSCISSORRECT			11
	#define COMMAND_GETBLENDFACTOR			12
	#define COMMAND_SETBLENDFACTOR			13
	#define COMMAND_GETMULTISAMPLEMASK		14
	#define COMMAND_SETMULTISAMPLEMASK		15
	#define COMMAND_GETREFERENCESTENCIL		16
	#define COMMAND_SETREFERENCESTENCIL		17
	#define COMMAND_SETBLENDSTATE			18
	#define COMMAND_SETDEPTHSTENCILSTATE		19
	#define COMMAND_APPLYRASTERIZERSTATE		20
	#define COMMAND_VERIFYSAMPLER			21
	#define COMMAND_APPLYVERTEXBUFFERBINDINGS	22
	#define COMMAND_APPLYVERTEXDECLARATION		23
	#define COMMAND_SETRENDERTARGETS		24
	#define COMMAND_RESOLVETARGET			25
	#define COMMAND_RESETBACKBUFFER			26
	#define COMMAND_READBACKBUFFER			27
	#define COMMAND_GETBACKBUFFERSIZE		28
	#define COMMAND_GETBACKBUFFERSURFACEFORMAT	29
	#define COMMAND_GETBACKBUFFERDEPTHFORMAT	30
	#define COMMAND_GETBACKBUFFERMULTISAMPLECOUNT	31
	#define COMMAND_CREATETEXTURE2D			32
	#define COMMAND_CREATETEXTURE3D			33
	#define COMMAND_CREATETEXTURECUBE		34
	#define COMMAND_ADDDISPOSETEXTURE		35
	#define COMMAND_SETTEXTUREDATA2D		36
	#define COMMAND_SETTEXTUREDATA3D		37
	#define COMMAND_SETTEXTUREDATACUBE		38
	#define COMMAND_SETTEXTUREDATAYUV		39
	#define COMMAND_GETTEXTUREDATA2D		40
	#define COMMAND_GETTEXTUREDATA3D		41
	#define COMMAND_GETTEXTUREDATACUBE		42
	#define COMMAND_GENCOLORRENDERBUFFER		43
	#define COMMAND_GENDEPTHSTENCILRENDERBUFFER	44
	#define COMMAND_ADDDISPOSERENDERBUFFER		45
	#define COMMAND_GENVERTEXBUFFER			46
	#define COMMAND_ADDDISPOSEVERTEXBUFFER		47
	#define COMMAND_SETVERTEXBUFFERDATA		48
	#define COMMAND_GETVERTEXBUFFERDATA		49
	#define COMMAND_GENINDEXBUFFER			50
	#define COMMAND_ADDDISPOSEINDEXBUFFER		51
	#define COMMAND_SETINDEXBUFFERDATA		52
	#define COMMAND_GETINDEXBUFFERDATA		53
	#define COMMAND_CREATEEFFECT			54
	#define COMMAND_CLONEEFFECT			55
	#define COMMAND_ADDDISPOSEEFFECT		56
	#define COMMAND_APPLYEFFECT			57
	#define COMMAND_BEGINPASSRESTORE		58
	#define COMMAND_ENDPASSRESTORE			59
	#define COMMAND_CREATEQUERY			60
	#define COMMAND_ADDDISPOSEQUERY			61
	#define COMMAND_QUERYBEGIN			62
	#define COMMAND_QUERYEND			63
	#define COMMAND_QUERYCOMPLETE			64
	#define COMMAND_QUERYPIXELCOUNT			65
	#define COMMAND_SUPPORTSDXT1			66
	#define COMMAND_SUPPORTSS3TC			67
	#define COMMAND_SUPPORTSHARDWAREINSTANCING	68
	#define COMMAND_SUPPORTSNOOVERWRITE		69
	#define COMMAND_GETMAXTEXTURESLOTS		70
	#define COMMAND_GETMAXMULTISAMPLECOUNT		71
	#define COMMAND_SETSTRINGMARKER			72
	#define COMMAND_GETBUFFERSIZE			73
	#define COMMAND_GETEFFECTDATA			74

	FNA3DNAMELESS union
	{
		struct
		{
			FNA3D_PresentationParameters *presentationParameters;
			uint8_t debugMode;
		} createDevice;

		/* TODO: Command Hell Part 1 */

		struct
		{
			FNA3D_SurfaceFormat format;
			int32_t width;
			int32_t height;
			int32_t levelCount;
			uint8_t isRenderTarget;
			FNA3D_Texture *retval;
		} createTexture2D;
		struct
		{
			FNA3D_SurfaceFormat format;
			int32_t width;
			int32_t height;
			int32_t depth;
			int32_t levelCount;
			FNA3D_Texture *retval;
		} createTexture3D;
		struct
		{
			FNA3D_SurfaceFormat format;
			int32_t size;
			int32_t levelCount;
			uint8_t isRenderTarget;
			FNA3D_Texture *retval;
		} createTextureCube;
		/* TODO: AddDisposeTexture */
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
			int32_t level;
			void* data;
			int32_t dataLength;
		} setTextureData2D;
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t level;
			int32_t left;
			int32_t top;
			int32_t right;
			int32_t bottom;
			int32_t front;
			int32_t back;
			void* data;
			int32_t dataLength;
		} setTextureData3D;
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
			FNA3D_CubeMapFace cubeMapFace;
			int32_t level;
			void* data;
			int32_t dataLength;
		} setTextureDataCube;
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t textureWidth;
			int32_t textureHeight;
			int32_t level;
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
			void* data;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
		} getTextureData2D;
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t left;
			int32_t top;
			int32_t front;
			int32_t right;
			int32_t bottom;
			int32_t back;
			int32_t level;
			void* data;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
		} getTextureData3D;
		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t textureSize;
			FNA3D_CubeMapFace cubeMapFace;
			int32_t level;
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
			void* data;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
		} getTextureDataCube;
		struct
		{
			int32_t width;
			int32_t height;
			FNA3D_SurfaceFormat format;
			int32_t multiSampleCount;
			FNA3D_Texture *texture;
			FNA3D_Renderbuffer *retval;
		} genColorRenderbuffer;
		struct
		{
			int32_t width;
			int32_t height;
			FNA3D_DepthFormat format;
			int32_t multiSampleCount;
			FNA3D_Renderbuffer *retval;
		} genDepthStencilRenderbuffer;
		/* TODO: AddDisposeRenderbuffer */
		struct
		{
			uint8_t dynamic;
			FNA3D_BufferUsage usage;
			int32_t vertexCount;
			int32_t vertexStride;
			FNA3D_Buffer *retval;
		} genVertexBuffer;
		/* TODO: AddDisposeVertexBuffer */
		struct
		{
			FNA3D_Buffer *buffer;
			int32_t offsetInBytes;
			void* data;
			int32_t dataLength;
			FNA3D_SetDataOptions options;
		} setVertexBufferData;
		struct
		{
			FNA3D_Buffer *buffer;
			int32_t offsetInBytes;
			void* data;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
			int32_t vertexStride;
		} getVertexBufferData;
		struct
		{
			uint8_t dynamic;
			FNA3D_BufferUsage usage;
			int32_t indexCount;
			FNA3D_IndexElementSize indexElementSize;
			FNA3D_Buffer *retval;
		} genIndexBuffer;
		/* TODO: AddDisposeIndexBuffer */
		struct
		{
			FNA3D_Buffer *buffer;
			int32_t offsetInBytes;
			void* data;
			int32_t dataLength;
			FNA3D_SetDataOptions options;
		} setIndexBufferData;
		struct
		{
			FNA3D_Buffer *buffer;
			int32_t offsetInBytes;
			void* data;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
		} getIndexBufferData;
		struct
		{
			uint8_t *effectCode;
			uint32_t effectCodeLength;
			FNA3D_Effect *retval;
		} createEffect;
		struct
		{
			FNA3D_Effect *cloneSource;
			FNA3D_Effect *retval;
		} cloneEffect;

		/* TODO Command Hell Part 2 */

		struct
		{
			FNA3D_Buffer *buffer;
			intptr_t retval;
		} getBufferSize;
		struct
		{
			FNA3D_Effect *effect;
			MOJOSHADER_effect *retval;
		} getEffectData;
	};
};

typedef struct ThreadedGLRenderer /* Cast FNA3D_Renderer* to this! */
{
	FNA3D_Device *actualDevice;

	GLThreadCommand *commands;
	SDL_mutex *commandsLock;
	SDL_sem *commandEvent;

	SDL_Thread *thread;
	uint8_t run;
} ThreadedGLRenderer;

typedef struct ThreadedGLBuffer /* Cast FNA3D_Buffer* to this! */
{
	ThreadedGLRenderer *parent;
	FNA3D_Buffer *actualBuffer;
} ThreadedGLBuffer;

typedef struct ThreadedGLEffect /* Cast FNA3D_Effect* to this! */
{
	ThreadedGLRenderer *parent;
	FNA3D_Effect *actualEffect;
} ThreadedGLEffect;

/* The Graphics Thread */

static int GLRenderThread(void* data)
{
	GLThreadCommand *cmd, *next;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) data;

	while (renderer->run)
	{
		SDL_SemWait(renderer->commandEvent);
		SDL_LockMutex(renderer->commandsLock);
		cmd = renderer->commands;
		while (cmd != NULL)
		{
			switch (cmd->type)
			{
			case COMMAND_CREATEDEVICE:
				renderer->actualDevice = ModernGLDriver.CreateDevice(
					cmd->createDevice.presentationParameters,
					cmd->createDevice.debugMode
				);
				break;
			/* TODO: Command Hell Part 3 */
			case COMMAND_GETBUFFERSIZE:
				cmd->getBufferSize.retval = renderer->actualDevice->GetBufferSize(
					cmd->getBufferSize.buffer
				);
				break;
			case COMMAND_GETEFFECTDATA:
				cmd->getEffectData.retval = renderer->actualDevice->GetEffectData(
					cmd->getEffectData.effect
				);
				break;
			default:
				FNA3D_LogError("Unknown GLCommand: %X\n", cmd->type);
				break;
			}
			next = cmd->next;
			SDL_SemPost(cmd->semaphore);
			cmd = next;
		}
		renderer->commands = NULL;
		SDL_UnlockMutex(renderer->commandsLock);
	}

	renderer->actualDevice->DestroyDevice(renderer->actualDevice);
	return 0;
}

static inline void ForceToRenderThread(
	ThreadedGLRenderer *renderer,
	GLThreadCommand *command
) {
	GLThreadCommand *curr;
	command->semaphore = SDL_CreateSemaphore(0);

	SDL_LockMutex(renderer->commandsLock);
	LinkedList_Add(renderer->commands, command, curr);
	SDL_UnlockMutex(renderer->commandsLock);

	SDL_SemWait(command->semaphore);
	SDL_DestroySemaphore(command->semaphore);
}

/* Quit */

static void THREADEDGL_DestroyDevice(FNA3D_Device *device)
{
	ThreadedGLRenderer* renderer = (ThreadedGLRenderer*) device->driverData;
	renderer->run = 0;
	SDL_WaitThread(renderer->thread, NULL);
	SDL_DestroyMutex(renderer->commandsLock);
	SDL_DestroySemaphore(renderer->commandEvent);
	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void THREADEDGL_BeginFrame(FNA3D_Renderer *driverData)
{
}

static void THREADEDGL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
}

static void THREADEDGL_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
}

/* Drawing */

static void THREADEDGL_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
}

static void THREADEDGL_DrawIndexedPrimitives(
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

static void THREADEDGL_DrawInstancedPrimitives(
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

static void THREADEDGL_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
}

static void THREADEDGL_DrawUserIndexedPrimitives(
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

static void THREADEDGL_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
}

/* Mutable Render States */

static void THREADEDGL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
}

static void THREADEDGL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
}

static void THREADEDGL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
}

static void THREADEDGL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
}

static int32_t THREADEDGL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	return 0;
}

static void THREADEDGL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
}

static int32_t THREADEDGL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	return 0;
}

static void THREADEDGL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
}

/* Immutable Render States */

static void THREADEDGL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
}

static void THREADEDGL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
}

static void THREADEDGL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
}

static void THREADEDGL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
}

/* Vertex State */

static void THREADEDGL_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
}

static void THREADEDGL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
}

/* Render Targets */

static void THREADEDGL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
}

static void THREADEDGL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
}

/* Backbuffer Functions */

static void THREADEDGL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
}

static void THREADEDGL_ReadBackbuffer(
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

static void THREADEDGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
}

static FNA3D_SurfaceFormat THREADEDGL_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	return FNA3D_SURFACEFORMAT_COLOR;
}

static FNA3D_DepthFormat THREADEDGL_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	return FNA3D_DEPTHFORMAT_NONE;
}

static int32_t THREADEDGL_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	return 0;
}

/* Textures */

static FNA3D_Texture* THREADEDGL_CreateTexture2D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	return NULL;
}

static FNA3D_Texture* THREADEDGL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	return NULL;
}

static FNA3D_Texture* THREADEDGL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	return NULL;
}

static void THREADEDGL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
}

static void THREADEDGL_SetTextureData2D(
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

static void THREADEDGL_SetTextureData3D(
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

static void THREADEDGL_SetTextureDataCube(
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

static void THREADEDGL_SetTextureDataYUV(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
) {
}

static void THREADEDGL_GetTextureData2D(
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

static void THREADEDGL_GetTextureData3D(
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

static void THREADEDGL_GetTextureDataCube(
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

static FNA3D_Renderbuffer* THREADEDGL_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
	return NULL;
}

static FNA3D_Renderbuffer* THREADEDGL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	return NULL;
}

static void THREADEDGL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
}

/* Vertex Buffers */

static FNA3D_Buffer* THREADEDGL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	return NULL;
}

static void THREADEDGL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
}

static void THREADEDGL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
}

static void THREADEDGL_GetVertexBufferData(
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

static FNA3D_Buffer* THREADEDGL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	return NULL;
}

static void THREADEDGL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
}

static void THREADEDGL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
}

static void THREADEDGL_GetIndexBufferData(
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

static FNA3D_Effect* THREADEDGL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
	return NULL;
}

static FNA3D_Effect* THREADEDGL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	return NULL;
}

static void THREADEDGL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
}

static void THREADEDGL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
}

static void THREADEDGL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
}

static void THREADEDGL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
}

/* Queries */

static FNA3D_Query* THREADEDGL_CreateQuery(FNA3D_Renderer *driverData)
{
	return NULL;
}

static void THREADEDGL_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
}

static void THREADEDGL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
}

static void THREADEDGL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
}

static uint8_t THREADEDGL_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	return 1;
}

static int32_t THREADEDGL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	return 0;
}

/* Feature Queries */

static uint8_t THREADEDGL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t THREADEDGL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t THREADEDGL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	return 0;
}

static uint8_t THREADEDGL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	return 0;
}

static int32_t THREADEDGL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	return 0;
}

static int32_t THREADEDGL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	return 0;
}

/* Debugging */

static void THREADEDGL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
}

/* Buffer Objects */

static intptr_t THREADEDGL_GetBufferSize(FNA3D_Buffer *buffer)
{
	GLThreadCommand cmd;
	ThreadedGLBuffer *buf = (ThreadedGLBuffer*) buffer;
	ThreadedGLRenderer *renderer = buf->parent;

	cmd.type = COMMAND_GETBUFFERSIZE;
	cmd.getBufferSize.buffer = buf->actualBuffer;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getBufferSize.retval;
}

/* Effect Objects */

static MOJOSHADER_effect* THREADEDGL_GetEffectData(FNA3D_Effect *effect)
{
	GLThreadCommand cmd;
	ThreadedGLEffect *eff = (ThreadedGLEffect*) effect;
	ThreadedGLRenderer *renderer = eff->parent;

	cmd.type = COMMAND_GETEFFECTDATA;
	cmd.getEffectData.effect = eff->actualEffect;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getEffectData.retval;
}

/* Driver */

static uint8_t THREADEDGL_PrepareWindowAttributes(uint32_t *flags)
{
	return ModernGLDriver.PrepareWindowAttributes(flags);
}

static void THREADEDGL_GetDrawableSize(void* window, int32_t *x, int32_t *y)
{
	ModernGLDriver.GetDrawableSize(window, x, y);
}

static FNA3D_Device* THREADEDGL_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	GLThreadCommand cmd;

	/* Initialize the Renderer first... */
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) SDL_malloc(
		sizeof(ThreadedGLRenderer)
	);
	renderer->run = 1;
	renderer->commands = NULL;
	renderer->commandsLock = SDL_CreateMutex();
	renderer->commandEvent = SDL_CreateSemaphore(0);

	/* Then allocate the end user's device... */
	FNA3D_Device *result = (FNA3D_Device*) SDL_malloc(sizeof(FNA3D_Device));
	result->driverData = (FNA3D_Renderer*) renderer;
	ASSIGN_DRIVER(THREADEDGL)

	/* ... then start the thread, finally. */
	renderer->thread = SDL_CreateThread(
		GLRenderThread,
		"GLRenderThread",
		renderer
	);

	/* The first command is always device creation! */
	cmd.type = COMMAND_CREATEDEVICE;
	cmd.createDevice.presentationParameters = presentationParameters;
	cmd.createDevice.debugMode = debugMode;
	ForceToRenderThread(renderer, &cmd);
	return result;
}

FNA3D_Driver ThreadedGLDriver = {
	"ThreadedGL",
	THREADEDGL_PrepareWindowAttributes,
	THREADEDGL_GetDrawableSize,
	THREADEDGL_CreateDevice
};

#endif /* FNA3D_DRIVER_THREADEDGL */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
