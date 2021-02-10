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

#include "FNA3D_Tracing.h"

#ifdef FNA3D_TRACING

#include <SDL.h>

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

#define WRITE(val) ops->write(ops, &val, sizeof(val), 1)

static void* windowHandle = NULL;

void FNA3D_Trace_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
) {
	SDL_RWops *ops = SDL_RWFromFile("FNA3D_Trace.bin", "wb");
	WRITE(MARK_CREATEDEVICE);
	WRITE(presentationParameters->backBufferWidth);
	WRITE(presentationParameters->backBufferHeight);
	WRITE(presentationParameters->backBufferFormat);
	WRITE(presentationParameters->multiSampleCount);
	windowHandle = presentationParameters->deviceWindowHandle;
	WRITE(presentationParameters->isFullScreen);
	WRITE(presentationParameters->depthStencilFormat);
	WRITE(presentationParameters->presentationInterval);
	WRITE(presentationParameters->displayOrientation);
	WRITE(presentationParameters->renderTargetUsage);
	WRITE(debugMode);
	ops->close(ops);
}

void FNA3D_Trace_DestroyDevice(void)
{
	SDL_RWops *ops = SDL_RWFromFile("FNA3D_Trace.bin", "ab");
	WRITE(MARK_DESTROYDEVICE);
	ops->close(ops);
}

void FNA3D_Trace_SwapBuffers(
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
	SDL_RWops *ops;
	uint8_t hasSource = sourceRectangle != NULL;
	uint8_t hasDestination = destinationRectangle != NULL;

	SDL_assert(overrideWindowHandle == windowHandle);

	ops = SDL_RWFromFile("FNA3D_Trace.bin", "ab");
	WRITE(MARK_SWAPBUFFERS);
	WRITE(hasSource);
	if (hasSource)
	{
		WRITE(sourceRectangle->x);
		WRITE(sourceRectangle->y);
		WRITE(sourceRectangle->w);
		WRITE(sourceRectangle->h);
	}
	WRITE(hasDestination);
	if (hasDestination)
	{
		WRITE(destinationRectangle->x);
		WRITE(destinationRectangle->y);
		WRITE(destinationRectangle->w);
		WRITE(destinationRectangle->h);
	}
	ops->close(ops);
}

void FNA3D_Trace_Clear(
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
	SDL_RWops *ops = SDL_RWFromFile("FNA3D_Trace.bin", "ab");
	WRITE(MARK_CLEAR);
	WRITE(options);
	WRITE(color->x);
	WRITE(color->y);
	WRITE(color->z);
	WRITE(color->w);
	WRITE(depth);
	WRITE(stencil);
	ops->close(ops);
}

