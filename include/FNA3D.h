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

/* Enumerations, should match XNA 4.0 */

typedef enum FNA3D_PresentInterval
{
	FNA3D_PRESENTINTERVAL_DEFAULT,
	FNA3D_PRESENTINTERVAL_ONE,
	FNA3D_PRESENTINTERVAL_TWO,
	FNA3D_PRESENTINTERVAL_IMMEDIATE
} FNA3D_PresentInterval;

typedef enum FNA3D_DisplayOrientation
{
	FNA3D_DISPLAYORIENTATION_DEFAULT,
	FNA3D_DISPLAYORIENTATION_LANDSCAPELEFT,
	FNA3D_DISPLAYORIENTATION_LANDSCAPERIGHT,
	FNA3D_DISPLAYORIENTATION_PORTRAIT
} FNA3D_DisplayOrientation;

typedef enum FNA3D_RenderTargetUsage
{
	FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS,
	FNA3D_RENDERTARGETUSAGE_PRESERVECONTENTS,
	FNA3D_RENDERTARGETUSAGE_PLATFORMCONTENTS
} FNA3D_RenderTargetUsage;

typedef enum FNA3D_ClearOptions
{
	FNA3D_CLEAROPTIONS_TARGET	= 1,
	FNA3D_CLEAROPTIONS_DEPTHBUFFER	= 2,
	FNA3D_CLEAROPTIONS_STENCIL	= 4
} FNA3D_ClearOptions;

typedef enum FNA3D_PrimitiveType
{
	FNA3D_PRIMITIVETYPE_TRIANGLELIST,
	FNA3D_PRIMITIVETYPE_TRIANGLESTRIP,
	FNA3D_PRIMITIVETYPE_LINELIST,
	FNA3D_PRIMITIVETYPE_LINESTRIP,
	FNA3D_PRIMITIVETYPE_POINTLIST_EXT
} FNA3D_PrimitiveType;

typedef enum FNA3D_IndexElementSize
{
	FNA3D_INDEXELEMENTSIZE_16BIT,
	FNA3D_INDEXELEMENTSIZE_32BIT
} FNA3D_IndexElementSize;

typedef enum FNA3D_SurfaceFormat
{
	FNA3D_SURFACEFORMAT_COLOR,
	FNA3D_SURFACEFORMAT_BGR565,
	FNA3D_SURFACEFORMAT_BGRA5551,
	FNA3D_SURFACEFORMAT_BGRA4444,
	FNA3D_SURFACEFORMAT_DXT1,
	FNA3D_SURFACEFORMAT_DXT3,
	FNA3D_SURFACEFORMAT_DXT5,
	FNA3D_SURFACEFORMAT_NORMALIZEDBYTE2,
	FNA3D_SURFACEFORMAT_NORMALIZEDBYTE4,
	FNA3D_SURFACEFORMAT_RGBA1010102,
	FNA3D_SURFACEFORMAT_RG32,
	FNA3D_SURFACEFORMAT_RGBA64,
	FNA3D_SURFACEFORMAT_ALPHA8,
	FNA3D_SURFACEFORMAT_SINGLE,
	FNA3D_SURFACEFORMAT_VECTOR2,
	FNA3D_SURFACEFORMAT_VECTOR4,
	FNA3D_SURFACEFORMAT_HALFSINGLE,
	FNA3D_SURFACEFORMAT_HALFVECTOR2,
	FNA3D_SURFACEFORMAT_HALFVECTOR4,
	FNA3D_SURFACEFORMAT_HDRBLENDABLE,
	FNA3D_SURFACEFORMAT_COLORBGRA_EXT
} FNA3D_SurfaceFormat;

typedef enum FNA3D_DepthFormat
{
	FNA3D_DEPTHFORMAT_NONE,
	FNA3D_DEPTHFORMAT_D16,
	FNA3D_DEPTHFORMAT_D24,
	FNA3D_DEPTHFORMAT_D24S8
} FNA3D_DepthFormat;

