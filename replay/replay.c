/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2021 Ethan Lee
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

#include <SDL.h>
#include <FNA3D.h>

static const uint8_t MARK_CREATEDEVICE			= 0;
static const uint8_t MARK_DESTROYDEVICE			= 1;
static const uint8_t MARK_SWAPBUFFERS			= 2;
static const uint8_t MARK_CLEAR				= 3;
static const uint8_t MARK_DRAWINDEXEDPRIMITIVES		= 4;
static const uint8_t MARK_DRAWINSTANCEDPRIMITIVES	= 5;
static const uint8_t MARK_DRAWPRIMITIVES		= 6;
static const uint8_t MARK_SETVIEWPORT			= 7;
static const uint8_t MARK_SETSCISSORRECT		= 8;
static const uint8_t MARK_SETBLENDFACTOR		= 9;
static const uint8_t MARK_SETMULTISAMPLEMASK		= 10;
static const uint8_t MARK_SETREFERENCESTENCIL		= 11;
static const uint8_t MARK_SETBLENDSTATE			= 12;
static const uint8_t MARK_SETDEPTHSTENCILSTATE		= 13;
static const uint8_t MARK_APPLYRASTERIZERSTATE		= 14;
static const uint8_t MARK_VERIFYSAMPLER			= 15;
static const uint8_t MARK_VERIFYVERTEXSAMPLER		= 16;
static const uint8_t MARK_APPLYVERTEXBUFFERBINDINGS	= 17;
static const uint8_t MARK_SETRENDERTARGETS		= 18;
static const uint8_t MARK_RESOLVETARGET			= 19;
static const uint8_t MARK_RESETBACKBUFFER		= 20;
static const uint8_t MARK_READBACKBUFFER		= 21;
static const uint8_t MARK_CREATETEXTURE2D		= 22;
static const uint8_t MARK_CREATETEXTURE3D		= 23;
static const uint8_t MARK_CREATETEXTURECUBE		= 24;
static const uint8_t MARK_ADDDISPOSETEXTURE		= 25;
static const uint8_t MARK_SETTEXTUREDATA2D		= 26;
static const uint8_t MARK_SETTEXTUREDATA3D		= 27;
static const uint8_t MARK_SETTEXTUREDATACUBE		= 28;
static const uint8_t MARK_SETTEXTUREDATAYUV		= 29;
static const uint8_t MARK_GETTEXTUREDATA2D		= 30;
static const uint8_t MARK_GETTEXTUREDATA3D		= 31;
static const uint8_t MARK_GETTEXTUREDATACUBE		= 32;
static const uint8_t MARK_GENCOLORRENDERBUFFER		= 33;
static const uint8_t MARK_GENDEPTHSTENCILRENDERBUFFER	= 34;
static const uint8_t MARK_ADDDISPOSERENDERBUFFER	= 35;
static const uint8_t MARK_GENVERTEXBUFFER		= 36;
static const uint8_t MARK_ADDDISPOSEVERTEXBUFFER	= 37;
static const uint8_t MARK_SETVERTEXBUFFERDATA		= 38;
static const uint8_t MARK_GETVERTEXBUFFERDATA		= 39;
static const uint8_t MARK_GENINDEXBUFFER		= 40;
static const uint8_t MARK_ADDDISPOSEINDEXBUFFER		= 41;
static const uint8_t MARK_SETINDEXBUFFERDATA		= 42;
static const uint8_t MARK_GETINDEXBUFFERDATA		= 43;
static const uint8_t MARK_CREATEEFFECT			= 44;
static const uint8_t MARK_CLONEEFFECT			= 45;
static const uint8_t MARK_ADDDISPOSEEFFECT		= 46;
static const uint8_t MARK_SETEFFECTTECHNIQUE		= 47;
static const uint8_t MARK_APPLYEFFECT			= 48;
static const uint8_t MARK_BEGINPASSRESTORE		= 49;
static const uint8_t MARK_ENDPASSRESTORE		= 50;
static const uint8_t MARK_CREATEQUERY			= 51;
static const uint8_t MARK_ADDDISPOSEQUERY		= 52;
static const uint8_t MARK_QUERYBEGIN			= 53;
static const uint8_t MARK_QUERYEND			= 54;
static const uint8_t MARK_QUERYPIXELCOUNT		= 55;
static const uint8_t MARK_SETSTRINGMARKER		= 56;

