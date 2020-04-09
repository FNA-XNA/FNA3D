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

#include "FNA3D_Driver.h"
#include "FNA3D_CommandStream.h"

void FNA3D_ExecuteCommand(
	FNA3D_Device *device,
	FNA3D_Command *cmd
) {
	switch (cmd->type)
	{
		case FNA3D_COMMAND_CREATEEFFECT:
			FNA3D_CreateEffect(
				device,
				cmd->createEffect.effectCode,
				cmd->createEffect.effectCodeLength,
				cmd->createEffect.effect,
				cmd->createEffect.effectData
			);
			break;
		case FNA3D_COMMAND_CLONEEFFECT:
			FNA3D_CloneEffect(
				device,
				cmd->cloneEffect.cloneSource,
				cmd->cloneEffect.effect,
				cmd->cloneEffect.effectData
			);
			break;
		case FNA3D_COMMAND_GENVERTEXBUFFER:
			cmd->genVertexBuffer.retval = FNA3D_GenVertexBuffer(
				device,
				cmd->genVertexBuffer.dynamic,
				cmd->genVertexBuffer.usage,
				cmd->genVertexBuffer.vertexCount,
				cmd->genVertexBuffer.vertexStride
			);
			break;
		case FNA3D_COMMAND_GENINDEXBUFFER:
			cmd->genIndexBuffer.retval = FNA3D_GenIndexBuffer(
				device,
				cmd->genIndexBuffer.dynamic,
				cmd->genIndexBuffer.usage,
				cmd->genIndexBuffer.indexCount,
				cmd->genIndexBuffer.indexElementSize
			);
			break;
		case FNA3D_COMMAND_SETVERTEXBUFFERDATA:
			FNA3D_SetVertexBufferData(
				device,
				cmd->setVertexBufferData.buffer,
				cmd->setVertexBufferData.offsetInBytes,
				cmd->setVertexBufferData.data,
				cmd->setVertexBufferData.dataLength,
				cmd->setVertexBufferData.options
			);
			break;
		case FNA3D_COMMAND_SETINDEXBUFFERDATA:
			FNA3D_SetIndexBufferData(
				device,
				cmd->setIndexBufferData.buffer,
				cmd->setIndexBufferData.offsetInBytes,
				cmd->setIndexBufferData.data,
				cmd->setIndexBufferData.dataLength,
				cmd->setIndexBufferData.options
			);
			break;
		case FNA3D_COMMAND_GETVERTEXBUFFERDATA:
			FNA3D_GetVertexBufferData(
				device,
				cmd->getVertexBufferData.buffer,
				cmd->getVertexBufferData.offsetInBytes,
				cmd->getVertexBufferData.data,
				cmd->getVertexBufferData.startIndex,
				cmd->getVertexBufferData.elementCount,
				cmd->getVertexBufferData.elementSizeInBytes,
				cmd->getVertexBufferData.vertexStride
			);
			break;
		case FNA3D_COMMAND_GETINDEXBUFFERDATA:
			FNA3D_GetIndexBufferData(
				device,
				cmd->getIndexBufferData.buffer,
				cmd->getIndexBufferData.offsetInBytes,
				cmd->getIndexBufferData.data,
				cmd->getIndexBufferData.dataLength
			);
			break;
		case FNA3D_COMMAND_CREATETEXTURE2D:
			cmd->createTexture2D.retval = FNA3D_CreateTexture2D(
				device,
				cmd->createTexture2D.format,
				cmd->createTexture2D.width,
				cmd->createTexture2D.height,
				cmd->createTexture2D.levelCount,
				cmd->createTexture2D.isRenderTarget
			);
			break;
		case FNA3D_COMMAND_CREATETEXTURE3D:
			cmd->createTexture3D.retval = FNA3D_CreateTexture3D(
				device,
				cmd->createTexture3D.format,
				cmd->createTexture3D.width,
				cmd->createTexture3D.height,
				cmd->createTexture3D.depth,
				cmd->createTexture3D.levelCount
			);
			break;
		case FNA3D_COMMAND_CREATETEXTURECUBE:
			cmd->createTextureCube.retval = FNA3D_CreateTextureCube(
				device,
				cmd->createTextureCube.format,
				cmd->createTextureCube.size,
				cmd->createTextureCube.levelCount,
				cmd->createTextureCube.isRenderTarget
			);
			break;
		case FNA3D_COMMAND_SETTEXTUREDATA2D:
			FNA3D_SetTextureData2D(
				device,
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
		case FNA3D_COMMAND_SETTEXTUREDATA3D:
			FNA3D_SetTextureData3D(
				device,
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
		case FNA3D_COMMAND_SETTEXTUREDATACUBE:
			FNA3D_SetTextureDataCube(
				device,
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
		case FNA3D_COMMAND_GETTEXTUREDATA2D:
			FNA3D_GetTextureData2D(
				device,
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
		case FNA3D_COMMAND_GETTEXTUREDATA3D:
			FNA3D_GetTextureData3D(
				device,
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
		case FNA3D_COMMAND_GETTEXTUREDATACUBE:
			FNA3D_GetTextureDataCube(
				device,
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
		case FNA3D_COMMAND_GENCOLORRENDERBUFFER:
			cmd->genColorRenderbuffer.retval = FNA3D_GenColorRenderbuffer(
				device,
				cmd->genColorRenderbuffer.width,
				cmd->genColorRenderbuffer.height,
				cmd->genColorRenderbuffer.format,
				cmd->genColorRenderbuffer.multiSampleCount,
				cmd->genColorRenderbuffer.texture
			);
			break;
		case FNA3D_COMMAND_GENDEPTHRENDERBUFFER:
			cmd->genDepthStencilRenderbuffer.retval = FNA3D_GenDepthStencilRenderbuffer(
				device,
				cmd->genDepthStencilRenderbuffer.width,
				cmd->genDepthStencilRenderbuffer.height,
				cmd->genDepthStencilRenderbuffer.format,
				cmd->genDepthStencilRenderbuffer.multiSampleCount
			);
			break;
		default:
			FNA3D_LogError(
				"Cannot execute unknown command (value = %d)",
				cmd->type
			);
			break;
	}
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