typedef enum FNA3D_CubeMapFace
{
	FNA3D_CUBEMAPFACE_POSITIVEX,
	FNA3D_CUBEMAPFACE_NEGATIVEX,
	FNA3D_CUBEMAPFACE_POSITIVEY,
	FNA3D_CUBEMAPFACE_NEGATIVEY,
	FNA3D_CUBEMAPFACE_POSITIVEZ,
	FNA3D_CUBEMAPFACE_NEGATIVEZ
} FNA3D_CubeMapFace;

typedef enum FNA3D_BufferUsage
{
	FNA3D_BUFFERUSAGE_NONE,
	FNA3D_BUFFERUSAGE_WRITEONLY
} FNA3D_BufferUsage;

typedef enum FNA3D_SetDataOptions
{
	FNA3D_SETDATAOPTIONS_NONE,
	FNA3D_SETDATAOPTIONS_DISCARD,
	FNA3D_SETDATAOPTIONS_NOOVERWRITE
} FNA3D_SetDataOptions;

typedef enum FNA3D_Blend
{
	FNA3D_BLEND_ONE,
	FNA3D_BLEND_ZERO,
	FNA3D_BLEND_SOURCECOLOR,
	FNA3D_BLEND_INVERSESOURCECOLOR,
	FNA3D_BLEND_SOURCEALPHA,
	FNA3D_BLEND_INVERSESOURCEALPHA,
	FNA3D_BLEND_DESTINATIONCOLOR,
	FNA3D_BLEND_INVERSEDESTINATIONCOLOR,
	FNA3D_BLEND_DESTINATIONALPHA,
	FNA3D_BLEND_INVERSEDESTINATIONALPHA,
	FNA3D_BLEND_BLENDFACTOR,
	FNA3D_BLEND_INVERSEBLENDFACTOR,
	FNA3D_BLEND_SOURCEALPHASATURATION
} FNA3D_Blend;

typedef enum FNA3D_BlendFunction
{
	FNA3D_BLENDFUNCTION_ADD,
	FNA3D_BLENDFUNCTION_SUBTRACT,
	FNA3D_BLENDFUNCTION_REVERSESUBTRACT,
	FNA3D_BLENDFUNCTION_MAX,
	FNA3D_BLENDFUNCTION_MIN
} FNA3D_BlendFunction;

typedef enum FNA3D_ColorWriteChannels
{
	FNA3D_COLORWRITECHANNELS_NONE	= 0,
	FNA3D_COLORWRITECHANNELS_RED	= 1,
	FNA3D_COLORWRITECHANNELS_GREEN	= 2,
	FNA3D_COLORWRITECHANNELS_BLUE	= 4,
	FNA3D_COLORWRITECHANNELS_ALPHA	= 8,
	FNA3D_COLORWRITECHANNELS_ALL	= 15
} FNA3D_ColorWriteChannels;

typedef enum FNA3D_StencilOperation
{
	FNA3D_STENCILOPERATION_KEEP,
	FNA3D_STENCILOPERATION_ZERO,
	FNA3D_STENCILOPERATION_REPLACE,
	FNA3D_STENCILOPERATION_INCREMENT,
	FNA3D_STENCILOPERATION_DECREMENT,
	FNA3D_STENCILOPERATION_INCREMENTSATURATION,
	FNA3D_STENCILOPERATION_DECREMENTSATURATION,
	FNA3D_STENCILOPERATION_INVERT
} FNA3D_StencilOperation;

typedef enum FNA3D_CompareFunction
{
	FNA3D_COMPAREFUNCTION_ALWAYS,
	FNA3D_COMPAREFUNCTION_NEVER,
	FNA3D_COMPAREFUNCTION_LESS,
	FNA3D_COMPAREFUNCTION_LESSEQUAL,
	FNA3D_COMPAREFUNCTION_EQUAL,
	FNA3D_COMPAREFUNCTION_GREATEREQUAL,
	FNA3D_COMPAREFUNCTION_GREATER,
	FNA3D_COMPAREFUNCTION_NOTEQUAL
} FNA3D_CompareFunction;

