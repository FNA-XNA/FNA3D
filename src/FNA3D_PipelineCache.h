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

#ifndef FNA3D_PIPELINECACHE_H
#define FNA3D_PIPELINECACHE_H

#include "FNA3D.h"

typedef struct StateHash
{
	uint64_t a;
	uint64_t b;
} StateHash;

typedef struct StateHashMap
{
	StateHash key;
	void* value;
} StateHashMap;

typedef struct UInt64HashMap
{
	uint64_t key;
	void* value;
} UInt64HashMap;

StateHash GetBlendStateHash(FNA3D_BlendState blendState);
StateHash GetDepthStencilStateHash(FNA3D_DepthStencilState dsState);
StateHash GetRasterizerStateHash(FNA3D_RasterizerState rastState, float bias);
StateHash GetSamplerStateHash(FNA3D_SamplerState samplerState);
uint64_t GetVertexDeclarationHash(
	FNA3D_VertexDeclaration declaration,
	void* vertexShader
);
uint64_t GetVertexBufferBindingsHash(
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	void* vertexShader
);

#endif /* FNA3D_PIPELINECACHE_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
