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
	#define COMMAND_SETEFFECTTECHNIQUE		57
	#define COMMAND_APPLYEFFECT			58
	#define COMMAND_BEGINPASSRESTORE		59
	#define COMMAND_ENDPASSRESTORE			60
	#define COMMAND_CREATEQUERY			61
	#define COMMAND_ADDDISPOSEQUERY			62
	#define COMMAND_QUERYBEGIN			63
	#define COMMAND_QUERYEND			64
	#define COMMAND_QUERYCOMPLETE			65
	#define COMMAND_QUERYPIXELCOUNT			66
	#define COMMAND_SUPPORTSDXT1			67
	#define COMMAND_SUPPORTSS3TC			68
	#define COMMAND_SUPPORTSHARDWAREINSTANCING	69
	#define COMMAND_SUPPORTSNOOVERWRITE		70
	#define COMMAND_GETMAXTEXTURESLOTS		71
	#define COMMAND_GETMAXMULTISAMPLECOUNT		72
	#define COMMAND_SETSTRINGMARKER			73

	FNA3DNAMELESS union
	{
		struct
		{
			FNA3D_PresentationParameters *presentationParameters;
			uint8_t debugMode;
		} createDevice;
		/* Nothing to store for BeginFrame */
		struct
		{
			FNA3D_Rect *sourceRectangle;
			FNA3D_Rect *destinationRectangle;
			void* overrideWindowHandle;
		} swapBuffers;
		struct
		{
			FNA3D_PresentInterval presentInterval;
		} setPresentationInterval;
		struct
		{
			FNA3D_ClearOptions options;
			FNA3D_Vec4 *color;
			float depth;
			int32_t stencil;
		} clear;
		struct
		{
			FNA3D_PrimitiveType primitiveType;
			int32_t baseVertex;
			int32_t minVertexIndex;
			int32_t numVertices;
			int32_t startIndex;
			int32_t primitiveCount;
			FNA3D_Buffer *indices;
			FNA3D_IndexElementSize indexElementSize;
		} drawIndexedPrimitives;
		struct
		{
			FNA3D_PrimitiveType primitiveType;
			int32_t baseVertex;
			int32_t minVertexIndex;
			int32_t numVertices;
			int32_t startIndex;
			int32_t primitiveCount;
			int32_t instanceCount;
			FNA3D_Buffer *indices;
			FNA3D_IndexElementSize indexElementSize;
		} drawInstancedPrimitives;
		struct
		{
			FNA3D_PrimitiveType primitiveType;
			int32_t vertexStart;
			int32_t primitiveCount;
		} drawPrimitives;
		struct
		{
			FNA3D_PrimitiveType primitiveType;
			void* vertexData;
			int32_t vertexOffset;
			int32_t numVertices;
			void* indexData;
			int32_t indexOffset;
			FNA3D_IndexElementSize indexElementSize;
			int32_t primitiveCount;
		} drawUserIndexedPrimitives;
		struct
		{
			FNA3D_PrimitiveType primitiveType;
			void* vertexData;
			int32_t vertexOffset;
			int32_t primitiveCount;
		} drawUserPrimitives;
		struct
		{
			FNA3D_Viewport *viewport;
		} setViewport;
		struct
		{
			FNA3D_Rect *scissor;
		} setScissorRect;
		struct
		{
			FNA3D_Color *blendFactor;
		} getBlendFactor;
		struct
		{
			FNA3D_Color *blendFactor;
		} setBlendFactor;
		struct
		{
			int32_t retval;
		} getMultiSampleMask;
		struct
		{
			int32_t mask;
		} setMultiSampleMask;
		struct
		{
			int32_t retval;
		} getReferenceStencil;
		struct
		{
			int32_t ref;
		} setReferenceStencil;
		struct
		{
			FNA3D_BlendState *blendState;
		} setBlendState;
		struct
		{
			FNA3D_DepthStencilState *depthStencilState;
		} setDepthStencilState;
		struct
		{
			FNA3D_RasterizerState *rasterizerState;
		} applyRasterizerState;
		struct
		{
			int32_t index;
			FNA3D_Texture *texture;
			FNA3D_SamplerState *sampler;
		} verifySampler;
		struct
		{
			FNA3D_VertexBufferBinding *bindings;
			int32_t numBindings;
			uint8_t bindingsUpdated;
			int32_t baseVertex;
		} applyVertexBufferBindings;
		struct
		{
			FNA3D_VertexDeclaration *vertexDeclaration;
			void* ptr;
			int32_t vertexOffset;
		} applyVertexDeclaration;
		struct
		{
			FNA3D_RenderTargetBinding *renderTargets;
			int32_t numRenderTargets;
			FNA3D_Renderbuffer *renderbuffer;
			FNA3D_DepthFormat depthFormat;
		} setRenderTargets;
		struct
		{
			FNA3D_RenderTargetBinding *target;
		} resolveTarget;
		struct
		{
			FNA3D_PresentationParameters *presentationParameters;
		} resetBackbuffer;
		struct
		{
			void* data;
			int32_t dataLen;
			int32_t startIndex;
			int32_t elementCount;
			int32_t elementSizeInBytes;
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
		} readBackbuffer;
		struct
		{
			int32_t *w;
			int32_t *h;
		} getBackbufferSize;
		struct
		{
			FNA3D_SurfaceFormat retval;
		} getBackbufferSurfaceFormat;
		struct
		{
			FNA3D_DepthFormat retval;
		} getBackbufferDepthFormat;
		struct
		{
			int32_t retval;
		} getBackbufferMultiSampleCount;
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
		struct
		{
			FNA3D_Texture *texture;
		} addDisposeTexture;
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
			FNA3D_Texture *y;
			FNA3D_Texture *u;
			FNA3D_Texture *v;
			int32_t w;
			int32_t h;
			void* ptr;
		} setTextureDataYUV;
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
		struct
		{
			FNA3D_Renderbuffer *renderbuffer;
		} addDisposeRenderbuffer;
		struct
		{
			uint8_t dynamic;
			FNA3D_BufferUsage usage;
			int32_t vertexCount;
			int32_t vertexStride;
			FNA3D_Buffer *retval;
		} genVertexBuffer;
		struct
		{
			FNA3D_Buffer *buffer;
		} addDisposeVertexBuffer;
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
		struct
		{
			FNA3D_Buffer *buffer;
		} addDisposeIndexBuffer;
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
			FNA3D_Effect **effect;
			MOJOSHADER_effect **effectData;
		} createEffect;
		struct
		{
			FNA3D_Effect *cloneSource;
			FNA3D_Effect **effect;
			MOJOSHADER_effect **effectData;
		} cloneEffect;
		struct
		{
			FNA3D_Effect *effect;
		} addDisposeEffect;
		struct
		{
			FNA3D_Effect *effect;
			MOJOSHADER_effectTechnique *technique;
		} setEffectTechnique;
		struct
		{
			FNA3D_Effect *effect;
			uint32_t pass;
			MOJOSHADER_effectStateChanges *stateChanges;
		} applyEffect;
		struct
		{
			FNA3D_Effect *effect;
			MOJOSHADER_effectStateChanges *stateChanges;
		} beginPassRestore;
		struct
		{
			FNA3D_Effect *effect;
		} endPassRestore;
		struct
		{
			FNA3D_Query* retval;
		} createQuery;
		struct
		{
			FNA3D_Query *query;
		} addDisposeQuery;
		struct
		{
			FNA3D_Query *query;
		} queryBegin;
		struct
		{
			FNA3D_Query *query;
		} queryEnd;
		struct
		{
			FNA3D_Query *query;
			uint8_t retval;
		} queryComplete;
		struct
		{
			FNA3D_Query *query;
			int32_t retval;
		} queryPixelCount;
		struct
		{
			uint8_t retval;
		} supportsDXT1;
		struct
		{
			uint8_t retval;
		} supportsS3TC;
		struct
		{
			uint8_t retval;
		} supportsHardwareInstancing;
		struct
		{
			uint8_t retval;
		} supportsNoOverwrite;
		struct
		{
			int32_t retval;
		} getMaxTextureSlots;
		struct
		{
			int32_t retval;
		} getMaxMultiSampleCount;
		struct
		{
			const char *text;
		} setStringMarker;
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