typedef enum FNA3D_CullMode
{
	FNA3D_CULLMODE_NONE,
	FNA3D_CULLMODE_CULLCLOCKWISEFACE,
	FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE
} FNA3D_CullMode;

typedef enum FNA3D_FillMode
{
	FNA3D_FILLMODE_SOLID,
	FNA3D_FILLMODE_WIREFRAME
} FNA3D_FillMode;

typedef enum FNA3D_TextureAddressMode
{
	FNA3D_TEXTUREADDRESSMODE_WRAP,
	FNA3D_TEXTUREADDRESSMODE_CLAMP,
	FNA3D_TEXTUREADDRESSMODE_MIRROR
} FNA3D_TextureAddressMode;

typedef enum FNA3D_TextureFilter
{
	FNA3D_TEXTUREFILTER_LINEAR,
	FNA3D_TEXTUREFILTER_POINT,
	FNA3D_TEXTUREFILTER_ANISOTROPIC,
	FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT,
	FNA3D_TEXTUREFILTER_POINT_MIPLINEAR,
	FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR,
	FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT,
	FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR,
	FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT
} FNA3D_TextureFilter;

typedef enum FNA3D_VertexElementFormat
{
	FNA3D_VERTEXELEMENTFORMAT_SINGLE,
	FNA3D_VERTEXELEMENTFORMAT_VECTOR2,
	FNA3D_VERTEXELEMENTFORMAT_VECTOR3,
	FNA3D_VERTEXELEMENTFORMAT_VECTOR4,
	FNA3D_VERTEXELEMENTFORMAT_COLOR,
	FNA3D_VERTEXELEMENTFORMAT_BYTE4,
	FNA3D_VERTEXELEMENTFORMAT_SHORT2,
	FNA3D_VERTEXELEMENTFORMAT_SHORT4,
	FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2,
	FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4,
	FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR2,
	FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4
} FNA3D_VertexElementFormat;

typedef enum FNA3D_VertexElementUsage
{
	FNA3D_VERTEXELEMENTUSAGE_POSITION,
	FNA3D_VERTEXELEMENTUSAGE_COLOR,
	FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE,
	FNA3D_VERTEXELEMENTUSAGE_NORMAL,
	FNA3D_VERTEXELEMENTUSAGE_BINORMAL,
	FNA3D_VERTEXELEMENTUSAGE_TANGENT,
	FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES,
	FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT,
	FNA3D_VERTEXELEMENTUSAGE_DEPTH,
	FNA3D_VERTEXELEMENTUSAGE_FOG,
	FNA3D_VERTEXELEMENTUSAGE_POINTSIZE,
	FNA3D_VERTEXELEMENTUSAGE_SAMPLE,
	FNA3D_VERTEXELEMENTUSAGE_TESSELATEFACTOR
} FNA3D_VertexElementUsage;

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
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	float minDepth;
	float maxDepth;
} FNA3D_Viewport;

typedef struct FNA3D_BlendState
{
	FNA3D_Blend colorSourceBlend;
	FNA3D_Blend colorDestinationBlend;
	FNA3D_BlendFunction colorBlendFunction;
	FNA3D_Blend alphaSourceBlend;
	FNA3D_Blend alphaDestinationBlend;
	FNA3D_BlendFunction alphaBlendFunction;
	FNA3D_ColorWriteChannels colorWriteEnable;
	FNA3D_ColorWriteChannels colorWriteEnable1;
	FNA3D_ColorWriteChannels colorWriteEnable2;
	FNA3D_ColorWriteChannels colorWriteEnable3;
	FNA3D_Color blendFactor;
	int32_t multiSampleMask;
} FNA3D_BlendState;

