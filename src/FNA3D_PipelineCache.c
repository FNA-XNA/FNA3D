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

#include "FNA3D_PipelineCache.h"

#include <SDL.h>

/* Packed Pipeline States */

PackedState GetPackedBlendState(FNA3D_BlendState blendState)
{
	PackedState result;
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

PackedState GetPackedDepthStencilState(FNA3D_DepthStencilState dsState)
{
	PackedState result;
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

#define FLOAT_TO_UINT64(f) (uint64_t) *((uint32_t*) &f)

PackedState GetPackedRasterizerState(FNA3D_RasterizerState rastState, float bias)
{
	PackedState result;
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

PackedState GetPackedSamplerState(FNA3D_SamplerState samplerState)
{
	PackedState result;
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

void* PackedStateArray_Fetch(PackedStateArray *arr, PackedState key)
{
	int32_t i;

	for (i = 0; i < arr->count; i += 1)
	{
		if (	key.a == arr->elements[i].key.a &&
			key.b == arr->elements[i].key.b		)
		{
			return arr->elements[i].value;
		}
	}

	return NULL;
}

void PackedStateArray_Insert(PackedStateArray *arr, PackedState key, void* value)
{
	PackedStateMap map =
	{
		{ key.a, key.b }, /* FIXME: Why can't VS2010 compile with just "key"? */
		value
	};

	EXPAND_ARRAY_IF_NEEDED(arr, 4, PackedStateMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* Vertex Buffer Bindings */

static inline uint32_t GetPackedVertexElement(FNA3D_VertexElement element)
{
	/* FIXME: Backport this to FNA! */

	/* Technically element.offset is an int32, but geez,
	 * if you're using more than 2^20 bytes, you've got
	 * bigger problems to worry about.
	 * -caleb
	 */
	return (
		  element.offset << 12
		| element.vertexElementFormat << 8
		| element.vertexElementUsage << 4
		| element.usageIndex
	);
}

static uint32_t HashVertexDeclarations(
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings
) {
	int32_t i, j;
	uint32_t hash = 0;

	/* The algorithm for this hashing function
	 * is taken from Josh Bloch's "Effective Java".
	 * (https://stackoverflow.com/a/113600/12492383)
	 */
	const uint32_t HASH_FACTOR = 39;
	for (i = 0; i < numBindings; i += 1)
	{
		for (j = 0; j < bindings[i].vertexDeclaration.elementCount; j += 1)
		{
			hash *= HASH_FACTOR;
			hash += GetPackedVertexElement(bindings[i].vertexDeclaration.elements[j]);
		}
	}

	return hash;
}

void* PackedVertexBufferBindingsArray_Fetch(
	PackedVertexBufferBindingsArray *arr,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	void* vertexShader,
	int32_t *outIndex
) {
	int32_t i, j;
	uint8_t mismatch;
	PackedVertexBufferBindings other;
	uint32_t declHash = HashVertexDeclarations(bindings, numBindings);

	for (i = 0; i < arr->count; i += 1)
	{
		other = arr->elements[i].key;

		if (	numBindings != other.numBindings ||
			vertexShader != other.vertexShader ||
			declHash != other.vertexDeclarationHash	)
		{
			continue;
		}

		mismatch = 0;
		for (j = 0; j < numBindings; j += 1)
		{
			if (	bindings[j].vertexBuffer != other.buffer[j] ||
				bindings[j].instanceFrequency != other.instanceFrequency[j]	)
			{
				mismatch = 1;
				break;
			}
		}
		if (!mismatch)
		{
			*outIndex = i;
			return arr->elements[i].value;
		}
	}

	*outIndex = i;
	return NULL;
}

void PackedVertexBufferBindingsArray_Insert(
	PackedVertexBufferBindingsArray *arr,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	void* vertexShader,
	void* value
) {
	PackedVertexBufferBindingsMap map = {0};
	int32_t i;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, PackedVertexBufferBindingsMap)

	/* Fill in the map */
	map.key.numBindings = numBindings;
	for (i = 0; i < numBindings; i += 1)
	{
		map.key.buffer[i] = bindings[i].vertexBuffer;
		map.key.instanceFrequency[i] = bindings[i].instanceFrequency;
	}
	map.key.vertexShader = vertexShader;
	map.key.vertexDeclarationHash = HashVertexDeclarations(bindings, numBindings);
	map.value = value;

	arr->elements[arr->count] = map;
	arr->count += 1;
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