void FNA3D_Trace_DrawIndexedPrimitives(
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

void FNA3D_Trace_DrawInstancedPrimitives(
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

void FNA3D_Trace_DrawPrimitives(
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
}

void FNA3D_Trace_SetViewport(FNA3D_Viewport *viewport)
{
}

void FNA3D_Trace_SetScissorRect(FNA3D_Rect *scissor)
{
}

void FNA3D_Trace_SetBlendFactor(
	FNA3D_Color *blendFactor
) {
}

void FNA3D_Trace_SetMultiSampleMask(int32_t mask)
{
}

void FNA3D_Trace_SetReferenceStencil(int32_t ref)
{
}

void FNA3D_Trace_SetBlendState(
	FNA3D_BlendState *blendState
) {
}

void FNA3D_Trace_SetDepthStencilState(
	FNA3D_DepthStencilState *depthStencilState
) {
}

void FNA3D_Trace_ApplyRasterizerState(
	FNA3D_RasterizerState *rasterizerState
) {
}

void FNA3D_Trace_VerifySampler(
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
}

void FNA3D_Trace_VerifyVertexSampler(
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
}

void FNA3D_Trace_ApplyVertexBufferBindings(
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
}

void FNA3D_Trace_SetRenderTargets(
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveTargetContents
) {
}

void FNA3D_Trace_ResolveTarget(
	FNA3D_RenderTargetBinding *target
) {
}

void FNA3D_Trace_ResetBackbuffer(
	FNA3D_PresentationParameters *presentationParameters
) {
	SDL_RWops *ops;

	SDL_assert(presentationParameters->deviceWindowHandle == windowHandle);

	ops = SDL_RWFromFile("FNA3D_Trace.bin", "wb");
	WRITE(MARK_RESETBACKBUFFER);
	WRITE(presentationParameters->backBufferWidth);
	WRITE(presentationParameters->backBufferHeight);
	WRITE(presentationParameters->backBufferFormat);
	WRITE(presentationParameters->multiSampleCount);
	WRITE(presentationParameters->isFullScreen);
	WRITE(presentationParameters->depthStencilFormat);
	WRITE(presentationParameters->presentationInterval);
	WRITE(presentationParameters->displayOrientation);
	WRITE(presentationParameters->renderTargetUsage);
	ops->close(ops);
}

void FNA3D_Trace_ReadBackbuffer(
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t dataLength
) {
}

void FNA3D_Trace_CreateTexture2D(
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
}

void FNA3D_Trace_CreateTexture3D(
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
}

void FNA3D_Trace_CreateTextureCube(
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
}

void FNA3D_Trace_AddDisposeTexture(
	FNA3D_Texture *texture
) {
}

void FNA3D_Trace_SetTextureData2D(
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
}

void FNA3D_Trace_SetTextureData3D(
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	void* data,
	int32_t dataLength
) {
}

void FNA3D_Trace_SetTextureDataCube(
	FNA3D_Texture *texture,
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

void FNA3D_Trace_SetTextureDataYUV(
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t yWidth,
	int32_t yHeight,
	int32_t uvWidth,
	int32_t uvHeight,
	void* data,
	int32_t dataLength
) {
}

void FNA3D_Trace_GetTextureData2D(
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	int32_t dataLength
) {
}

void FNA3D_Trace_GetTextureData3D(
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t z,
	int32_t w,
	int32_t h,
	int32_t d,
	int32_t level,
	int32_t dataLength
) {
}

void FNA3D_Trace_GetTextureDataCube(
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	int32_t dataLength
) {
}

void FNA3D_Trace_GenColorRenderbuffer(
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
}

void FNA3D_Trace_GenDepthStencilRenderbuffer(
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
}

void FNA3D_Trace_AddDisposeRenderbuffer(
	FNA3D_Renderbuffer *renderbuffer
) {
}

void FNA3D_Trace_GenVertexBuffer(
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
}

void FNA3D_Trace_AddDisposeVertexBuffer(
	FNA3D_Buffer *buffer
) {
}

void FNA3D_Trace_SetVertexBufferData(
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
}

void FNA3D_Trace_GetVertexBufferData(
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
}

void FNA3D_Trace_GenIndexBuffer(
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
}

void FNA3D_Trace_AddDisposeIndexBuffer(
	FNA3D_Buffer *buffer
) {
}

void FNA3D_Trace_SetIndexBufferData(
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
}

void FNA3D_Trace_GetIndexBufferData(
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	int32_t dataLength
) {
}

void FNA3D_Trace_CreateEffect(
	uint8_t *effectCode,
	uint32_t effectCodeLength
) {
}

void FNA3D_Trace_CloneEffect(
	FNA3D_Effect *cloneSource
) {
}

void FNA3D_Trace_AddDisposeEffect(
	FNA3D_Effect *effect
) {
}

void FNA3D_Trace_SetEffectTechnique(
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
}

void FNA3D_Trace_ApplyEffect(
	FNA3D_Effect *effect,
	uint32_t pass
) {
}

void FNA3D_Trace_BeginPassRestore(
	FNA3D_Effect *effect
) {
}

void FNA3D_Trace_EndPassRestore(
	FNA3D_Effect *effect
) {
}

void FNA3D_Trace_CreateQuery(void)
{
}

void FNA3D_Trace_AddDisposeQuery(FNA3D_Query *query)
{
}

void FNA3D_Trace_QueryBegin(FNA3D_Query *query)
{
}

void FNA3D_Trace_QueryEnd(FNA3D_Query *query)
{
}

void FNA3D_Trace_QueryPixelCount(
	FNA3D_Query *query
) {
}

void FNA3D_Trace_SetStringMarker(const char *text)
{
}

void FNA3D_Trace_RegisterTexture(FNA3D_Texture *texture)
{
}

void FNA3D_Trace_RegisterRenderbuffer(FNA3D_Renderbuffer *renderbuffer)
{
}

void FNA3D_Trace_RegisterBuffer(FNA3D_Buffer *buffer)
{
}

void FNA3D_Trace_RegisterEffect(FNA3D_Effect *effect, MOJOSHADER_effect *data)
{
}

void FNA3D_Trace_RegisterQuery(FNA3D_Query *query)
{
}

#undef WRITE

#else

extern int this_tu_is_empty;

#endif /* FNA3D_TRACING */