typedef struct FNA3D_DepthStencilState
{
	uint8_t depthBufferEnable;
	uint8_t depthBufferWriteEnable;
	FNA3D_CompareFunction depthBufferFunction;
	uint8_t stencilEnable;
	int32_t stencilMask;
	int32_t stencilWriteMask;
	uint8_t twoSidedStencilMode;
	FNA3D_StencilOperation stencilFail;
	FNA3D_StencilOperation stencilDepthBufferFail;
	FNA3D_StencilOperation stencilPass;
	FNA3D_CompareFunction stencilFunction;
	FNA3D_StencilOperation ccwStencilFail;
	FNA3D_StencilOperation ccwStencilDepthBufferFail;
	FNA3D_StencilOperation ccwStencilPass;
	FNA3D_CompareFunction ccwStencilFunction;
	int32_t referenceStencil;
} FNA3D_DepthStencilState;

typedef struct FNA3D_RasterizerState
{
	FNA3D_FillMode fillMode;
	FNA3D_CullMode cullMode;
	float depthBias;
	float slopeScaleDepthBias;
	uint8_t scissorTestEnable;
	uint8_t multiSampleAntiAlias;
} FNA3D_RasterizerState;

typedef struct FNA3D_SamplerState
{
	FNA3D_TextureFilter filter;
	FNA3D_TextureAddressMode addressU;
	FNA3D_TextureAddressMode addressV;
	FNA3D_TextureAddressMode addressW;
	float mipMapLevelOfDetailBias;
	int32_t maxAnisotropy;
	int32_t maxMipLevel;
} FNA3D_SamplerState;

typedef struct FNA3D_VertexElement
{
	int32_t offset;
	FNA3D_VertexElementFormat vertexElementFormat;
	FNA3D_VertexElementUsage vertexElementUsage;
	int32_t usageIndex;
} FNA3D_VertexElement;

typedef struct FNA3D_VertexDeclaration
{
	int32_t vertexStride;
	int32_t elementCount;
	FNA3D_VertexElement *elements;
} FNA3D_VertexDeclaration;

typedef struct FNA3D_VertexBufferBinding
{
	FNA3D_Buffer *vertexBuffer;
	FNA3D_VertexDeclaration vertexDeclaration;
	int32_t vertexOffset;
	int32_t instanceFrequency;
} FNA3D_VertexBufferBinding;

typedef struct FNA3D_PresentationParameters
{
	int32_t backBufferWidth;
	int32_t backBufferHeight;
	FNA3D_SurfaceFormat backBufferFormat;
	int32_t multiSampleCount;
	void* deviceWindowHandle;
	uint8_t isFullScreen;
	FNA3D_DepthFormat depthStencilFormat;
	FNA3D_PresentInterval presentationInterval;
	FNA3D_DisplayOrientation displayOrientation;
	FNA3D_RenderTargetUsage renderTargetUsage;
} FNA3D_PresentationParameters;

typedef struct FNA3D_RenderTargetBinding
{
	/* FNA3D */
	#define RENDERTARGET_TYPE_2D 0
	#define RENDERTARGET_TYPE_CUBE 1
	uint8_t type;

	/* Texture */
	FNA3D_SurfaceFormat format;
	int32_t levelCount;
	FNA3D_Texture *texture;

	/* IRenderTarget */
	int32_t width;
	int32_t height;
	FNA3D_RenderTargetUsage renderTargetUsage;
	FNA3D_Renderbuffer *colorBuffer;
	FNA3D_DepthFormat depthStencilFormat;
	int32_t multiSampleCount;

	/* RenderTargetBinding */
	FNA3D_CubeMapFace cubeMapFace;
} FNA3D_RenderTargetBinding;

/* Functions */

/* Logging */

typedef void (FNA3DCALL * FNA3D_LogFunc)(const char *msg);

FNA3DAPI void FNA3D_HookLogFunctions(
	FNA3D_LogFunc info,
	FNA3D_LogFunc warn,
	FNA3D_LogFunc error
);

