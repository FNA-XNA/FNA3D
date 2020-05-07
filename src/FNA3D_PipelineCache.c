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
#include "FNA3D_PipelineCache.h"

#include <SDL.h>

/* STB_DS Implementation */

#define strcmp SDL_strcmp
#define strlen SDL_strlen
#ifdef memcmp
#undef memcmp
#endif
#define memcmp SDL_memcmp
#ifdef memcpy
#undef memcpy
#endif
#define memcpy SDL_memcpy
#ifdef memmove
#undef memmove
#endif
#define memmove SDL_memmove
#ifdef memset
#undef memset
#endif
#define memset SDL_memset
#define STBDS_ASSERT(x) SDL_assert(x)
#define STBDS_REALLOC(c,p,s) SDL_realloc(p,s)
#define STBDS_FREE(c,p) SDL_free(p)
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

/* State Hashing */

#define FLOAT_TO_UINT64(f) (uint64_t) *((uint32_t*) &f)

StateHash GetBlendStateHash(FNA3D_BlendState blendState)
{
	StateHash result;
	int32_t funcs = (
		  blendState.alphaBlendFunction << 4
		| blendState.colorBlendFunction << 0
	);
	int32_t blendsAndColorWritesChannels = (
		  blendState.alphaDestinationBlend << 28
		| blendState.alphaSourceBlend << 24
		| blendState.colorDestinationBlend << 20
		| blendState.colorSourceBlend << 16
		| blendState.colorWriteEnable << 12
		| blendState.colorWriteEnable1 << 8
		| blendState.colorWriteEnable2 << 4
		| blendState.colorWriteEnable3 << 0
	);
	int32_t blendFactorPacked = (
		  blendState.blendFactor.r << 0
		| blendState.blendFactor.g << 8
		| blendState.blendFactor.b << 16
		| blendState.blendFactor.a << 24
	);
	result.a = (
		(uint64_t) funcs << 32 |
		(uint64_t) blendsAndColorWritesChannels
	);
	result.b = (
		(uint64_t) blendState.multiSampleMask << 32 |
		(uint64_t) blendFactorPacked
	);
	return result;
}

StateHash GetDepthStencilStateHash(FNA3D_DepthStencilState dsState)
{
	StateHash result;
	int32_t packedProperties = (
		  dsState.depthBufferEnable << 30
		| dsState.depthBufferWriteEnable << 29
		| dsState.stencilEnable << 28
		| dsState.twoSidedStencilMode << 27
		| dsState.depthBufferFunction << 24
		| dsState.stencilFunction << 21
		| dsState.ccwStencilFunction << 18
		| dsState.stencilPass << 15
		| dsState.stencilFail << 12
		| dsState.stencilDepthBufferFail << 9
		| dsState.ccwStencilPass << 6
		| dsState.ccwStencilFail << 3
		| dsState.ccwStencilDepthBufferFail
	);
	result.a = (
		(uint64_t) dsState.stencilMask << 32 |
		(uint64_t) packedProperties
	);
	result.b = (
		(uint64_t) dsState.referenceStencil << 32 |
		(uint64_t) dsState.stencilWriteMask
	);
	return result;
}

StateHash GetRasterizerStateHash(FNA3D_RasterizerState rastState, float bias)
{
	StateHash result;
	int32_t packedProperties = (
		  rastState.multiSampleAntiAlias << 4
		| rastState.scissorTestEnable << 3
		| rastState.fillMode << 2
		| rastState.cullMode
	);
	result.a = (uint64_t) packedProperties;
	result.b = (
		FLOAT_TO_UINT64(rastState.slopeScaleDepthBias) << 32 |
		FLOAT_TO_UINT64(bias)
	);
	return result;
}

StateHash GetSamplerStateHash(FNA3D_SamplerState samplerState)
{
	StateHash result;
	int32_t packedProperties = (
		  samplerState.filter << 6
		| samplerState.addressU << 4
		| samplerState.addressV << 2
		| samplerState.addressW
	);
	result.a = (
		(uint64_t) samplerState.maxAnisotropy << 32 |
		(uint64_t) packedProperties
	);
	result.b = (
		(uint64_t) samplerState.maxMipLevel << 32 |
		FLOAT_TO_UINT64(samplerState.mipMapLevelOfDetailBias)
	);
	return result;
}

#undef FLOAT_TO_UINT64

/* Vertex Declaration Hashing */

/* The algorithm for these hashing functions
 * is taken from Josh Bloch's "Effective Java".
 * (https://stackoverflow.com/a/113600/12492383)
 *
 * FIXME: Is there a better way to hash this?
 * -caleb
 */

#define HASH_FACTOR 39

static uint64_t GetVertexElementHash(FNA3D_VertexElement element)
{
	/* FIXME: Backport this to FNA! -caleb */
	return (
		  (uint64_t) element.offset << 32
		| (uint64_t) element.vertexElementFormat << 8
		| (uint64_t) element.vertexElementUsage << 4
		| (uint64_t) element.usageIndex
	);
}

uint64_t GetVertexDeclarationHash(
	FNA3D_VertexDeclaration declaration,
	void* vertexShader
) {
	uint64_t result = (uint64_t) (size_t) vertexShader;
	int32_t i;
	for (i = 0; i < declaration.elementCount; i += 1)
	{
		result = result * HASH_FACTOR + (
			GetVertexElementHash(declaration.elements[i])
		);
	}
	result = result * HASH_FACTOR + (
		(uint64_t) declaration.vertexStride
	);
	return result;
}

uint64_t GetVertexBufferBindingsHash(
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	void* vertexShader
) {
	uint64_t result = (uint64_t) (size_t) vertexShader;
	int32_t i;
	for (i = 0; i < numBindings; i += 1)
	{
		result = result * HASH_FACTOR + (
			(uint64_t) bindings[i].instanceFrequency
		);
		result = result * HASH_FACTOR + GetVertexDeclarationHash(
			bindings[i].vertexDeclaration,
			vertexShader
		);
	}
	return result;
}

#undef HASH_FACTOR

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