int main(int argc, char **argv)
{
	#define READ(val) ops->read(ops, &val, sizeof(val), 1)

	SDL_WindowFlags flags;
	SDL_RWops *ops;
	uint8_t mark;

	/* CreateDevice, ResetBackbuffer */
	FNA3D_Device *device;
	FNA3D_PresentationParameters presentationParameters;
	uint8_t debugMode;

	/* SwapBuffers */
	uint8_t hasSource, hasDestination;
	FNA3D_Rect sourceRectangle;
	FNA3D_Rect destinationRectangle;

	/* Clear */
	FNA3D_ClearOptions options;
	FNA3D_Vec4 color;
	float depth;
	int32_t stencil;

	/* DrawPrimitives */
	FNA3D_PrimitiveType primitiveType;
	int32_t vertexStart;
	int32_t primitiveCount;

	/* SetViewport */
	FNA3D_Viewport viewport;

	/* SetScissorRect */
	FNA3D_Rect scissor;

	/* SetBlendFactor */
	FNA3D_Color blendFactor;

	/* SetMultiSampleMask */
	int32_t mask;

	/* SetReferenceStencil */
	int32_t ref;

	/* SetBlendState */
	FNA3D_BlendState blendState;

	/* SetDepthStencilState */
	FNA3D_DepthStencilState depthStencilState;

	/* ApplyRasterizerState */
	FNA3D_RasterizerState rasterizerState;

	/* CreateEffect */
	FNA3D_Effect *effect;
	MOJOSHADER_effect *effectData;

	/* Miscellaneous allocations, dimensions, blah blah... */
	int32_t x, y, z, w, h, d, levelCount, sizeInBytes, dataLength;
	FNA3D_CubeMapFace cubeMapFace;
	FNA3D_SurfaceFormat format;
	FNA3D_BufferUsage usage;
	uint8_t isRenderTarget, dynamic;
	void* miscBuffer;

	/* TODO: Use argv for filenames */
	ops = SDL_RWFromFile("FNA3D_Trace.bin", "rb");
	if (ops == NULL)
	{
		SDL_Log("FNA3D_Trace.bin not found!");
		return 0;
	}

	/* Beginning of the file should be a CreateDevice call */
	READ(mark);
	if (mark != MARK_CREATEDEVICE)
	{
		SDL_Log("Bad trace!");
		return 0;
	}
	READ(presentationParameters.backBufferWidth);
	READ(presentationParameters.backBufferHeight);
	READ(presentationParameters.backBufferFormat);
	READ(presentationParameters.multiSampleCount);
	READ(presentationParameters.isFullScreen);
	READ(presentationParameters.depthStencilFormat);
	READ(presentationParameters.presentationInterval);
	READ(presentationParameters.displayOrientation);
	READ(presentationParameters.renderTargetUsage);
	READ(debugMode);

	/* Create a window alongside the device */
	SDL_Init(SDL_INIT_VIDEO);
	flags = SDL_WINDOW_SHOWN | FNA3D_PrepareWindowAttributes();
	if (presentationParameters.isFullScreen)
	{
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	presentationParameters.deviceWindowHandle = SDL_CreateWindow(
		"FNA3D Replay",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		presentationParameters.backBufferWidth,
		presentationParameters.backBufferHeight,
		flags
	);
	device = FNA3D_CreateDevice(&presentationParameters, debugMode);

	/* Go through all the calls, let vsync do the timing if applicable */
	READ(mark);
	while (mark != MARK_DESTROYDEVICE)
	{
		switch (mark)
		{
		case MARK_SWAPBUFFERS:
			READ(hasSource);
			if (hasSource)
			{
				READ(sourceRectangle.x);
				READ(sourceRectangle.y);
				READ(sourceRectangle.w);
				READ(sourceRectangle.h);
			}
			READ(hasDestination);
			if (hasDestination)
			{
				READ(destinationRectangle.x);
				READ(destinationRectangle.y);
				READ(destinationRectangle.w);
				READ(destinationRectangle.h);
			}
			FNA3D_SwapBuffers(
				device,
				&sourceRectangle,
				&destinationRectangle,
				presentationParameters.deviceWindowHandle
			);
			break;
		case MARK_CLEAR:
			READ(options);
			READ(color.x);
			READ(color.y);
			READ(color.z);
			READ(color.w);
			READ(depth);
			READ(stencil);
			FNA3D_Clear(device, options, &color, depth, stencil);
			break;
		case MARK_DRAWINDEXEDPRIMITIVES:
			break;
		case MARK_DRAWINSTANCEDPRIMITIVES:
			break;
		case MARK_DRAWPRIMITIVES:
			READ(primitiveType);
			READ(vertexStart);
			READ(primitiveCount);
			FNA3D_DrawPrimitives(
				device,
				primitiveType,
				vertexStart,
				primitiveCount
			);
			break;
		case MARK_SETVIEWPORT:
			READ(viewport.x);
			READ(viewport.y);
			READ(viewport.w);
			READ(viewport.h);
			READ(viewport.minDepth);
			READ(viewport.maxDepth);
			FNA3D_SetViewport(device, &viewport);
			break;
		case MARK_SETSCISSORRECT:
			READ(scissor.x);
			READ(scissor.y);
			READ(scissor.w);
			READ(scissor.h);
			FNA3D_SetScissorRect(device, &scissor);
			break;
		case MARK_SETBLENDFACTOR:
			READ(blendFactor.r);
			READ(blendFactor.g);
			READ(blendFactor.b);
			READ(blendFactor.a);
			FNA3D_SetBlendFactor(device, &blendFactor);
			break;
		case MARK_SETMULTISAMPLEMASK:
			READ(mask);
			FNA3D_SetMultiSampleMask(device, mask);
			break;
		case MARK_SETREFERENCESTENCIL:
			READ(ref);
			FNA3D_SetReferenceStencil(device, ref);
			break;
		case MARK_SETBLENDSTATE:
			READ(blendState.colorSourceBlend);
			READ(blendState.colorDestinationBlend);
			READ(blendState.colorBlendFunction);
			READ(blendState.alphaSourceBlend);
			READ(blendState.alphaDestinationBlend);
			READ(blendState.alphaBlendFunction);
			READ(blendState.colorWriteEnable);
			READ(blendState.colorWriteEnable1);
			READ(blendState.colorWriteEnable2);
			READ(blendState.colorWriteEnable3);
			READ(blendState.blendFactor.r);
			READ(blendState.blendFactor.g);
			READ(blendState.blendFactor.b);
			READ(blendState.blendFactor.a);
			READ(blendState.multiSampleMask);
			FNA3D_SetBlendState(device, &blendState);
			break;
		case MARK_SETDEPTHSTENCILSTATE:
			READ(depthStencilState.depthBufferEnable);
			READ(depthStencilState.depthBufferWriteEnable);
			READ(depthStencilState.depthBufferFunction);
			READ(depthStencilState.stencilEnable);
			READ(depthStencilState.stencilMask);
			READ(depthStencilState.stencilWriteMask);
			READ(depthStencilState.twoSidedStencilMode);
			READ(depthStencilState.stencilFail);
			READ(depthStencilState.stencilDepthBufferFail);
			READ(depthStencilState.stencilPass);
			READ(depthStencilState.stencilFunction);
			READ(depthStencilState.ccwStencilFail);
			READ(depthStencilState.ccwStencilDepthBufferFail);
			READ(depthStencilState.ccwStencilPass);
			READ(depthStencilState.ccwStencilFunction);
			READ(depthStencilState.referenceStencil);
			FNA3D_SetDepthStencilState(device, &depthStencilState);
			break;
		case MARK_APPLYRASTERIZERSTATE:
			READ(rasterizerState.fillMode);
			READ(rasterizerState.cullMode);
			READ(rasterizerState.depthBias);
			READ(rasterizerState.slopeScaleDepthBias);
			READ(rasterizerState.scissorTestEnable);
			READ(rasterizerState.multiSampleAntiAlias);
			FNA3D_ApplyRasterizerState(device, &rasterizerState);
			break;
		case MARK_VERIFYSAMPLER:
			break;
		case MARK_VERIFYVERTEXSAMPLER:
			break;
		case MARK_APPLYVERTEXBUFFERBINDINGS:
			break;
		case MARK_SETRENDERTARGETS:
			break;
		case MARK_RESOLVETARGET:
			break;
		case MARK_RESETBACKBUFFER:
			READ(presentationParameters.backBufferWidth);
			READ(presentationParameters.backBufferHeight);
			READ(presentationParameters.backBufferFormat);
			READ(presentationParameters.multiSampleCount);
			READ(presentationParameters.isFullScreen);
			READ(presentationParameters.depthStencilFormat);
			READ(presentationParameters.presentationInterval);
			READ(presentationParameters.displayOrientation);
			READ(presentationParameters.renderTargetUsage);
			SDL_SetWindowFullscreen(
				presentationParameters.deviceWindowHandle,
				presentationParameters.isFullScreen ?
					SDL_WINDOW_FULLSCREEN_DESKTOP :
					0
			);
			SDL_SetWindowSize(
				presentationParameters.deviceWindowHandle,
				presentationParameters.backBufferWidth,
				presentationParameters.backBufferHeight
			);
			FNA3D_ResetBackbuffer(device, &presentationParameters);
			break;
		case MARK_READBACKBUFFER:
			READ(x);
			READ(y);
			READ(w);
			READ(h);
			READ(dataLength);
			miscBuffer = SDL_malloc(dataLength);
			FNA3D_ReadBackbuffer(
				device,
				x,
				y,
				w,
				h,
				miscBuffer,
				dataLength
			);
			SDL_free(miscBuffer);
			break;
		case MARK_CREATETEXTURE2D:
			READ(format);
			READ(w);
			READ(h);
			READ(levelCount);
			READ(isRenderTarget);
			/* TODO: */ FNA3D_CreateTexture2D(
				device,
				format,
				w,
				h,
				levelCount,
				isRenderTarget
			);
			break;
		case MARK_CREATETEXTURE3D:
			READ(format);
			READ(w);
			READ(h);
			READ(d);
			READ(levelCount);
			/* TODO: */ FNA3D_CreateTexture3D(
				device,
				format,
				w,
				h,
				d,
				levelCount
			);
			break;
		case MARK_CREATETEXTURECUBE:
			READ(format);
			READ(w);
			READ(levelCount);
			READ(isRenderTarget);
			/* TODO: */ FNA3D_CreateTextureCube(
				device,
				format,
				w,
				levelCount,
				isRenderTarget
			);
			break;
		case MARK_ADDDISPOSETEXTURE:
			break;
		case MARK_SETTEXTUREDATA2D:
			break;
		case MARK_SETTEXTUREDATA3D:
			break;
		case MARK_SETTEXTUREDATACUBE:
			break;
		case MARK_SETTEXTUREDATAYUV:
			break;
		case MARK_GETTEXTUREDATA2D:
			break;
		case MARK_GETTEXTUREDATA3D:
			break;
		case MARK_GETTEXTUREDATACUBE:
			break;
		case MARK_GENCOLORRENDERBUFFER:
			break;
		case MARK_GENDEPTHSTENCILRENDERBUFFER:
			break;
		case MARK_ADDDISPOSERENDERBUFFER:
			break;
		case MARK_GENVERTEXBUFFER:
			READ(dynamic);
			READ(usage);
			READ(sizeInBytes);
			/* TODO: */ FNA3D_GenVertexBuffer(
				device,
				dynamic,
				usage,
				sizeInBytes
			);
			break;
		case MARK_ADDDISPOSEVERTEXBUFFER:
			break;
		case MARK_SETVERTEXBUFFERDATA:
			break;
		case MARK_GETVERTEXBUFFERDATA:
			break;
		case MARK_GENINDEXBUFFER:
			READ(dynamic);
			READ(usage);
			READ(sizeInBytes);
			/* TODO: */ FNA3D_GenIndexBuffer(
				device,
				dynamic,
				usage,
				sizeInBytes
			);
			break;
		case MARK_ADDDISPOSEINDEXBUFFER:
			break;
		case MARK_SETINDEXBUFFERDATA:
			break;
		case MARK_GETINDEXBUFFERDATA:
			break;
		case MARK_CREATEEFFECT:
			READ(dataLength);
			miscBuffer = SDL_malloc(dataLength);
			FNA3D_CreateEffect(
				device,
				(uint8_t*) miscBuffer,
				dataLength,
				&effect,
				&effectData
			);
			break;
		case MARK_CLONEEFFECT:
			break;
		case MARK_ADDDISPOSEEFFECT:
			break;
		case MARK_SETEFFECTTECHNIQUE:
			break;
		case MARK_APPLYEFFECT:
			break;
		case MARK_BEGINPASSRESTORE:
			break;
		case MARK_ENDPASSRESTORE:
			break;
		case MARK_CREATEQUERY:
			/* TODO: */ FNA3D_CreateQuery(device);
			break;
		case MARK_ADDDISPOSEQUERY:
			break;
		case MARK_QUERYBEGIN:
			break;
		case MARK_QUERYEND:
			break;
		case MARK_QUERYPIXELCOUNT:
			break;
		case MARK_SETSTRINGMARKER:
			READ(dataLength);
			miscBuffer = SDL_malloc(dataLength);
			ops->read(ops, miscBuffer, dataLength, 1);
			FNA3D_SetStringMarker(device, (char*) miscBuffer);
			break;
		case MARK_CREATEDEVICE:
		case MARK_DESTROYDEVICE:
			SDL_assert(0 && "Unexpected mark!");
			break;
		default:
			SDL_assert(0 && "Unrecognized mark!");
			break;
		}
		READ(mark);
	}

	/* Clean up. We out. */
	ops->close(ops);
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(presentationParameters.deviceWindowHandle);
	SDL_Quit();
	return 0;

	#undef READ
}