/* Init/Quit */

/* This should be called before window creation!
 * Returns an SDL_WindowFlags mask.
 */
FNA3DAPI uint32_t FNA3D_PrepareWindowAttributes();

/* This should be called after window creation!
 * Use this for detecting high-DPI windows.
 */
FNA3DAPI void FNA3D_GetDrawableSize(void* window, int32_t *x, int32_t *y);

FNA3DAPI FNA3D_Device* FNA3D_CreateDevice(
	FNA3D_PresentationParameters *presentationParameters,
	uint8_t debugMode
);

FNA3DAPI void FNA3D_DestroyDevice(FNA3D_Device *device);

/* Begin/End Frame */

FNA3DAPI void FNA3D_BeginFrame(FNA3D_Device *device);

FNA3DAPI void FNA3D_SwapBuffers(
	FNA3D_Device *device,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
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
FNA3DAPI void FNA3D_VerifySampler(
	FNA3D_Device *device,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
);

/* Vertex State */

FNA3DAPI void FNA3D_ApplyVertexBufferBindings(
	FNA3D_Device *device,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
);

FNA3DAPI void FNA3D_ApplyVertexDeclaration(
	FNA3D_Device *device,
	FNA3D_VertexDeclaration *vertexDeclaration,
	void* ptr,
	int32_t vertexOffset
);

/* Render Targets */

FNA3DAPI void FNA3D_SetRenderTargets(
	FNA3D_Device *device,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *renderbuffer,
	FNA3D_DepthFormat depthFormat
);

FNA3DAPI void FNA3D_ResolveTarget(
	FNA3D_Device *device,
	FNA3D_RenderTargetBinding *target
);

/* Backbuffer Functions */

FNA3DAPI void FNA3D_ResetBackbuffer(
	FNA3D_Device *device,
	FNA3D_PresentationParameters *presentationParameters
);

FNA3DAPI void FNA3D_ReadBackbuffer(
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
);

FNA3DAPI void FNA3D_GetBackbufferSize(
	FNA3D_Device *device,
	int32_t *w,
	int32_t *h
);

FNA3DAPI FNA3D_SurfaceFormat FNA3D_GetBackbufferSurfaceFormat(
	FNA3D_Device *device
);

FNA3DAPI FNA3D_DepthFormat FNA3D_GetBackbufferDepthFormat(FNA3D_Device *device);

FNA3DAPI int32_t FNA3D_GetBackbufferMultiSampleCount(FNA3D_Device *device);

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
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	FNA3D_CubeMapFace cubeMapFace,
	int32_t level,
	void* data,
	int32_t dataLength
);
FNA3DAPI void FNA3D_SetTextureDataYUV(
	FNA3D_Device *device,
	FNA3D_Texture *y,
	FNA3D_Texture *u,
	FNA3D_Texture *v,
	int32_t w,
	int32_t h,
	void* ptr
);
FNA3DAPI void FNA3D_GetTextureData2D(
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

#ifndef _INCL_MOJOSHADER_H_
typedef struct MOJOSHADER_effect MOJOSHADER_effect;
typedef struct MOJOSHADER_effectTechnique MOJOSHADER_effectTechnique;
typedef struct MOJOSHADER_effectStateChanges MOJOSHADER_effectStateChanges;
#endif /* _INCL_MOJOSHADER_H_ */

FNA3DAPI void FNA3D_CreateEffect(
	FNA3D_Device *device,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
);
FNA3DAPI void FNA3D_CloneEffect(
	FNA3D_Device *device,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
);
FNA3DAPI void FNA3D_AddDisposeEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);
FNA3DAPI void FNA3D_SetEffectTechnique(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
);
FNA3DAPI void FNA3D_ApplyEffect(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
);
FNA3DAPI void FNA3D_BeginPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
);
FNA3DAPI void FNA3D_EndPassRestore(
	FNA3D_Device *device,
	FNA3D_Effect *effect
);

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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FNA3D_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
