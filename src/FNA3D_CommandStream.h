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

#ifndef FNA3D_COMMANDSTREAM_H
#define FNA3D_COMMANDSTREAM_H

#include "FNA3D.h"
#include <SDL.h>

typedef struct FNA3D_Command FNA3D_Command;
struct FNA3D_Command
{
	#define FNA3D_COMMAND_CREATEEFFECT 0
	#define FNA3D_COMMAND_CLONEEFFECT 1
	#define FNA3D_COMMAND_GENVERTEXBUFFER 2
	#define FNA3D_COMMAND_GENINDEXBUFFER 3
	#define FNA3D_COMMAND_SETVERTEXBUFFERDATA 4
	#define FNA3D_COMMAND_SETINDEXBUFFERDATA 5
	#define FNA3D_COMMAND_GETVERTEXBUFFERDATA 6
	#define FNA3D_COMMAND_GETINDEXBUFFERDATA 7
	#define FNA3D_COMMAND_CREATETEXTURE2D 8
	#define FNA3D_COMMAND_CREATETEXTURE3D 9
	#define FNA3D_COMMAND_CREATETEXTURECUBE 10
	#define FNA3D_COMMAND_SETTEXTUREDATA2D 11
	#define FNA3D_COMMAND_SETTEXTUREDATA3D 12
	#define FNA3D_COMMAND_SETTEXTUREDATACUBE 13
	#define FNA3D_COMMAND_GETTEXTUREDATA2D 14
	#define FNA3D_COMMAND_GETTEXTUREDATA3D 15
	#define FNA3D_COMMAND_GETTEXTUREDATACUBE 16
	#define FNA3D_COMMAND_GENCOLORRENDERBUFFER 17
	#define FNA3D_COMMAND_GENDEPTHRENDERBUFFER 18
	uint8_t type;
	FNA3DNAMELESS union
	{
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
			uint8_t dynamic;
			FNA3D_BufferUsage usage;
			int32_t vertexCount;
			int32_t vertexStride;
			FNA3D_Buffer *retval;
		} genVertexBuffer;

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
			int32_t offsetInBytes;
			void* data;
			int32_t elementCount;
			int32_t elementSizeInBytes;
			int32_t vertexStride;
			FNA3D_SetDataOptions options;
		} setVertexBufferData;

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
			int32_t elementCount;
			int32_t elementSizeInBytes;
			int32_t vertexStride;
		} getVertexBufferData;

		struct
		{
			FNA3D_Buffer *buffer;
			int32_t offsetInBytes;
			void* data;
			int32_t dataLength;
		} getIndexBufferData;

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
			int32_t x;
			int32_t y;
			int32_t z;
			int32_t w;
			int32_t h;
			int32_t d;
			int32_t level;
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
			int32_t x;
			int32_t y;
			int32_t w;
			int32_t h;
			int32_t level;
			void* data;
			int32_t dataLength;
		} getTextureData2D;

		struct
		{
			FNA3D_Texture *texture;
			FNA3D_SurfaceFormat format;
			int32_t x;
			int32_t y;
			int32_t z;
			int32_t w;
			int32_t h;
			int32_t d;
			int32_t level;
			void* data;
			int32_t dataLength;
		} getTextureData3D;

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
	};
	SDL_sem *semaphore;
	FNA3D_Command *next;
};

void FNA3D_ExecuteCommand(
	FNA3D_Device *device,
	FNA3D_Command *cmd
);

#endif /* FNA3D_COMMANDSTREAM_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