/* The Graphics Thread */

static int GLRenderThread(void* data)
{
	GLThreadCommand *cmd, *next;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) data;

	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

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
			case COMMAND_BEGINFRAME:
				renderer->actualDevice->BeginFrame(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SWAPBUFFERS:
				renderer->actualDevice->SwapBuffers(
					renderer->actualDevice->driverData,
					cmd->swapBuffers.sourceRectangle,
					cmd->swapBuffers.destinationRectangle,
					cmd->swapBuffers.overrideWindowHandle
				);
				break;
			case COMMAND_SETPRESENTATIONINTERVAL:
				renderer->actualDevice->SetPresentationInterval(
					renderer->actualDevice->driverData,
					cmd->setPresentationInterval.presentInterval
				);
				break;
			case COMMAND_CLEAR:
				renderer->actualDevice->Clear(
					renderer->actualDevice->driverData,
					cmd->clear.options,
					cmd->clear.color,
					cmd->clear.depth,
					cmd->clear.stencil
				);
				break;
			case COMMAND_DRAWINDEXEDPRIMITIVES:
				renderer->actualDevice->DrawIndexedPrimitives(
					renderer->actualDevice->driverData,
					cmd->drawIndexedPrimitives.primitiveType,
					cmd->drawIndexedPrimitives.baseVertex,
					cmd->drawIndexedPrimitives.minVertexIndex,
					cmd->drawIndexedPrimitives.numVertices,
					cmd->drawIndexedPrimitives.startIndex,
					cmd->drawIndexedPrimitives.primitiveCount,
					cmd->drawIndexedPrimitives.indices,
					cmd->drawIndexedPrimitives.indexElementSize
				);
				break;
			case COMMAND_DRAWINSTANCEDPRIMITIVES:
				renderer->actualDevice->DrawInstancedPrimitives(
					renderer->actualDevice->driverData,
					cmd->drawInstancedPrimitives.primitiveType,
					cmd->drawInstancedPrimitives.baseVertex,
					cmd->drawInstancedPrimitives.minVertexIndex,
					cmd->drawInstancedPrimitives.numVertices,
					cmd->drawInstancedPrimitives.startIndex,
					cmd->drawInstancedPrimitives.primitiveCount,
					cmd->drawInstancedPrimitives.instanceCount,
					cmd->drawInstancedPrimitives.indices,
					cmd->drawInstancedPrimitives.indexElementSize
				);
				break;
			case COMMAND_DRAWPRIMITIVES:
				renderer->actualDevice->DrawPrimitives(
					renderer->actualDevice->driverData,
					cmd->drawPrimitives.primitiveType,
					cmd->drawPrimitives.vertexStart,
					cmd->drawPrimitives.primitiveCount
				);
				break;
			case COMMAND_DRAWUSERINDEXEDPRIMITIVES:
				renderer->actualDevice->DrawUserIndexedPrimitives(
					renderer->actualDevice->driverData,
					cmd->drawUserIndexedPrimitives.primitiveType,
					cmd->drawUserIndexedPrimitives.vertexData,
					cmd->drawUserIndexedPrimitives.vertexOffset,
					cmd->drawUserIndexedPrimitives.numVertices,
					cmd->drawUserIndexedPrimitives.indexData,
					cmd->drawUserIndexedPrimitives.indexOffset,
					cmd->drawUserIndexedPrimitives.indexElementSize,
					cmd->drawUserIndexedPrimitives.primitiveCount
				);
				break;
			case COMMAND_DRAWUSERPRIMITIVES:
				renderer->actualDevice->DrawUserPrimitives(
					renderer->actualDevice->driverData,
					cmd->drawUserPrimitives.primitiveType,
					cmd->drawUserPrimitives.vertexData,
					cmd->drawUserPrimitives.vertexOffset,
					cmd->drawUserPrimitives.primitiveCount
				);
				break;
			case COMMAND_SETVIEWPORT:
				renderer->actualDevice->SetViewport(
					renderer->actualDevice->driverData,
					cmd->setViewport.viewport
				);
				break;
			case COMMAND_SETSCISSORRECT:
				renderer->actualDevice->SetScissorRect(
					renderer->actualDevice->driverData,
					cmd->setScissorRect.scissor
				);
				break;
			case COMMAND_GETBLENDFACTOR:
				renderer->actualDevice->GetBlendFactor(
					renderer->actualDevice->driverData,
					cmd->getBlendFactor.blendFactor
				);
				break;
			case COMMAND_SETBLENDFACTOR:
				renderer->actualDevice->SetBlendFactor(
					renderer->actualDevice->driverData,
					cmd->setBlendFactor.blendFactor
				);
				break;
			case COMMAND_GETMULTISAMPLEMASK:
				cmd->getMultiSampleMask.retval = renderer->actualDevice->GetMultiSampleMask(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SETMULTISAMPLEMASK:
				renderer->actualDevice->SetMultiSampleMask(
					renderer->actualDevice->driverData,
					cmd->setMultiSampleMask.mask
				);
				break;
			case COMMAND_GETREFERENCESTENCIL:
				cmd->getReferenceStencil.retval = renderer->actualDevice->GetReferenceStencil(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SETREFERENCESTENCIL:
				renderer->actualDevice->SetReferenceStencil(
					renderer->actualDevice->driverData,
					cmd->setReferenceStencil.ref
				);
				break;
			case COMMAND_SETBLENDSTATE:
				renderer->actualDevice->SetBlendState(
					renderer->actualDevice->driverData,
					cmd->setBlendState.blendState
				);
				break;
			case COMMAND_SETDEPTHSTENCILSTATE:
				renderer->actualDevice->SetDepthStencilState(
					renderer->actualDevice->driverData,
					cmd->setDepthStencilState.depthStencilState
				);
				break;
			case COMMAND_APPLYRASTERIZERSTATE:
				renderer->actualDevice->ApplyRasterizerState(
					renderer->actualDevice->driverData,
					cmd->applyRasterizerState.rasterizerState
				);
				break;
			case COMMAND_VERIFYSAMPLER:
				renderer->actualDevice->VerifySampler(
					renderer->actualDevice->driverData,
					cmd->verifySampler.index,
					cmd->verifySampler.texture,
					cmd->verifySampler.sampler
				);
				break;
			case COMMAND_APPLYVERTEXBUFFERBINDINGS:
				renderer->actualDevice->ApplyVertexBufferBindings(
					renderer->actualDevice->driverData,
					cmd->applyVertexBufferBindings.bindings,
					cmd->applyVertexBufferBindings.numBindings,
					cmd->applyVertexBufferBindings.bindingsUpdated,
					cmd->applyVertexBufferBindings.baseVertex
				);
				break;
			case COMMAND_APPLYVERTEXDECLARATION:
				renderer->actualDevice->ApplyVertexDeclaration(
					renderer->actualDevice->driverData,
					cmd->applyVertexDeclaration.vertexDeclaration,
					cmd->applyVertexDeclaration.ptr,
					cmd->applyVertexDeclaration.vertexOffset
				);
				break;
			case COMMAND_SETRENDERTARGETS:
				renderer->actualDevice->SetRenderTargets(
					renderer->actualDevice->driverData,
					cmd->setRenderTargets.renderTargets,
					cmd->setRenderTargets.numRenderTargets,
					cmd->setRenderTargets.renderbuffer,
					cmd->setRenderTargets.depthFormat
				);
				break;
			case COMMAND_RESOLVETARGET:
				renderer->actualDevice->ResolveTarget(
					renderer->actualDevice->driverData,
					cmd->resolveTarget.target
				);
				break;
			case COMMAND_RESETBACKBUFFER:
				renderer->actualDevice->ResetBackbuffer(
					renderer->actualDevice->driverData,
					cmd->resetBackbuffer.presentationParameters
				);
				break;
			case COMMAND_READBACKBUFFER:
				renderer->actualDevice->ReadBackbuffer(
					renderer->actualDevice->driverData,
					cmd->readBackbuffer.data,
					cmd->readBackbuffer.dataLen,
					cmd->readBackbuffer.startIndex,
					cmd->readBackbuffer.elementCount,
					cmd->readBackbuffer.elementSizeInBytes,
					cmd->readBackbuffer.x,
					cmd->readBackbuffer.y,
					cmd->readBackbuffer.w,
					cmd->readBackbuffer.h
				);
				break;
			case COMMAND_GETBACKBUFFERSIZE:
				renderer->actualDevice->GetBackbufferSize(
					renderer->actualDevice->driverData,
					cmd->getBackbufferSize.w,
					cmd->getBackbufferSize.h
				);
				break;
			case COMMAND_GETBACKBUFFERSURFACEFORMAT:
				cmd->getBackbufferSurfaceFormat.retval = renderer->actualDevice->GetBackbufferSurfaceFormat(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_GETBACKBUFFERDEPTHFORMAT:
				cmd->getBackbufferDepthFormat.retval = renderer->actualDevice->GetBackbufferDepthFormat(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_GETBACKBUFFERMULTISAMPLECOUNT:
				cmd->getBackbufferMultiSampleCount.retval = renderer->actualDevice->GetBackbufferMultiSampleCount(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_CREATETEXTURE2D:
				cmd->createTexture2D.retval = renderer->actualDevice->CreateTexture2D(
					renderer->actualDevice->driverData,
					cmd->createTexture2D.format,
					cmd->createTexture2D.width,
					cmd->createTexture2D.height,
					cmd->createTexture2D.levelCount,
					cmd->createTexture2D.isRenderTarget
				);
				break;
			case COMMAND_CREATETEXTURE3D:
				cmd->createTexture3D.retval = renderer->actualDevice->CreateTexture3D(
					renderer->actualDevice->driverData,
					cmd->createTexture3D.format,
					cmd->createTexture3D.width,
					cmd->createTexture3D.height,
					cmd->createTexture3D.depth,
					cmd->createTexture3D.levelCount
				);
				break;
			case COMMAND_CREATETEXTURECUBE:
				cmd->createTextureCube.retval = renderer->actualDevice->CreateTextureCube(
					renderer->actualDevice->driverData,
					cmd->createTextureCube.format,
					cmd->createTextureCube.size,
					cmd->createTextureCube.levelCount,
					cmd->createTextureCube.isRenderTarget
				);
				break;
			case COMMAND_ADDDISPOSETEXTURE:
				renderer->actualDevice->AddDisposeTexture(
					renderer->actualDevice->driverData,
					cmd->addDisposeTexture.texture
				);
				break;
			case COMMAND_SETTEXTUREDATA2D:
				renderer->actualDevice->SetTextureData2D(
					renderer->actualDevice->driverData,
					cmd->setTextureData2D.texture,
					cmd->setTextureData2D.format,
					cmd->setTextureData2D.x,
					cmd->setTextureData2D.y,
					cmd->setTextureData2D.w,
					cmd->setTextureData2D.h,
					cmd->setTextureData2D.level,
					cmd->setTextureData2D.data,
					cmd->setTextureData2D.dataLength
				);
				break;
			case COMMAND_SETTEXTUREDATA3D:
				renderer->actualDevice->SetTextureData3D(
					renderer->actualDevice->driverData,
					cmd->setTextureData3D.texture,
					cmd->setTextureData3D.format,
					cmd->setTextureData3D.level,
					cmd->setTextureData3D.left,
					cmd->setTextureData3D.top,
					cmd->setTextureData3D.right,
					cmd->setTextureData3D.bottom,
					cmd->setTextureData3D.front,
					cmd->setTextureData3D.back,
					cmd->setTextureData3D.data,
					cmd->setTextureData3D.dataLength
				);
				break;
			case COMMAND_SETTEXTUREDATACUBE:
				renderer->actualDevice->SetTextureDataCube(
					renderer->actualDevice->driverData,
					cmd->setTextureDataCube.texture,
					cmd->setTextureDataCube.format,
					cmd->setTextureDataCube.x,
					cmd->setTextureDataCube.y,
					cmd->setTextureDataCube.w,
					cmd->setTextureDataCube.h,
					cmd->setTextureDataCube.cubeMapFace,
					cmd->setTextureDataCube.level,
					cmd->setTextureDataCube.data,
					cmd->setTextureDataCube.dataLength
				);
				break;
			case COMMAND_SETTEXTUREDATAYUV:
				renderer->actualDevice->SetTextureDataYUV(
					renderer->actualDevice->driverData,
					cmd->setTextureDataYUV.y,
					cmd->setTextureDataYUV.u,
					cmd->setTextureDataYUV.v,
					cmd->setTextureDataYUV.w,
					cmd->setTextureDataYUV.h,
					cmd->setTextureDataYUV.ptr
				);
				break;
			case COMMAND_GETTEXTUREDATA2D:
				renderer->actualDevice->GetTextureData2D(
					renderer->actualDevice->driverData,
					cmd->getTextureData2D.texture,
					cmd->getTextureData2D.format,
					cmd->getTextureData2D.textureWidth,
					cmd->getTextureData2D.textureHeight,
					cmd->getTextureData2D.level,
					cmd->getTextureData2D.x,
					cmd->getTextureData2D.y,
					cmd->getTextureData2D.w,
					cmd->getTextureData2D.h,
					cmd->getTextureData2D.data,
					cmd->getTextureData2D.startIndex,
					cmd->getTextureData2D.elementCount,
					cmd->getTextureData2D.elementSizeInBytes
				);
				break;
			case COMMAND_GETTEXTUREDATA3D:
				renderer->actualDevice->GetTextureData3D(
					renderer->actualDevice->driverData,
					cmd->getTextureData3D.texture,
					cmd->getTextureData3D.format,
					cmd->getTextureData3D.left,
					cmd->getTextureData3D.top,
					cmd->getTextureData3D.front,
					cmd->getTextureData3D.right,
					cmd->getTextureData3D.bottom,
					cmd->getTextureData3D.back,
					cmd->getTextureData3D.level,
					cmd->getTextureData3D.data,
					cmd->getTextureData3D.startIndex,
					cmd->getTextureData3D.elementCount,
					cmd->getTextureData3D.elementSizeInBytes
				);
				break;
			case COMMAND_GETTEXTUREDATACUBE:
				renderer->actualDevice->GetTextureDataCube(
					renderer->actualDevice->driverData,
					cmd->getTextureDataCube.texture,
					cmd->getTextureDataCube.format,
					cmd->getTextureDataCube.textureSize,
					cmd->getTextureDataCube.cubeMapFace,
					cmd->getTextureDataCube.level,
					cmd->getTextureDataCube.x,
					cmd->getTextureDataCube.y,
					cmd->getTextureDataCube.w,
					cmd->getTextureDataCube.h,
					cmd->getTextureDataCube.data,
					cmd->getTextureDataCube.startIndex,
					cmd->getTextureDataCube.elementCount,
					cmd->getTextureDataCube.elementSizeInBytes
				);
				break;
			case COMMAND_GENCOLORRENDERBUFFER:
				cmd->genColorRenderbuffer.retval = renderer->actualDevice->GenColorRenderbuffer(
					renderer->actualDevice->driverData,
					cmd->genColorRenderbuffer.width,
					cmd->genColorRenderbuffer.height,
					cmd->genColorRenderbuffer.format,
					cmd->genColorRenderbuffer.multiSampleCount,
					cmd->genColorRenderbuffer.texture
				);
				break;
			case COMMAND_GENDEPTHSTENCILRENDERBUFFER:
				cmd->genDepthStencilRenderbuffer.retval = renderer->actualDevice->GenDepthStencilRenderbuffer(
					renderer->actualDevice->driverData,
					cmd->genDepthStencilRenderbuffer.width,
					cmd->genDepthStencilRenderbuffer.height,
					cmd->genDepthStencilRenderbuffer.format,
					cmd->genDepthStencilRenderbuffer.multiSampleCount
				);
				break;
			case COMMAND_ADDDISPOSERENDERBUFFER:
				renderer->actualDevice->AddDisposeRenderbuffer(
					renderer->actualDevice->driverData,
					cmd->addDisposeRenderbuffer.renderbuffer
				);
				break;
			case COMMAND_GENVERTEXBUFFER:
				cmd->genVertexBuffer.retval = renderer->actualDevice->GenVertexBuffer(
					renderer->actualDevice->driverData,
					cmd->genVertexBuffer.dynamic,
					cmd->genVertexBuffer.usage,
					cmd->genVertexBuffer.vertexCount,
					cmd->genVertexBuffer.vertexStride
				);
				break;
			case COMMAND_ADDDISPOSEVERTEXBUFFER:
				renderer->actualDevice->AddDisposeVertexBuffer(
					renderer->actualDevice->driverData,
					cmd->addDisposeVertexBuffer.buffer
				);
				break;
			case COMMAND_SETVERTEXBUFFERDATA:
				renderer->actualDevice->SetVertexBufferData(
					renderer->actualDevice->driverData,
					cmd->setVertexBufferData.buffer,
					cmd->setVertexBufferData.offsetInBytes,
					cmd->setVertexBufferData.data,
					cmd->setVertexBufferData.dataLength,
					cmd->setVertexBufferData.options
				);
				break;
			case COMMAND_GETVERTEXBUFFERDATA:
				renderer->actualDevice->GetVertexBufferData(
					renderer->actualDevice->driverData,
					cmd->getVertexBufferData.buffer,
					cmd->getVertexBufferData.offsetInBytes,
					cmd->getVertexBufferData.data,
					cmd->getVertexBufferData.startIndex,
					cmd->getVertexBufferData.elementCount,
					cmd->getVertexBufferData.elementSizeInBytes,
					cmd->getVertexBufferData.vertexStride
				);
				break;
			case COMMAND_GENINDEXBUFFER:
				cmd->genIndexBuffer.retval = renderer->actualDevice->GenIndexBuffer(
					renderer->actualDevice->driverData,
					cmd->genIndexBuffer.dynamic,
					cmd->genIndexBuffer.usage,
					cmd->genIndexBuffer.indexCount,
					cmd->genIndexBuffer.indexElementSize
				);
				break;
			case COMMAND_ADDDISPOSEINDEXBUFFER:
				renderer->actualDevice->AddDisposeIndexBuffer(
					renderer->actualDevice->driverData,
					cmd->addDisposeIndexBuffer.buffer
				);
				break;
			case COMMAND_SETINDEXBUFFERDATA:
				renderer->actualDevice->SetIndexBufferData(
					renderer->actualDevice->driverData,
					cmd->setIndexBufferData.buffer,
					cmd->setIndexBufferData.offsetInBytes,
					cmd->setIndexBufferData.data,
					cmd->setIndexBufferData.dataLength,
					cmd->setIndexBufferData.options
				);
				break;
			case COMMAND_GETINDEXBUFFERDATA:
				renderer->actualDevice->GetIndexBufferData(
					renderer->actualDevice->driverData,
					cmd->getIndexBufferData.buffer,
					cmd->getIndexBufferData.offsetInBytes,
					cmd->getIndexBufferData.data,
					cmd->getIndexBufferData.startIndex,
					cmd->getIndexBufferData.elementCount,
					cmd->getIndexBufferData.elementSizeInBytes
				);
				break;
			case COMMAND_CREATEEFFECT:
				renderer->actualDevice->CreateEffect(
					renderer->actualDevice->driverData,
					cmd->createEffect.effectCode,
					cmd->createEffect.effectCodeLength,
					cmd->createEffect.effect,
					cmd->createEffect.effectData
				);
				break;
			case COMMAND_CLONEEFFECT:
				renderer->actualDevice->CloneEffect(
					renderer->actualDevice->driverData,
					cmd->cloneEffect.cloneSource,
					cmd->cloneEffect.effect,
					cmd->cloneEffect.effectData
				);
				break;
			case COMMAND_ADDDISPOSEEFFECT:
				renderer->actualDevice->AddDisposeEffect(
					renderer->actualDevice->driverData,
					cmd->addDisposeEffect.effect
				);
				break;
			case COMMAND_SETEFFECTTECHNIQUE:
				renderer->actualDevice->SetEffectTechnique(
					renderer->actualDevice->driverData,
					cmd->setEffectTechnique.effect,
					cmd->setEffectTechnique.technique
				);
				break;
			case COMMAND_APPLYEFFECT:
				renderer->actualDevice->ApplyEffect(
					renderer->actualDevice->driverData,
					cmd->applyEffect.effect,
					cmd->applyEffect.pass,
					cmd->applyEffect.stateChanges
				);
				break;
			case COMMAND_BEGINPASSRESTORE:
				renderer->actualDevice->BeginPassRestore(
					renderer->actualDevice->driverData,
					cmd->beginPassRestore.effect,
					cmd->beginPassRestore.stateChanges
				);
				break;
			case COMMAND_ENDPASSRESTORE:
				renderer->actualDevice->EndPassRestore(
					renderer->actualDevice->driverData,
					cmd->endPassRestore.effect
				);
				break;
			case COMMAND_CREATEQUERY:
				cmd->createQuery.retval = renderer->actualDevice->CreateQuery(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_ADDDISPOSEQUERY:
				renderer->actualDevice->AddDisposeQuery(
					renderer->actualDevice->driverData,
					cmd->addDisposeQuery.query
				);
				break;
			case COMMAND_QUERYBEGIN:
				renderer->actualDevice->QueryBegin(
					renderer->actualDevice->driverData,
					cmd->queryBegin.query
				);
				break;
			case COMMAND_QUERYEND:
				renderer->actualDevice->QueryEnd(
					renderer->actualDevice->driverData,
					cmd->queryEnd.query
				);
				break;
			case COMMAND_QUERYCOMPLETE:
				cmd->queryComplete.retval = renderer->actualDevice->QueryComplete(
					renderer->actualDevice->driverData,
					cmd->queryComplete.query
				);
				break;
			case COMMAND_QUERYPIXELCOUNT:
				cmd->queryPixelCount.retval = renderer->actualDevice->QueryPixelCount(
					renderer->actualDevice->driverData,
					cmd->queryPixelCount.query
				);
				break;
			case COMMAND_SUPPORTSDXT1:
				cmd->supportsDXT1.retval = renderer->actualDevice->SupportsDXT1(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SUPPORTSS3TC:
				cmd->supportsS3TC.retval = renderer->actualDevice->SupportsS3TC(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SUPPORTSHARDWAREINSTANCING:
				cmd->supportsHardwareInstancing.retval = renderer->actualDevice->SupportsHardwareInstancing(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SUPPORTSNOOVERWRITE:
				cmd->supportsNoOverwrite.retval = renderer->actualDevice->SupportsNoOverwrite(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_GETMAXTEXTURESLOTS:
				cmd->getMaxTextureSlots.retval = renderer->actualDevice->GetMaxTextureSlots(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_GETMAXMULTISAMPLECOUNT:
				cmd->getMaxMultiSampleCount.retval = renderer->actualDevice->GetMaxMultiSampleCount(
					renderer->actualDevice->driverData
				);
				break;
			case COMMAND_SETSTRINGMARKER:
				renderer->actualDevice->SetStringMarker(
					renderer->actualDevice->driverData,
					cmd->setStringMarker.text
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

	SDL_SemPost(renderer->commandEvent);

	SDL_SemWait(command->semaphore);
	SDL_DestroySemaphore(command->semaphore);
}

/* Quit */

static void THREADEDGL_DestroyDevice(FNA3D_Device *device)
{
	ThreadedGLRenderer* renderer = (ThreadedGLRenderer*) device->driverData;
	renderer->run = 0;
	SDL_SemPost(renderer->commandEvent);
	SDL_WaitThread(renderer->thread, NULL);
	SDL_DestroyMutex(renderer->commandsLock);
	SDL_DestroySemaphore(renderer->commandEvent);
	SDL_free(renderer);
	SDL_free(device);
}

/* Begin/End Frame */

static void THREADEDGL_BeginFrame(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_BEGINFRAME;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SwapBuffers(
	FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SWAPBUFFERS;
	cmd.swapBuffers.sourceRectangle = sourceRectangle;
	cmd.swapBuffers.destinationRectangle = destinationRectangle;
	cmd.swapBuffers.overrideWindowHandle = overrideWindowHandle;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetPresentationInterval(
	FNA3D_Renderer *driverData,
	FNA3D_PresentInterval presentInterval
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETPRESENTATIONINTERVAL;
	cmd.setPresentationInterval.presentInterval = presentInterval;
	ForceToRenderThread(renderer, &cmd);
}

/* Drawing */

static void THREADEDGL_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CLEAR;
	cmd.clear.options = options;
	cmd.clear.color = color;
	cmd.clear.depth = depth;
	cmd.clear.stencil = stencil;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_DRAWINDEXEDPRIMITIVES;
	cmd.drawIndexedPrimitives.primitiveType = primitiveType;
	cmd.drawIndexedPrimitives.baseVertex = baseVertex;
	cmd.drawIndexedPrimitives.minVertexIndex = minVertexIndex;
	cmd.drawIndexedPrimitives.numVertices = numVertices;
	cmd.drawIndexedPrimitives.startIndex = startIndex;
	cmd.drawIndexedPrimitives.primitiveCount = primitiveCount;
	cmd.drawIndexedPrimitives.indices = indices;
	cmd.drawIndexedPrimitives.indexElementSize = indexElementSize;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_DRAWINSTANCEDPRIMITIVES;
	cmd.drawInstancedPrimitives.primitiveType = primitiveType;
	cmd.drawInstancedPrimitives.baseVertex = baseVertex;
	cmd.drawInstancedPrimitives.minVertexIndex = minVertexIndex;
	cmd.drawInstancedPrimitives.numVertices = numVertices;
	cmd.drawInstancedPrimitives.startIndex = startIndex;
	cmd.drawInstancedPrimitives.primitiveCount = primitiveCount;
	cmd.drawInstancedPrimitives.instanceCount = instanceCount;
	cmd.drawInstancedPrimitives.indices = indices;
	cmd.drawInstancedPrimitives.indexElementSize = indexElementSize;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_DrawPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_DRAWPRIMITIVES;
	cmd.drawPrimitives.primitiveType = primitiveType;
	cmd.drawPrimitives.vertexStart = vertexStart;
	cmd.drawPrimitives.primitiveCount = primitiveCount;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_DRAWUSERINDEXEDPRIMITIVES;
	cmd.drawUserIndexedPrimitives.primitiveType = primitiveType;
	cmd.drawUserIndexedPrimitives.vertexData = vertexData;
	cmd.drawUserIndexedPrimitives.vertexOffset = vertexOffset;
	cmd.drawUserIndexedPrimitives.numVertices = numVertices;
	cmd.drawUserIndexedPrimitives.indexData = indexData;
	cmd.drawUserIndexedPrimitives.indexOffset = indexOffset;
	cmd.drawUserIndexedPrimitives.indexElementSize = indexElementSize;
	cmd.drawUserIndexedPrimitives.primitiveCount = primitiveCount;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_DrawUserPrimitives(
	FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	void* vertexData,
	int32_t vertexOffset,
	int32_t primitiveCount
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_DRAWUSERPRIMITIVES;
	cmd.drawUserPrimitives.primitiveType = primitiveType;
	cmd.drawUserPrimitives.vertexData = vertexData;
	cmd.drawUserPrimitives.vertexOffset = vertexOffset;
	cmd.drawUserPrimitives.primitiveCount = primitiveCount;
	ForceToRenderThread(renderer, &cmd);
}

/* Mutable Render States */

static void THREADEDGL_SetViewport(FNA3D_Renderer *driverData, FNA3D_Viewport *viewport)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETVIEWPORT;
	cmd.setViewport.viewport = viewport;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetScissorRect(FNA3D_Renderer *driverData, FNA3D_Rect *scissor)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETSCISSORRECT;
	cmd.setScissorRect.scissor = scissor;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETBLENDFACTOR;
	cmd.getBlendFactor.blendFactor = blendFactor;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETBLENDFACTOR;
	cmd.setBlendFactor.blendFactor = blendFactor;
	ForceToRenderThread(renderer, &cmd);
}

static int32_t THREADEDGL_GetMultiSampleMask(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETMULTISAMPLEMASK;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getMultiSampleMask.retval;
}

static void THREADEDGL_SetMultiSampleMask(FNA3D_Renderer *driverData, int32_t mask)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETMULTISAMPLEMASK;
	cmd.setMultiSampleMask.mask = mask;
	ForceToRenderThread(renderer, &cmd);
}

static int32_t THREADEDGL_GetReferenceStencil(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETREFERENCESTENCIL;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getReferenceStencil.retval;
}

static void THREADEDGL_SetReferenceStencil(FNA3D_Renderer *driverData, int32_t ref)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETREFERENCESTENCIL;
	cmd.setReferenceStencil.ref = ref;
	ForceToRenderThread(renderer, &cmd);
}

/* Immutable Render States */

static void THREADEDGL_SetBlendState(
	FNA3D_Renderer *driverData,
	FNA3D_BlendState *blendState
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETBLENDSTATE;
	cmd.setBlendState.blendState = blendState;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETDEPTHSTENCILSTATE;
	cmd.setDepthStencilState.depthStencilState = depthStencilState;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_ApplyRasterizerState(
	FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_APPLYRASTERIZERSTATE;
	cmd.applyRasterizerState.rasterizerState = rasterizerState;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_VerifySampler(
	FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_VERIFYSAMPLER;
	cmd.verifySampler.index = index;
	cmd.verifySampler.texture = texture;
	cmd.verifySampler.sampler = sampler;
	ForceToRenderThread(renderer, &cmd);
}

/* Vertex State */

static void THREADEDGL_ApplyVertexBufferBindings(
	FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_APPLYVERTEXBUFFERBINDINGS;
	cmd.applyVertexBufferBindings.bindings = bindings;
	cmd.applyVertexBufferBindings.numBindings = numBindings;
	cmd.applyVertexBufferBindings.bindingsUpdated = bindingsUpdated;
	cmd.applyVertexBufferBindings.baseVertex = baseVertex;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_ApplyVertexDeclaration(
	FNA3D_Renderer *driverData,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_APPLYVERTEXDECLARATION;
	cmd.applyVertexDeclaration.vertexDeclaration = vertexDeclaration;
	cmd.applyVertexDeclaration.ptr = ptr;
	cmd.applyVertexDeclaration.vertexOffset = vertexOffset;
	ForceToRenderThread(renderer, &cmd);
}

/* Render Targets */

static void THREADEDGL_SetRenderTargets(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETRENDERTARGETS;
	cmd.setRenderTargets.renderTargets = renderTargets;
	cmd.setRenderTargets.numRenderTargets = numRenderTargets;
	cmd.setRenderTargets.renderbuffer = renderbuffer;
	cmd.setRenderTargets.depthFormat = depthFormat;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_ResolveTarget(
	FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *target
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_RESOLVETARGET;
	cmd.resolveTarget.target = target;
	ForceToRenderThread(renderer, &cmd);
}

/* Backbuffer Functions */

static void THREADEDGL_ResetBackbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_RESETBACKBUFFER;
	cmd.resetBackbuffer.presentationParameters = presentationParameters;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_READBACKBUFFER;
	cmd.readBackbuffer.data = data;
	cmd.readBackbuffer.dataLen = dataLen;
	cmd.readBackbuffer.startIndex = startIndex;
	cmd.readBackbuffer.elementCount = elementCount;
	cmd.readBackbuffer.elementSizeInBytes = elementSizeInBytes;
	cmd.readBackbuffer.x = x;
	cmd.readBackbuffer.y = y;
	cmd.readBackbuffer.w = w;
	cmd.readBackbuffer.h = h;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_GetBackbufferSize(
	FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETBACKBUFFERSIZE;
	cmd.getBackbufferSize.w = w;
	cmd.getBackbufferSize.h = h;
	ForceToRenderThread(renderer, &cmd);
}

static FNA3D_SurfaceFormat THREADEDGL_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETBACKBUFFERSURFACEFORMAT;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getBackbufferSurfaceFormat.retval;
}

static FNA3D_DepthFormat THREADEDGL_GetBackbufferDepthFormat(
	FNA3D_Renderer *driverData
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETBACKBUFFERDEPTHFORMAT;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getBackbufferDepthFormat.retval;
}

static int32_t THREADEDGL_GetBackbufferMultiSampleCount(
	FNA3D_Renderer *driverData
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETBACKBUFFERMULTISAMPLECOUNT;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getBackbufferMultiSampleCount.retval;
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CREATETEXTURE2D;
	cmd.createTexture2D.format = format;
	cmd.createTexture2D.width = width;
	cmd.createTexture2D.height = height;
	cmd.createTexture2D.levelCount = levelCount;
	cmd.createTexture2D.isRenderTarget = isRenderTarget;
	ForceToRenderThread(renderer, &cmd);
	return cmd.createTexture2D.retval;
}

static FNA3D_Texture* THREADEDGL_CreateTexture3D(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CREATETEXTURE3D;
	cmd.createTexture3D.format = format;
	cmd.createTexture3D.width = width;
	cmd.createTexture3D.height = height;
	cmd.createTexture3D.depth = depth;
	cmd.createTexture3D.levelCount = levelCount;
	ForceToRenderThread(renderer, &cmd);
	return cmd.createTexture3D.retval;
}

static FNA3D_Texture* THREADEDGL_CreateTextureCube(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CREATETEXTURECUBE;
	cmd.createTextureCube.format = format;
	cmd.createTextureCube.size = size;
	cmd.createTextureCube.levelCount = levelCount;
	cmd.createTextureCube.isRenderTarget = isRenderTarget;
	ForceToRenderThread(renderer, &cmd);
	return cmd.createTextureCube.retval;
}

static void THREADEDGL_AddDisposeTexture(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSETEXTURE;
	cmd.addDisposeTexture.texture = texture;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETTEXTUREDATA2D;
	cmd.setTextureData2D.texture = texture;
	cmd.setTextureData2D.format = format;
	cmd.setTextureData2D.x = x;
	cmd.setTextureData2D.y = y;
	cmd.setTextureData2D.w = w;
	cmd.setTextureData2D.h = h;
	cmd.setTextureData2D.level = level;
	cmd.setTextureData2D.data = data;
	cmd.setTextureData2D.dataLength = dataLength;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETTEXTUREDATA3D;
	cmd.setTextureData3D.texture = texture;
	cmd.setTextureData3D.format = format;
	cmd.setTextureData3D.level = level;
	cmd.setTextureData3D.left = left;
	cmd.setTextureData3D.top = top;
	cmd.setTextureData3D.right = right;
	cmd.setTextureData3D.bottom = bottom;
	cmd.setTextureData3D.front = front;
	cmd.setTextureData3D.back = back;
	cmd.setTextureData3D.data = data;
	cmd.setTextureData3D.dataLength = dataLength;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETTEXTUREDATACUBE;
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
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETTEXTUREDATAYUV;
	cmd.setTextureDataYUV.y = y;
	cmd.setTextureDataYUV.u = u;
	cmd.setTextureDataYUV.v = v;
	cmd.setTextureDataYUV.w = w;
	cmd.setTextureDataYUV.h = h;
	cmd.setTextureDataYUV.ptr = ptr;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETTEXTUREDATA2D;
	cmd.getTextureData2D.texture = texture;
	cmd.getTextureData2D.format = format;
	cmd.getTextureData2D.textureWidth = textureWidth;
	cmd.getTextureData2D.textureHeight = textureHeight;
	cmd.getTextureData2D.level = level;
	cmd.getTextureData2D.x = x;
	cmd.getTextureData2D.y = y;
	cmd.getTextureData2D.w = w;
	cmd.getTextureData2D.h = h;
	cmd.getTextureData2D.data = data;
	cmd.getTextureData2D.startIndex = startIndex;
	cmd.getTextureData2D.elementCount = elementCount;
	cmd.getTextureData2D.elementSizeInBytes = elementSizeInBytes;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETTEXTUREDATA3D;
	cmd.getTextureData3D.texture = texture;
	cmd.getTextureData3D.format = format;
	cmd.getTextureData3D.left = left;
	cmd.getTextureData3D.top = top;
	cmd.getTextureData3D.front = front;
	cmd.getTextureData3D.right = right;
	cmd.getTextureData3D.bottom = bottom;
	cmd.getTextureData3D.back = back;
	cmd.getTextureData3D.level = level;
	cmd.getTextureData3D.data = data;
	cmd.getTextureData3D.startIndex = startIndex;
	cmd.getTextureData3D.elementCount = elementCount;
	cmd.getTextureData3D.elementSizeInBytes = elementSizeInBytes;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETTEXTUREDATACUBE;
	cmd.getTextureDataCube.texture = texture;
	cmd.getTextureDataCube.format = format;
	cmd.getTextureDataCube.textureSize = textureSize;
	cmd.getTextureDataCube.cubeMapFace = cubeMapFace;
	cmd.getTextureDataCube.level = level;
	cmd.getTextureDataCube.x = x;
	cmd.getTextureDataCube.y = y;
	cmd.getTextureDataCube.w = w;
	cmd.getTextureDataCube.h = h;
	cmd.getTextureDataCube.data = data;
	cmd.getTextureDataCube.startIndex = startIndex;
	cmd.getTextureDataCube.elementCount = elementCount;
	cmd.getTextureDataCube.elementSizeInBytes = elementSizeInBytes;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GENCOLORRENDERBUFFER;
	cmd.genColorRenderbuffer.width = width;
	cmd.genColorRenderbuffer.height = height;
	cmd.genColorRenderbuffer.format = format;
	cmd.genColorRenderbuffer.multiSampleCount = multiSampleCount;
	cmd.genColorRenderbuffer.texture = texture;
	ForceToRenderThread(renderer, &cmd);
	return cmd.genColorRenderbuffer.retval;
}

static FNA3D_Renderbuffer* THREADEDGL_GenDepthStencilRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GENDEPTHSTENCILRENDERBUFFER;
	cmd.genDepthStencilRenderbuffer.width = width;
	cmd.genDepthStencilRenderbuffer.height = height;
	cmd.genDepthStencilRenderbuffer.format = format;
	cmd.genDepthStencilRenderbuffer.multiSampleCount = multiSampleCount;
	ForceToRenderThread(renderer, &cmd);
	return cmd.genDepthStencilRenderbuffer.retval;
}

static void THREADEDGL_AddDisposeRenderbuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSERENDERBUFFER;
	cmd.addDisposeRenderbuffer.renderbuffer = renderbuffer;
	ForceToRenderThread(renderer, &cmd);
}

/* Vertex Buffers */

static FNA3D_Buffer* THREADEDGL_GenVertexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t vertexCount,
	int32_t vertexStride
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GENVERTEXBUFFER;
	cmd.genVertexBuffer.dynamic = dynamic;
	cmd.genVertexBuffer.usage = usage;
	cmd.genVertexBuffer.vertexCount = vertexCount;
	cmd.genVertexBuffer.vertexStride = vertexStride;
	ForceToRenderThread(renderer, &cmd);
	return cmd.genVertexBuffer.retval;
}

static void THREADEDGL_AddDisposeVertexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSEVERTEXBUFFER;
	cmd.addDisposeVertexBuffer.buffer = buffer;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETVERTEXBUFFERDATA;
	cmd.setVertexBufferData.buffer = buffer;
	cmd.setVertexBufferData.offsetInBytes = offsetInBytes;
	cmd.setVertexBufferData.data = data;
	cmd.setVertexBufferData.dataLength = dataLength;
	cmd.setVertexBufferData.options = options;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETVERTEXBUFFERDATA;
	cmd.getVertexBufferData.buffer = buffer;
	cmd.getVertexBufferData.offsetInBytes = offsetInBytes;
	cmd.getVertexBufferData.data = data;
	cmd.getVertexBufferData.startIndex = startIndex;
	cmd.getVertexBufferData.elementCount = elementCount;
	cmd.getVertexBufferData.elementSizeInBytes = elementSizeInBytes;
	cmd.getVertexBufferData.vertexStride = vertexStride;
	ForceToRenderThread(renderer, &cmd);
}

/* Index Buffers */

static FNA3D_Buffer* THREADEDGL_GenIndexBuffer(
	FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t indexCount,
	FNA3D_IndexElementSize indexElementSize
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GENINDEXBUFFER;
	cmd.genIndexBuffer.dynamic = dynamic;
	cmd.genIndexBuffer.usage = usage;
	cmd.genIndexBuffer.indexCount = indexCount;
	cmd.genIndexBuffer.indexElementSize = indexElementSize;
	ForceToRenderThread(renderer, &cmd);
	return cmd.genIndexBuffer.retval;
}

static void THREADEDGL_AddDisposeIndexBuffer(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSEINDEXBUFFER;
	cmd.addDisposeIndexBuffer.buffer = buffer;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetIndexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETINDEXBUFFERDATA;
	cmd.setIndexBufferData.buffer = buffer;
	cmd.setIndexBufferData.offsetInBytes = offsetInBytes;
	cmd.setIndexBufferData.data = data;
	cmd.setIndexBufferData.dataLength = dataLength;
	ForceToRenderThread(renderer, &cmd);
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
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETINDEXBUFFERDATA;
	cmd.getIndexBufferData.buffer = buffer;
	cmd.getIndexBufferData.offsetInBytes = offsetInBytes;
	cmd.getIndexBufferData.data = data;
	cmd.getIndexBufferData.startIndex = startIndex;
	cmd.getIndexBufferData.elementCount = elementCount;
	cmd.getIndexBufferData.elementSizeInBytes = elementSizeInBytes;
	ForceToRenderThread(renderer, &cmd);
}

/* Effects */

static void THREADEDGL_CreateEffect(
	FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CREATEEFFECT;
	cmd.createEffect.effectCode = effectCode;
	cmd.createEffect.effectCodeLength = effectCodeLength;
	cmd.createEffect.effect = effect;
	cmd.createEffect.effectData = effectData;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_CloneEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CLONEEFFECT;
	cmd.cloneEffect.cloneSource = cloneSource;
	cmd.cloneEffect.effect = effect;
	cmd.cloneEffect.effectData = effectData;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_AddDisposeEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSEEFFECT;
	cmd.addDisposeEffect.effect = effect;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_SetEffectTechnique(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETEFFECTTECHNIQUE;
	cmd.setEffectTechnique.effect = effect;
	cmd.setEffectTechnique.technique = technique;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_ApplyEffect(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_APPLYEFFECT;
	cmd.applyEffect.effect = effect;
	cmd.applyEffect.pass = pass;
	cmd.applyEffect.stateChanges = stateChanges;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_BeginPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_BEGINPASSRESTORE;
	cmd.beginPassRestore.effect = effect;
	cmd.beginPassRestore.stateChanges = stateChanges;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ENDPASSRESTORE;
	cmd.endPassRestore.effect = effect;
	ForceToRenderThread(renderer, &cmd);
}

/* Queries */

static FNA3D_Query* THREADEDGL_CreateQuery(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_CREATEQUERY;
	ForceToRenderThread(renderer, &cmd);
	return cmd.createQuery.retval;
}

static void THREADEDGL_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_ADDDISPOSEQUERY;
	cmd.addDisposeQuery.query = query;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_QueryBegin(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_QUERYBEGIN;
	cmd.queryBegin.query = query;
	ForceToRenderThread(renderer, &cmd);
}

static void THREADEDGL_QueryEnd(FNA3D_Renderer *driverData, FNA3D_Query *query)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_QUERYEND;
	cmd.queryEnd.query = query;
	ForceToRenderThread(renderer, &cmd);
}

static uint8_t THREADEDGL_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_QUERYCOMPLETE;
	cmd.queryComplete.query = query;
	ForceToRenderThread(renderer, &cmd);
	return cmd.queryComplete.retval;
}

static int32_t THREADEDGL_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_QUERYPIXELCOUNT;
	cmd.queryPixelCount.query = query;
	ForceToRenderThread(renderer, &cmd);
	return cmd.queryPixelCount.retval;
}

/* Feature Queries */

static uint8_t THREADEDGL_SupportsDXT1(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SUPPORTSDXT1;
	ForceToRenderThread(renderer, &cmd);
	return cmd.supportsDXT1.retval;
}

static uint8_t THREADEDGL_SupportsS3TC(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SUPPORTSS3TC;
	ForceToRenderThread(renderer, &cmd);
	return cmd.supportsS3TC.retval;
}

static uint8_t THREADEDGL_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SUPPORTSHARDWAREINSTANCING;
	ForceToRenderThread(renderer, &cmd);
	return cmd.supportsHardwareInstancing.retval;
}

static uint8_t THREADEDGL_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SUPPORTSNOOVERWRITE;
	ForceToRenderThread(renderer, &cmd);
	return cmd.supportsNoOverwrite.retval;
}

static int32_t THREADEDGL_GetMaxTextureSlots(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETMAXTEXTURESLOTS;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getMaxTextureSlots.retval;
}

static int32_t THREADEDGL_GetMaxMultiSampleCount(FNA3D_Renderer *driverData)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_GETMAXMULTISAMPLECOUNT;
	ForceToRenderThread(renderer, &cmd);
	return cmd.getMaxMultiSampleCount.retval;
}

/* Debugging */

static void THREADEDGL_SetStringMarker(FNA3D_Renderer *driverData, const char *text)
{
	GLThreadCommand cmd;
	ThreadedGLRenderer *renderer = (ThreadedGLRenderer*) driverData;

	cmd.type = COMMAND_SETSTRINGMARKER;
	cmd.setStringMarker.text = text;
	ForceToRenderThread(renderer, &cmd);
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

	FNA3D_LogInfo("Using ThreadedGL!");

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
