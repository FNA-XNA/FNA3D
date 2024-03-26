/* FNA3D - 3D Graphics Library for FNA
 *
 * Copyright (c) 2020-2024 Ethan Lee
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

#if FNA3D_DRIVER_SDL

#include <SDL3/SDL_gpu.h>
#include <SDL3_shader/SDL_shader.h>

/* Mojoshader interop */

/* TODO: actually move this to mojoshader */
#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

/* Max entries for each register file type */
#define MAX_REG_FILE_F 8192
#define MAX_REG_FILE_I 2047
#define MAX_REG_FILE_B 2047

typedef struct MOJOSHADER_sdlContext MOJOSHADER_sdlContext;
typedef struct MOJOSHADER_sdlShader MOJOSHADER_sdlShader;
typedef struct MOJOSHADER_sdlProgram MOJOSHADER_sdlProgram;

struct MOJOSHADER_sdlContext
{
    SDL_GpuDevice *device;
    SDL_GpuBackend backend;
    const char *profile;

    SDL_GpuCommandBuffer *commandBuffer;

    MOJOSHADER_malloc malloc_fn;
    MOJOSHADER_free free_fn;
    void *malloc_data;

    /* The constant register files...
     * !!! FIXME: Man, it kills me how much memory this takes...
     * !!! FIXME:  ... make this dynamically allocated on demand.
     */
    float vs_reg_file_f[MAX_REG_FILE_F * 4];
    int32_t vs_reg_file_i[MAX_REG_FILE_I * 4];
    uint8_t vs_reg_file_b[MAX_REG_FILE_B * 4];
    float ps_reg_file_f[MAX_REG_FILE_F * 4];
    int32_t ps_reg_file_i[MAX_REG_FILE_I * 4];
    uint8_t ps_reg_file_b[MAX_REG_FILE_B * 4];

    MOJOSHADER_sdlProgram *bound_program;
    HashTable *linker_cache;

    /*
     * Note that these may not necessarily align with bound_program!
     * We need to store these so effects can have overlapping shaders.
     */
    MOJOSHADER_sdlShader *vertex_shader;
    MOJOSHADER_sdlShader *pixel_shader;

    uint8_t vertex_shader_needs_bind;
    uint8_t pixel_shader_needs_bind;
};

struct MOJOSHADER_sdlShader
{
    const MOJOSHADER_parseData *parseData;
    uint16_t tag;
    uint32_t refcount;
};

struct MOJOSHADER_sdlProgram
{
    SDL_GpuShaderModule *vertexModule;
    SDL_GpuShaderModule *pixelModule;
    MOJOSHADER_sdlShader *vertexShader;
    MOJOSHADER_sdlShader *pixelShader;
};

typedef struct BoundShaders
{
    MOJOSHADER_sdlShader *vertex;
    MOJOSHADER_sdlShader *fragment;
} BoundShaders;

/* Error state... */
static char error_buffer[1024] = { '\0' };

static void set_error(const char *str)
{
    snprintf(error_buffer, sizeof (error_buffer), "%s", str);
}

static inline void out_of_memory(void)
{
    set_error("out of memory");
}

/* Internals */

void MOJOSHADER_sdlDeleteProgram(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlProgram *p
) {
    if (p->vertexModule != NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->vertexModule);
    }
    if (p->pixelModule != NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->pixelModule);
    }
    ctx->free_fn(p, ctx->malloc_data);
}

static uint32_t hash_shaders(const void *sym, void *data)
{
    (void) data;
    const BoundShaders *s = (const BoundShaders *) sym;
    const uint16_t v = (s->vertex) ? s->vertex->tag : 0;
    const uint16_t f = (s->fragment) ? s->fragment->tag : 0;
    return ((uint32_t) v << 16) | (uint32_t) f;
} // hash_shaders

static int match_shaders(const void *_a, const void *_b, void *data)
{
    (void) data;
    const BoundShaders *a = (const BoundShaders *) _a;
    const BoundShaders *b = (const BoundShaders *) _b;

    const uint16_t av = (a->vertex) ? a->vertex->tag : 0;
    const uint16_t bv = (b->vertex) ? b->vertex->tag : 0;
    if (av != bv)
        return 0;

    const uint16_t af = (a->fragment) ? a->fragment->tag : 0;
    const uint16_t bf = (b->fragment) ? b->fragment->tag : 0;
    if (af != bf)
        return 0;

    return 1;
} // match_shaders

static void nuke_shaders(
    const void *_ctx,
    const void *key,
    const void *value,
    void *data
) {
    MOJOSHADER_sdlContext *ctx = (MOJOSHADER_sdlContext *) _ctx;
    (void) data;
    ctx->free_fn((void *) key, ctx->malloc_data); // this was a BoundShaders struct.
    MOJOSHADER_sdlDeleteProgram(ctx, (MOJOSHADER_sdlProgram *) value);
} // nuke_shaders

static void update_uniform_buffer(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *shader
) {
    int32_t i, j;
    int32_t offset;
    uint8_t *contents;
    uint32_t content_size;
    uint32_t *contentsI;
    float *regF; int *regI; uint8_t *regB;

    if (shader == NULL || shader->parseData->uniform_count == 0)
        return;

    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
    {
        regF = ctx->vs_reg_file_f;
        regI = ctx->vs_reg_file_i;
        regB = ctx->vs_reg_file_b;
    }
    else
    {
        regF = ctx->ps_reg_file_f;
        regI = ctx->ps_reg_file_i;
        regB = ctx->ps_reg_file_b;
    }
    content_size = 0;

    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        const int32_t size = arrayCount ? arrayCount : 1;
        content_size += size * 16;
    }

    contents = (uint8_t*) ctx->malloc_fn(content_size, ctx->malloc_data);

    offset = 0;
    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t index = shader->parseData->uniforms[i].index;
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        const int32_t size = arrayCount ? arrayCount : 1;

        switch (shader->parseData->uniforms[i].type)
        {
            case MOJOSHADER_UNIFORM_FLOAT:
                memcpy(
                    contents + offset,
                    &regF[4 * index],
                    size * 16
                );
                break;

            case MOJOSHADER_UNIFORM_INT:
                memcpy(
                    contents + offset,
                    &regI[4 * index],
                    size * 16
                );
                break;

            case MOJOSHADER_UNIFORM_BOOL:
                contentsI = (uint32_t *) (contents + offset);
                for (j = 0; j < size; j++)
                    contentsI[j * 4] = regB[index + j];
                break;

            default:
                set_error(
                    "SOMETHING VERY WRONG HAPPENED WHEN UPDATING UNIFORMS"
                );
                assert(0);
                break;
        }

        offset += size * 16;
    }

    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
    {
        regF = ctx->vs_reg_file_f;
        regI = ctx->vs_reg_file_i;
        regB = ctx->vs_reg_file_b;

        SDL_GpuPushVertexShaderUniforms(
            ctx->device,
            ctx->commandBuffer,
            contents,
            content_size
        );
    }
    else
    {
        regF = ctx->ps_reg_file_f;
        regI = ctx->ps_reg_file_i;
        regB = ctx->ps_reg_file_b;

        SDL_GpuPushFragmentShaderUniforms(
            ctx->device,
            ctx->commandBuffer,
            contents,
            content_size
        );
    }

    ctx->free_fn(contents, ctx->malloc_data);
}

static uint16_t shaderTagCounter = 1;

MOJOSHADER_sdlContext *MOJOSHADER_sdlCreateContext(
    SDL_GpuDevice *device,
    SDL_GpuBackend backend,
    MOJOSHADER_malloc m,
    MOJOSHADER_free f,
    void *malloc_d
) {
    MOJOSHADER_sdlContext* resultCtx;

    if (m == NULL) m = MOJOSHADER_internal_malloc;
    if (f == NULL) f = MOJOSHADER_internal_free;

    resultCtx = (MOJOSHADER_sdlContext*) m(sizeof(MOJOSHADER_sdlContext), malloc_d);
    if (resultCtx == NULL)
    {
        out_of_memory();
        goto init_fail;
    }

    SDL_memset(resultCtx, '\0', sizeof(MOJOSHADER_sdlContext));
    resultCtx->device = device;
    resultCtx->backend = backend;
    resultCtx->profile = "spirv"; /* always use spirv and interop with SDL3_shader */

    resultCtx->malloc_fn = m;
    resultCtx->free_fn = f;
    resultCtx->malloc_data = malloc_d;

    return resultCtx;

init_fail:
    if (resultCtx != NULL)
        f(resultCtx, malloc_d);
    return NULL;
}

MOJOSHADER_sdlShader *MOJOSHADER_sdlCompileShader(
    MOJOSHADER_sdlContext *ctx,
    const char *mainfn,
    const unsigned char *tokenbuf,
    const unsigned int bufsize,
    const MOJOSHADER_swizzle *swiz,
    const unsigned int swizcount,
    const MOJOSHADER_samplerMap *smap,
    const unsigned int smapcount
) {
    MOJOSHADER_sdlShader *shader;

    const MOJOSHADER_parseData *pd = MOJOSHADER_parse(
        "spirv", mainfn,
        tokenbuf, bufsize,
        swiz, swizcount,
        smap, smapcount,
        ctx->malloc_fn,
        ctx->free_fn,
        ctx->malloc_data
    );

    if (pd->error_count > 0)
    {
        set_error(pd->errors[0].error);
        goto parse_shader_fail;
    }

    shader = (MOJOSHADER_sdlShader*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlShader), ctx->malloc_data);
    if (shader == NULL)
    {
        out_of_memory();
        goto parse_shader_fail;
    }

    shader->parseData = pd;
    shader->refcount = 1;
    shader->tag = shaderTagCounter++;
    return shader;

parse_shader_fail:
    MOJOSHADER_freeParseData(pd);
    if (shader != NULL)
        ctx->free_fn(shader, ctx->malloc_data);
    return NULL;
}

MOJOSHADER_sdlProgram *MOJOSHADER_sdlLinkProgram(
    MOJOSHADER_sdlContext *ctx
) {
    MOJOSHADER_sdlProgram *result;
    MOJOSHADER_sdlShader *vshader = ctx->vertex_shader;
    MOJOSHADER_sdlShader *pshader = ctx->pixel_shader;
    const char *v_shader_source;
    const char *p_shader_source;
    uint32_t v_shader_len;
    uint32_t p_shader_len;
    const char *v_transpiled_source;
    const char *p_transpiled_source;
    size_t v_transpiled_len;
    size_t p_transpiled_len;
    SDL_GpuShaderModuleCreateInfo createInfo;

    if ((vshader == NULL) || (pshader == NULL)) /* Both shaders MUST exist! */
    {
        return NULL;
    }

    result = (MOJOSHADER_sdlProgram*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlProgram), ctx->malloc_data);

    if (result == NULL)
    {
        out_of_memory();
        return NULL;
    }

    MOJOSHADER_spirv_link_attributes(vshader->parseData, pshader->parseData);
    v_shader_source = vshader->parseData->output;
    p_shader_source = pshader->parseData->output;
    v_shader_len = vshader->parseData->output_len - sizeof(SpirvPatchTable);
    p_shader_len = pshader->parseData->output_len - sizeof(SpirvPatchTable);

    v_transpiled_source = SHD_TranslateFromSPIRV(
        ctx->backend,
        v_shader_source,
        v_shader_len,
        &v_transpiled_len
    );

    if (v_transpiled_source == NULL)
    {
        set_error("Failed to transpile vertex shader from SPIR-V!");
        return NULL;
    }

    p_transpiled_source = SHD_TranslateFromSPIRV(
        ctx->backend,
        p_shader_source,
        p_shader_len,
        &p_transpiled_len
    );

    if (p_transpiled_source == NULL)
    {
        set_error("Failed to transpile pixel shader from SPIR-V!");
        ctx->free_fn((char*) v_transpiled_source, ctx->malloc_data);
        return NULL;
    }

    createInfo.code     = v_transpiled_source;
    createInfo.codeSize = v_transpiled_len;
    createInfo.type     = SDL_GPU_SHADERTYPE_VERTEX;

    result->vertexModule = SDL_GpuCreateShaderModule(
        ctx->device,
        &createInfo
    );

    ctx->free_fn((char*) v_transpiled_source, ctx->malloc_data);

    if (result->vertexModule == NULL)
    {
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    }

    createInfo.code     = p_transpiled_source;
    createInfo.codeSize = p_transpiled_len;
    createInfo.codeSize = SDL_GPU_SHADERTYPE_FRAGMENT;

    result->pixelModule = SDL_GpuCreateShaderModule(
        ctx->device,
        &createInfo
    );

    ctx->free_fn((char*) p_transpiled_source, ctx->malloc_data);

    if (result->pixelModule == NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, result->vertexModule);
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    }

    result->vertexShader = vshader;
    result->pixelShader = pshader;

    ctx->bound_program = result;

    return result;
}

void MOJOSHADER_sdlBindShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *vshader,
    MOJOSHADER_sdlShader *pshader
) {

    if (vshader != NULL)
    {
        ctx->vertex_shader = vshader;
    }

    if (pshader != NULL)
    {
        ctx->pixel_shader = pshader;
    }
}

void MOJOSHADER_sdlGetBoundShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader **vshader,
    MOJOSHADER_sdlShader **pshader
) {
    if (vshader != NULL)
    {
        if (ctx->bound_program != NULL)
        {
            *vshader = ctx->bound_program->vertexShader;
        }
        else
        {
            *vshader = ctx->vertex_shader; /* In case a pshader isn't set yet */
        }
    }
    if (pshader != NULL)
    {
        if (ctx->bound_program != NULL)
        {
            *pshader = ctx->bound_program->pixelShader;
        }
        else
        {
            *pshader = ctx->pixel_shader; /* In case a vshader isn't set yet */
        }
    }
}

void MOJOSHADER_sdlMapUniformBufferMemory(
    MOJOSHADER_sdlContext *ctx,
    float **vsf, int **vsi, unsigned char **vsb,
    float **psf, int **psi, unsigned char **psb
) {
    *vsf = ctx->vs_reg_file_f;
    *vsi = ctx->vs_reg_file_i;
    *vsb = ctx->vs_reg_file_b;
    *psf = ctx->ps_reg_file_f;
    *psi = ctx->ps_reg_file_i;
    *psb = ctx->ps_reg_file_b;
}

void MOJOSHADER_sdlUnmapUniformBufferMemory(MOJOSHADER_sdlContext *ctx)
{
    if (ctx->bound_program == NULL)
    {
        return; /* Ignore buffer updates until we have a real program linked */
    }

    update_uniform_buffer(ctx, ctx->bound_program->vertexShader);
    update_uniform_buffer(ctx, ctx->bound_program->pixelShader);
}

void MOJOSHADER_sdlDeleteShader(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *shader
) {
    if (shader != NULL)
    {
        if (shader->refcount > 1)
            shader->refcount--;
        else
        {
            // See if this was bound as an unlinked program anywhere...
            if (ctx->linker_cache)
            {
                const void *key = NULL;
                void *iter = NULL;
                int morekeys = hash_iter_keys(ctx->linker_cache, &key, &iter);
                while (morekeys)
                {
                    const BoundShaders *shaders = (const BoundShaders *) key;
                    // Do this here so we don't confuse the iteration by removing...
                    morekeys = hash_iter_keys(ctx->linker_cache, &key, &iter);
                    if ((shaders->vertex == shader) || (shaders->fragment == shader))
                    {
                        // Deletes the linked program
                        hash_remove(ctx->linker_cache, shaders, ctx);
                    }
                }
            }

            MOJOSHADER_freeParseData(shader->parseData);
            ctx->free_fn(shader, ctx->malloc_data);
        }
    }
}

/* Returns 0 if program not already linked. */
uint8_t MOJOSHADER_sdlCheckProgramStatus(
    MOJOSHADER_sdlContext *ctx
) {
    MOJOSHADER_sdlShader *vshader = ctx->vertex_shader;
    MOJOSHADER_sdlShader *pshader = ctx->pixel_shader;

    if (ctx->linker_cache == NULL)
    {
        ctx->linker_cache = hash_create(NULL, hash_shaders, match_shaders,
                                        nuke_shaders, 0, ctx->malloc_fn,
                                        ctx->free_fn, ctx->malloc_data);

        if (ctx->linker_cache == NULL)
        {
            out_of_memory();
            return 0;
        }
    }

    BoundShaders shaders;
    shaders.vertex = vshader;
    shaders.fragment = pshader;

    const void *val = NULL;
    return hash_find(ctx->linker_cache, &shaders, &val);
}

void MOJOSHADER_sdlShaderAddRef(MOJOSHADER_sdlShader *shader)
{
    if (shader != NULL)
        shader->refcount++;
}

unsigned int MOJOSHADER_sdlGetShaderRefCount(MOJOSHADER_sdlShader *shader)
{
    if (shader != NULL)
        return shader->refcount;
    return 0;
}

const MOJOSHADER_parseData *MOJOSHADER_sdlGetShaderParseData(
    MOJOSHADER_sdlShader *shader
) {
    return (shader != NULL) ? shader->parseData : NULL;
}

/* call this before calls to MOJOSHADER_effectBeginPass! */
void MOJOSHADER_sdlSetCommandBuffer(
    MOJOSHADER_sdlContext *ctx,
    SDL_GpuCommandBuffer *commandBuffer
) {
    ctx->commandBuffer = commandBuffer;
}

const char *MOJOSHADER_sdlGetError(
    MOJOSHADER_sdlContext *ctx
) {
    return error_buffer;
}

#include "FNA3D_Driver.h"
#include "FNA3D_PipelineCache.h"

static inline SDL_GpuSampleCount XNAToSDL_SampleCount(int32_t sampleCount)
{
	if (sampleCount <= 1)
	{
		return SDL_GPU_SAMPLECOUNT_1;
	}
	else if (sampleCount == 2)
	{
		return SDL_GPU_SAMPLECOUNT_2;
	}
	else if (sampleCount <= 4)
	{
		return SDL_GPU_SAMPLECOUNT_4;
	}
	else if (sampleCount <= 8)
	{
		return SDL_GPU_SAMPLECOUNT_8;
	}
	else
	{
		FNA3D_LogWarn("Unexpected sample count: %d", sampleCount);
		return SDL_GPU_SAMPLECOUNT_1;
	}
}

static inline float XNAToSDL_DepthBiasScale(SDL_GpuTextureFormat format)
{
	switch (format)
	{
		case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
			return (float) ((1 << 16) - 1);

		case SDL_GPU_TEXTUREFORMAT_D32_SFLOAT:
		case SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT:
			return (float) ((1 << 23) - 1);

		default:
			return 0.0f;
	}
}

static inline SDL_GpuTextureFormat XNAToSDL_DepthFormat(
	FNA3D_DepthFormat format
) {
	switch (format)
	{
		case FNA3D_DEPTHFORMAT_D16:
			return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
		case FNA3D_DEPTHFORMAT_D24:
			return SDL_GPU_TEXTUREFORMAT_D32_SFLOAT;
		case FNA3D_DEPTHFORMAT_D24S8:
			return SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT;
		default:
            FNA3D_LogError("Unrecognized depth format!");
			return 0;
	}
}

/* TODO: add the relevant SRGB formats to SDL_gpu */
static SDL_GpuTextureFormat XNAToSDL_SurfaceFormat[] =
{
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8,             /* SurfaceFormat.Color */
	SDL_GPU_TEXTUREFORMAT_R5G6B5,               /* SurfaceFormat.Bgr565 */
	SDL_GPU_TEXTUREFORMAT_A1R5G5B5,             /* SurfaceFormat.Bgra5551 */
	SDL_GPU_TEXTUREFORMAT_B4G4R4A4,             /* SurfaceFormat.Bgra4444 */
	SDL_GPU_TEXTUREFORMAT_BC1,                  /* SurfaceFormat.Dxt1 */
	SDL_GPU_TEXTUREFORMAT_BC2,                  /* SurfaceFormat.Dxt3 */
	SDL_GPU_TEXTUREFORMAT_BC3,                  /* SurfaceFormat.Dxt5 */
	SDL_GPU_TEXTUREFORMAT_R8G8_SNORM,           /* SurfaceFormat.NormalizedByte2 */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,       /* SurfaceFormat.NormalizedByte4 */
	SDL_GPU_TEXTUREFORMAT_A2R10G10B10,          /* SurfaceFormat.Rgba1010102 */
	SDL_GPU_TEXTUREFORMAT_R16G16,               /* SurfaceFormat.Rg32 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16,         /* SurfaceFormat.Rgba64 */
	SDL_GPU_TEXTUREFORMAT_R8,                   /* SurfaceFormat.Alpha8 */
	SDL_GPU_TEXTUREFORMAT_R32_SFLOAT,           /* SurfaceFormat.Single */
	SDL_GPU_TEXTUREFORMAT_R32G32_SFLOAT,        /* SurfaceFormat.Vector2 */
	SDL_GPU_TEXTUREFORMAT_R32G32B32A32_SFLOAT,  /* SurfaceFormat.Vector4 */
	SDL_GPU_TEXTUREFORMAT_R16_SFLOAT,           /* SurfaceFormat.HalfSingle */
	SDL_GPU_TEXTUREFORMAT_R16G16_SFLOAT,        /* SurfaceFormat.HalfVector2 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT,  /* SurfaceFormat.HalfVector4 */
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT,  /* SurfaceFormat.HdrBlendable */
	SDL_GPU_TEXTUREFORMAT_B8G8R8A8,             /* SurfaceFormat.ColorBgraEXT */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8,             /* FIXME SRGB */ /* SurfaceFormat.ColorSrgbEXT */
	SDL_GPU_TEXTUREFORMAT_BC3,                  /* FIXME SRGB */ /* SurfaceFormat.Dxt5SrgbEXT */
	SDL_GPU_TEXTUREFORMAT_BC7,                  /* SurfaceFormat.Bc7EXT */
	SDL_GPU_TEXTUREFORMAT_BC7                   /* FIXME SRGB */ /* SurfaceFormat.Bc7SrgbEXT */
};

static SDL_GpuPrimitiveType XNAToSDL_PrimitiveType[] =
{
	SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,	/* FNA3D_PRIMITIVETYPE_TRIANGLELIST */
	FNA3D_PRIMITIVETYPE_TRIANGLESTRIP,	/* FNA3D_PRIMITIVETYPE_TRIANGLESTRIP */
	SDL_GPU_PRIMITIVETYPE_LINELIST,	    /* FNA3D_PRIMITIVETYPE_LINELIST */
	SDL_GPU_PRIMITIVETYPE_LINESTRIP,	/* FNA3D_PRIMITIVETYPE_LINESTRIP */
	SDL_GPU_PRIMITIVETYPE_POINTLIST	    /* FNA3D_PRIMITIVETYPE_POINTLIST_EXT */
};

static SDL_GpuIndexElementSize XNAToSDL_IndexElementSize[] =
{
	SDL_GPU_INDEXELEMENTSIZE_16BIT,	/* FNA3D_INDEXELEMENTSIZE_16BIT */
	SDL_GPU_INDEXELEMENTSIZE_32BIT	/* FNA3D_INDEXELEMENTSIZE_32BIT */
};

static SDL_GpuBlendFactor XNAToSDL_BlendFactor[] =
{
	SDL_GPU_BLENDFACTOR_ONE,                      /* FNA3D_BLEND_ONE */
	SDL_GPU_BLENDFACTOR_ZERO,                     /* FNA3D_BLEND_ZERO */
	SDL_GPU_BLENDFACTOR_SRC_COLOR,                /* FNA3D_BLEND_SOURCECOLOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,      /* FNA3D_BLEND_INVERSESOURCECOLOR */
	SDL_GPU_BLENDFACTOR_SRC_ALPHA,                /* FNA3D_BLEND_SOURCEALPHA */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,      /* FNA3D_BLEND_INVERSESOURCEALPHA */
	SDL_GPU_BLENDFACTOR_DST_COLOR,                /* FNA3D_BLEND_DESTINATIONCOLOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,      /* FNA3D_BLEND_INVERSEDESTINATIONCOLOR */
	SDL_GPU_BLENDFACTOR_DST_ALPHA,                /* FNA3D_BLEND_DESTINATIONALPHA */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,      /* FNA3D_BLEND_INVERSEDESTINATIONALPHA */
	SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,           /* FNA3D_BLEND_BLENDFACTOR */
	SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR, /* FNA3D_BLEND_INVERSEBLENDFACTOR */
	SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE        /* FNA3D_BLEND_SOURCEALPHASATURATION */
};

static SDL_GpuBlendOp XNAToSDL_BlendOp[] =
{
	SDL_GPU_BLENDOP_ADD,              /* FNA3D_BLENDFUNCTION_ADD */
	SDL_GPU_BLENDOP_SUBTRACT,         /* FNA3D_BLENDFUNCTION_SUBTRACT */
	SDL_GPU_BLENDOP_REVERSE_SUBTRACT, /* FNA3D_BLENDFUNCTION_REVERSESUBTRACT */
	SDL_GPU_BLENDOP_MAX,              /* FNA3D_BLENDFUNCTION_MAX */
	SDL_GPU_BLENDOP_MIN               /* FNA3D_BLENDFUNCTION_MIN */
};

static SDL_GpuPresentMode XNAToSDL_PresentMode[] =
{
    SDL_GPU_PRESENTMODE_MAILBOX, /* Falls back to FIFO if not supported */
    SDL_GPU_PRESENTMODE_MAILBOX, /* Falls back to FIFO if not supported */
    SDL_GPU_PRESENTMODE_FIFO,
    SDL_GPU_PRESENTMODE_IMMEDIATE
};

static SDL_GpuFilter XNAToSDL_MagFilter[] =
{
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuFilter XNAToSDL_MinFilter[] =
{
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_FILTER_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_FILTER_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuSamplerMipmapMode XNAToSDL_MipFilter[] =
{
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_LINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, /* FNA3D_TEXTUREFILTER_POINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_ANISOTROPIC */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,	/* FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_POINT_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,	/* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, /* FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT */
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, 	/* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR */
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, /* FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT */
};

static SDL_GpuSamplerAddressMode XNAToSDL_SamplerAddressMode[] =
{
	SDL_GPU_SAMPLERADDRESSMODE_REPEAT,         /* FNA3D_TEXTUREADDRESSMODE_WRAP */
	SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,  /* FNA3D_TEXTUREADDRESSMODE_CLAMP */
	SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT /* FNA3D_TEXTUREADDRESSMODE_MIRROR */
};

/* Indirection to cleanly handle Renderbuffers */
typedef struct SDLGPU_TextureHandle
{
    SDL_GpuTexture *texture;
    SDL_GpuTextureCreateInfo createInfo;
} SDLGPU_TextureHandle;

typedef struct SDLGPU_Renderbuffer
{
    SDL_GpuTexture *texture;
    uint8_t isDepth; /* if true, owns the texture reference */
} SDLGPU_Renderbuffer;

typedef struct SDLGPU_Effect
{
    MOJOSHADER_effect *effect;
} SDLGPU_Effect;

typedef struct SamplerStateHashMap
{
	PackedState key;
	SDL_GpuSampler *value;
} SamplerStateHashMap;

typedef struct SamplerStateHashArray
{
	SamplerStateHashMap *elements;
	int32_t count;
	int32_t capacity;
} SamplerStateHashArray;

static inline SDL_GpuSampler* SamplerStateHashArray_Fetch(
	SamplerStateHashArray *arr,
	PackedState key
) {
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

static inline void SamplerStateHashArray_Insert(
	SamplerStateHashArray *arr,
	PackedState key,
	SDL_GpuSampler *value
) {
	SamplerStateHashMap map;
	map.key.a = key.a;
	map.key.b = key.b;
	map.value = value;

	EXPAND_ARRAY_IF_NEEDED(arr, 4, SamplerStateHashMap)

	arr->elements[arr->count] = map;
	arr->count += 1;
}

typedef struct SDLGPU_Renderer
{
    SDL_GpuDevice *device;
    SDL_GpuCommandBuffer *commandBuffer;

    uint8_t renderPassInProgress;
    uint8_t needNewRenderPass;

    uint8_t shouldClearColorOnBeginPass;
	uint8_t shouldClearDepthOnBeginPass;
	uint8_t shouldClearStencilOnBeginPass;

    SDL_GpuVec4 clearColorValue;
	SDL_GpuDepthStencilValue clearDepthStencilValue;

	/* Defer render pass settings */
    SDL_GpuTexture *nextRenderPassColorAttachments[MAX_RENDERTARGET_BINDINGS];
    SDL_GpuCubeMapFace nextRenderPassColorAttachmentCubeFace[MAX_RENDERTARGET_BINDINGS];
	uint32_t nextRenderPassColorAttachmentCount;

	SDL_GpuTexture *nextRenderPassDepthStencilAttachment; /* may be NULL */
    SDL_GpuTextureFormat currentDepthFormat;

    SDL_GpuPrimitiveType currentPrimitiveType;
    uint8_t needNewGraphicsPipeline;
	int32_t currentVertexBufferBindingsIndex;

    PackedVertexBufferBindingsArray vertexBufferBindingsCache;

    /* Vertex buffer bind settings */
	uint32_t numVertexBindings;
	FNA3D_VertexBufferBinding vertexBindings[MAX_BOUND_VERTEX_BUFFERS];
	FNA3D_VertexElement vertexElements[MAX_BOUND_VERTEX_BUFFERS][MAX_VERTEX_ATTRIBUTES];
    SDL_GpuBufferBinding vertexBufferBindings[MAX_BOUND_VERTEX_BUFFERS];
    uint8_t needVertexBufferBind;

    /* Index buffer state shadowing */
    SDL_GpuBufferBinding indexBufferBinding;

    /* Sampler bind settings */
    SDL_GpuTextureSamplerBinding vertexTextureSamplerBindings[MAX_VERTEXTEXTURE_SAMPLERS];
    uint8_t needVertexSamplerBind;

    SDL_GpuTextureSamplerBinding fragmentTextureSamplerBindings[MAX_TEXTURE_SAMPLERS];
    uint8_t needFragmentSamplerBind;

    /* Other pipeline state */
    float blendConstants[4];
    uint32_t multisampleMask;
    uint32_t stencilReference;
    SDL_GpuColorAttachmentBlendState blendState[4];
    FNA3D_DepthStencilState depthStencilState;
	FNA3D_RasterizerState rasterizerState;
    SDL_GpuRect scissorRect;

    /* Presentation structure */

    void *mainWindowHandle;
    SDL_GpuTexture *fauxBackbufferColor;
    SDL_GpuTexture *fauxBackbufferDepthStencil; /* may be NULL */
    uint32_t fauxBackbufferWidth;
    uint32_t fauxBackbufferHeight;
    FNA3D_SurfaceFormat fauxBackbufferColorFormat; /* for reading back */
    FNA3D_DepthFormat fauxBackbufferDepthStencilFormat; /* for reading back */
    int32_t fauxBackbufferSampleCount; /* for reading back */

    /* Transfer structure */

    SDL_GpuTransferBuffer *textureDownloadBuffer;
    uint32_t textureDownloadBufferSize;

    SDL_GpuTransferBuffer *bufferDownloadBuffer;
    uint32_t bufferDownloadBufferSize;

    SDL_GpuTransferBuffer *textureUploadBuffer;
    uint32_t textureUploadBufferSize;
    uint32_t textureUploadBufferOffset;

    SDL_GpuTransferBuffer *bufferUploadBuffer;
    uint32_t bufferUploadBufferSize;
    uint32_t bufferUploadBufferOffset;

    /* Hashing */

	SamplerStateHashArray samplerStateArray;

    /* MOJOSHADER */

    MOJOSHADER_sdlContext *mojoshaderContext;
    MOJOSHADER_effect *currentEffect;
    const MOJOSHADER_effectTechnique *currentTechnique;
    uint32_t currentPass;
} SDLGPU_Renderer;

/* Statics */

static SDL_GpuBackend preferredBackends[2] = { SDL_GPU_BACKEND_VULKAN, SDL_GPU_BACKEND_D3D11 };
static FNA3D_PresentationParameters requestedPresentationParameters;
static SDL_GpuBackend selectedBackend = SDL_GPU_BACKEND_INVALID;

/* Destroy */

static void SDLGPU_DestroyDevice(FNA3D_Device *device)
{
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) device->driverData;
    SDL_GpuDestroyDevice(renderer->device);
    SDL_free(renderer);
    SDL_free(device);
}

/* Submission / Presentation */

static void SDLGPU_ResetCommandBufferState(
    SDLGPU_Renderer *renderer
) {
    /* Reset state */
    renderer->needNewRenderPass = 1;
    renderer->needNewGraphicsPipeline = 1;
    renderer->needVertexBufferBind = 1;
    renderer->needVertexSamplerBind = 1;
    renderer->needFragmentSamplerBind = 1;

    renderer->textureUploadBufferOffset = 0;

    MOJOSHADER_sdlSetCommandBuffer(
        renderer->mojoshaderContext,
        renderer->commandBuffer
    );
}

static void SDLGPU_INTERNAL_FlushCommandsAndStall(
    SDLGPU_Renderer *renderer
) {
    SDL_GpuFence *fence = SDL_GpuSubmitAndAcquireFence(
        renderer->device,
        renderer->commandBuffer
    );

    SDL_GpuWaitForFences(
        renderer->device,
        1,
        1,
        &fence
    );

    SDL_GpuReleaseFence(
        renderer->device,
        fence
    );

    SDLGPU_ResetCommandBufferState(renderer);
}

static void SDLGPU_INTERNAL_FlushCommands(
    SDLGPU_Renderer *renderer
) {
    SDL_GpuSubmit(renderer->device, renderer->commandBuffer);
    SDLGPU_ResetCommandBufferState(renderer);
}

/* FIXME: this will break with multi-window, need a claim/unclaim structure */
static void SDLGPU_SwapBuffers(
    FNA3D_Renderer *driverData,
	FNA3D_Rect *sourceRectangle,
	FNA3D_Rect *destinationRectangle,
	void* overrideWindowHandle
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDL_GpuTexture *swapchainTexture;
    SDL_GpuTextureRegion srcRegion;
    SDL_GpuTextureRegion dstRegion;
    uint32_t width, height;

    swapchainTexture = SDL_GpuAcquireSwapchainTexture(
        renderer->device,
        renderer->commandBuffer,
        overrideWindowHandle,
        &width,
        &height
    );

    srcRegion.textureSlice.texture = renderer->fauxBackbufferColor;
    srcRegion.textureSlice.layer = 0;
    srcRegion.textureSlice.mipLevel = 0;
    srcRegion.x = 0;
    srcRegion.y = 0;
    srcRegion.z = 0;
    srcRegion.w = width;
    srcRegion.h = height;
    srcRegion.d = 1;

    dstRegion.textureSlice.texture = swapchainTexture;
    dstRegion.textureSlice.layer = 0;
    dstRegion.textureSlice.mipLevel = 0;
    dstRegion.x = 0;
    dstRegion.y = 0;
    dstRegion.z = 0;
    srcRegion.w = width;
    srcRegion.h = height;
    srcRegion.d = 1;

    SDL_GpuCopyTextureToTexture(
        renderer->device,
        renderer->commandBuffer,
        &srcRegion,
        &dstRegion,
        SDL_GPU_TEXTUREWRITEOPTIONS_SAFE
    );

    SDLGPU_INTERNAL_FlushCommands(renderer);
}

/* Transfer */

static void SDLGPU_INTERNAL_GetTextureData(
    SDLGPU_Renderer *renderer,
    SDL_GpuTexture *texture,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t w,
    uint32_t h,
    uint32_t d,
    uint32_t layer,
    uint32_t level,
    void* data,
    uint32_t dataLength
) {
    SDL_GpuTextureRegion region;
    SDL_GpuBufferImageCopy textureCopyParams;
    SDL_GpuBufferCopy bufferCopyParams;

    /* Flush and stall so the data is up to date */
    SDLGPU_INTERNAL_FlushCommandsAndStall(renderer);

    /* Create transfer buffer if necessary */
    if (renderer->textureDownloadBuffer == NULL)
    {
        renderer->textureDownloadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_TEXTURE,
            dataLength
        );

        renderer->textureDownloadBufferSize = dataLength;
    }
    else if (renderer->textureDownloadBufferSize < dataLength)
    {
        SDL_GpuQueueDestroyTransferBuffer(
            renderer->device,
            renderer->textureDownloadBuffer
        );

        renderer->textureDownloadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_TEXTURE,
            dataLength
        );

        renderer->textureDownloadBufferSize = dataLength;
    }

    /* Set up texture download */
    region.textureSlice.texture = renderer->fauxBackbufferColor;
    region.textureSlice.mipLevel = level;
    region.textureSlice.layer = layer;
    region.x = x;
    region.y = y;
    region.z = z;
    region.w = w;
    region.h = h;
    region.d = d;

    /* All zeroes, assume tight packing */
    textureCopyParams.bufferImageHeight = 0;
    textureCopyParams.bufferOffset = 0;
    textureCopyParams.bufferStride = 0;

    SDL_GpuDownloadFromTexture(
        renderer->device,
        &region,
        renderer->textureDownloadBuffer,
        &textureCopyParams,
        SDL_GPU_TRANSFEROPTIONS_CYCLE
    );

    /* Copy into data pointer */
    bufferCopyParams.srcOffset = 0;
    bufferCopyParams.dstOffset = 0;
    bufferCopyParams.size = dataLength;

    SDL_GpuGetTransferData(
        renderer->device,
        renderer->textureDownloadBuffer,
        data,
        &bufferCopyParams
    );
}

/* Drawing */

static void SDLGPU_INTERNAL_PrepareRenderPassClear(
	SDLGPU_Renderer *renderer,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil,
	uint8_t clearColor,
	uint8_t clearDepth,
	uint8_t clearStencil
) {
    if (!clearColor && !clearDepth && !clearStencil)
	{
		return;
	}

	renderer->shouldClearColorOnBeginPass |= clearColor;
	renderer->shouldClearDepthOnBeginPass |= clearDepth;
	renderer->shouldClearStencilOnBeginPass |= clearStencil;

	if (clearColor)
	{
		renderer->clearColorValue.x = color->x;
		renderer->clearColorValue.y = color->y;
		renderer->clearColorValue.z = color->z;
		renderer->clearColorValue.w = color->w;
	}

	if (clearDepth)
	{
		if (depth < 0.0f)
		{
			depth = 0.0f;
		}
		else if (depth > 1.0f)
		{
			depth = 1.0f;
		}

		renderer->clearDepthStencilValue.depth = depth;
	}

	if (clearStencil)
	{
		renderer->clearDepthStencilValue.stencil = stencil;
	}

	renderer->needNewRenderPass = 1;
}

static void SDLGPU_Clear(
	FNA3D_Renderer *driverData,
	FNA3D_ClearOptions options,
	FNA3D_Vec4 *color,
	float depth,
	int32_t stencil
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
	uint8_t clearColor = (options & FNA3D_CLEAROPTIONS_TARGET) == FNA3D_CLEAROPTIONS_TARGET;
	uint8_t clearDepth = (options & FNA3D_CLEAROPTIONS_DEPTHBUFFER) == FNA3D_CLEAROPTIONS_DEPTHBUFFER;
	uint8_t clearStencil = (options & FNA3D_CLEAROPTIONS_STENCIL) == FNA3D_CLEAROPTIONS_STENCIL;

    SDLGPU_INTERNAL_PrepareRenderPassClear(
        renderer,
        color,
        depth,
        stencil,
        clearColor,
        clearDepth,
        clearStencil
    );
}

static void SDLGPU_INTERNAL_EndPass(
    SDLGPU_Renderer *renderer
) {
    if (renderer->renderPassInProgress)
    {
        SDL_GpuEndRenderPass(
            renderer->device,
            renderer->commandBuffer
        );
    }
}

static void SDLGPU_INTERNAL_BeginRenderPass(
    SDLGPU_Renderer *renderer
) {
    SDL_GpuColorAttachmentInfo colorAttachmentInfos[MAX_RENDERTARGET_BINDINGS];
    SDL_GpuDepthStencilAttachmentInfo depthStencilAttachmentInfo;
    uint32_t i;

    if (!renderer->needNewRenderPass)
    {
        return;
    }

    SDLGPU_INTERNAL_EndPass(renderer);

    for (i = 0; i < renderer->nextRenderPassColorAttachmentCount; i += 1)
    {
        colorAttachmentInfos[i].textureSlice.texture = renderer->nextRenderPassColorAttachments[i];
        colorAttachmentInfos[i].textureSlice.layer = renderer->nextRenderPassColorAttachmentCubeFace[i];
        colorAttachmentInfos[i].textureSlice.mipLevel = 0;

        colorAttachmentInfos[i].loadOp =
            renderer->shouldClearColorOnBeginPass ?
                SDL_GPU_LOADOP_CLEAR :
                SDL_GPU_LOADOP_LOAD;

        /* We always have to store just in case changing render state breaks the render pass. */
        /* FIXME: perhaps there is a way around this? */
        colorAttachmentInfos[i].storeOp = SDL_GPU_STOREOP_STORE;

        colorAttachmentInfos[i].writeOption =
            colorAttachmentInfos[i].loadOp == SDL_GPU_LOADOP_LOAD ?
                SDL_GPU_TEXTUREWRITEOPTIONS_SAFE :
                SDL_GPU_TEXTUREWRITEOPTIONS_CYCLE; /* cycle if we can, it's fast! */

        if (renderer->shouldClearColorOnBeginPass)
        {
            colorAttachmentInfos[i].clearColor = renderer->clearColorValue;
        }
        else
        {
            colorAttachmentInfos[i].clearColor.x = 0;
            colorAttachmentInfos[i].clearColor.y = 0;
            colorAttachmentInfos[i].clearColor.z = 0;
            colorAttachmentInfos[i].clearColor.w = 0;
        }
    }

    if (renderer->nextRenderPassDepthStencilAttachment != NULL)
    {
        depthStencilAttachmentInfo.textureSlice.texture = renderer->nextRenderPassDepthStencilAttachment;
        depthStencilAttachmentInfo.textureSlice.layer = 0;
        depthStencilAttachmentInfo.textureSlice.mipLevel = 0;

        depthStencilAttachmentInfo.loadOp =
            renderer->shouldClearDepthOnBeginPass ?
                SDL_GPU_LOADOP_CLEAR :
                SDL_GPU_LOADOP_DONT_CARE;

        if (renderer->shouldClearDepthOnBeginPass)
        {
            depthStencilAttachmentInfo.loadOp = SDL_GPU_LOADOP_CLEAR;
        }
        else
        {
            /* FIXME: is there a way to safely get rid of this load op? */
            depthStencilAttachmentInfo.loadOp = SDL_GPU_LOADOP_LOAD;
        }

        if (renderer->shouldClearStencilOnBeginPass)
        {
            depthStencilAttachmentInfo.stencilLoadOp = SDL_GPU_LOADOP_CLEAR;
        }
        else
        {
            /* FIXME: is there a way to safely get rid of this load op? */
            depthStencilAttachmentInfo.stencilLoadOp = SDL_GPU_LOADOP_LOAD;
        }

        /* We always have to store just in case changing render state breaks the render pass. */
        /* FIXME: perhaps there is a way around this? */
        depthStencilAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

        depthStencilAttachmentInfo.writeOption =
            depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD || depthStencilAttachmentInfo.loadOp == SDL_GPU_LOADOP_LOAD ?
                SDL_GPU_TEXTUREWRITEOPTIONS_SAFE :
                SDL_GPU_TEXTUREWRITEOPTIONS_CYCLE; /* Cycle if we can! */

        if (renderer->shouldClearDepthOnBeginPass || renderer->shouldClearStencilOnBeginPass)
        {
            depthStencilAttachmentInfo.depthStencilClearValue = renderer->clearDepthStencilValue;
        }
    }

    SDL_GpuBeginRenderPass(
        renderer->device,
        renderer->commandBuffer,
        colorAttachmentInfos,
        renderer->nextRenderPassColorAttachmentCount,
        renderer->nextRenderPassDepthStencilAttachment != NULL ? &depthStencilAttachmentInfo : NULL
    );

    renderer->needNewRenderPass = 0;

    renderer->shouldClearColorOnBeginPass = 0;
    renderer->shouldClearDepthOnBeginPass = 0;
    renderer->shouldClearStencilOnBeginPass = 0;

    renderer->needNewGraphicsPipeline = 1;
}

static void SDLGPU_SetRenderTargets(
    FNA3D_Renderer *driverData,
	FNA3D_RenderTargetBinding *renderTargets,
	int32_t numRenderTargets,
	FNA3D_Renderbuffer *depthStencilBuffer,
	FNA3D_DepthFormat depthFormat,
	uint8_t preserveTargetContents /* ignored */
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    int32_t i;

    if (
        renderer->shouldClearColorOnBeginPass ||
        renderer->shouldClearDepthOnBeginPass ||
        renderer->shouldClearDepthOnBeginPass
    ) {
        SDLGPU_INTERNAL_BeginRenderPass(renderer);
    }

    for (i = 0; i < MAX_RENDERTARGET_BINDINGS; i += 1)
    {
        renderer->nextRenderPassColorAttachments[i] = NULL;
    }
    renderer->nextRenderPassDepthStencilAttachment = NULL;

    if (numRenderTargets <= 0)
    {
        renderer->nextRenderPassColorAttachments[0] = renderer->fauxBackbufferColor;
        renderer->nextRenderPassColorAttachmentCubeFace[0] = 0;
        renderer->nextRenderPassColorAttachmentCount = 1;

        renderer->nextRenderPassDepthStencilAttachment = renderer->fauxBackbufferDepthStencil;
    }
    else
    {
        for (i = 0; i < numRenderTargets; i += 1)
        {
            renderer->nextRenderPassColorAttachmentCubeFace[i] = (
                renderTargets[i].type == FNA3D_RENDERTARGET_TYPE_CUBE ?
                    (SDL_GpuCubeMapFace) renderTargets[i].cube.face :
                    0
            );

            if (renderTargets[i].colorBuffer != NULL)
            {
                renderer->nextRenderPassColorAttachments[i] = ((SDLGPU_Renderbuffer*) renderTargets[i].colorBuffer)->texture;
            }
        }

        renderer->nextRenderPassColorAttachmentCount = numRenderTargets;
    }

    if (depthStencilBuffer != NULL)
    {
        renderer->nextRenderPassDepthStencilAttachment = ((SDLGPU_Renderbuffer*) depthStencilBuffer)->texture;
        renderer->currentDepthFormat = XNAToSDL_DepthFormat(depthFormat);
    }

    renderer->needNewRenderPass = 1;
}

static void SDLGPU_ResolveTarget(
    FNA3D_Renderer *driverData,
    FNA3D_RenderTargetBinding *target
) {
    /* no-op? SDL_gpu auto-resolves MSAA targets */
}

static void SDLGPU_INTERNAL_BindGraphicsPipeline(
    SDLGPU_Renderer *renderer
) {
    /* TODO */
    if (!renderer->needNewGraphicsPipeline)
    {
        return;
    }

    /* Reset deferred binding state */
    renderer->needNewGraphicsPipeline = 0;
    renderer->needFragmentSamplerBind = 1;
    renderer->needVertexSamplerBind = 1;
    renderer->needVertexBufferBind = 1;
    renderer->indexBufferBinding.gpuBuffer = NULL;
}

static SDL_GpuSampler* SDLGPU_INTERNAL_FetchSamplerState(
    SDLGPU_Renderer *renderer,
    FNA3D_SamplerState *samplerState
) {
    SDL_GpuSamplerStateCreateInfo samplerCreateInfo;
    SDL_GpuSampler *sampler;

    PackedState hash = GetPackedSamplerState(*samplerState);
    sampler = SamplerStateHashArray_Fetch(
        &renderer->samplerStateArray,
        hash
    );
    if (sampler != NULL)
    {
        return sampler;
    }

    samplerCreateInfo.magFilter = XNAToSDL_MagFilter[samplerState->filter];
    samplerCreateInfo.minFilter = XNAToSDL_MinFilter[samplerState->filter];
    samplerCreateInfo.mipmapMode = XNAToSDL_MipFilter[samplerState->filter];
    samplerCreateInfo.addressModeU = XNAToSDL_SamplerAddressMode[
        samplerState->addressU
    ];
    samplerCreateInfo.addressModeV = XNAToSDL_SamplerAddressMode[
        samplerState->addressV
    ];
    samplerCreateInfo.addressModeW = XNAToSDL_SamplerAddressMode[
        samplerState->addressW
    ];

    samplerCreateInfo.mipLodBias = samplerState->mipMapLevelOfDetailBias;
    samplerCreateInfo.anisotropyEnable = (samplerState->filter == FNA3D_TEXTUREFILTER_ANISOTROPIC);
    samplerCreateInfo.maxAnisotropy = (float) SDL_max(1, samplerState->maxAnisotropy);
    samplerCreateInfo.compareEnable = 0;
    samplerCreateInfo.compareOp = 0;
    samplerCreateInfo.minLod = (float) samplerState->maxMipLevel;
    samplerCreateInfo.maxLod = 1000.0f;
    samplerCreateInfo.borderColor = SDL_GPU_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK;

    sampler = SDL_GpuCreateSampler(
        renderer->device,
        &samplerCreateInfo
    );

    if (sampler == NULL)
    {
        FNA3D_LogError("Failed to create sampler!");
        return NULL;
    }

    SamplerStateHashArray_Insert(
        &renderer->samplerStateArray,
        hash,
        sampler
    );

    return sampler;
}

static void SDLGPU_VerifyVertexSampler(
    FNA3D_Renderer *driverData,
	int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
    SDL_GpuSampler *gpuSampler;

    if (texture == NULL)
    {
        if (renderer->vertexTextureSamplerBindings[index].texture != NULL)
        {
            renderer->vertexTextureSamplerBindings[index].texture = NULL;
            renderer->vertexTextureSamplerBindings[index].sampler = NULL;
            renderer->needVertexSamplerBind = 1;
        }

        return;
    }

    if (textureHandle->texture != renderer->vertexTextureSamplerBindings[index].texture)
    {
        renderer->vertexTextureSamplerBindings[index].texture = textureHandle->texture;
        renderer->needVertexSamplerBind = 1;
    }

    gpuSampler = SDLGPU_INTERNAL_FetchSamplerState(
        renderer,
        sampler
    );

    if (gpuSampler != renderer->vertexTextureSamplerBindings[index].sampler)
    {
        renderer->vertexTextureSamplerBindings[index].sampler = gpuSampler;
        renderer->needVertexSamplerBind = 1;
    }
}

static void SDLGPU_VerifySampler(
    FNA3D_Renderer *driverData,
    int32_t index,
	FNA3D_Texture *texture,
	FNA3D_SamplerState *sampler
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
    SDL_GpuSampler *gpuSampler;

    if (texture == NULL)
    {
        if (renderer->fragmentTextureSamplerBindings[index].texture != NULL)
        {
            renderer->fragmentTextureSamplerBindings[index].texture = NULL;
            renderer->fragmentTextureSamplerBindings[index].sampler = NULL;
            renderer->needFragmentSamplerBind = 1;
        }

        return;
    }

    if (textureHandle->texture != renderer->fragmentTextureSamplerBindings[index].texture)
    {
        renderer->fragmentTextureSamplerBindings[index].texture = textureHandle->texture;
        renderer->needFragmentSamplerBind = 1;
    }

    gpuSampler = SDLGPU_INTERNAL_FetchSamplerState(
        renderer,
        sampler
    );

    if (gpuSampler != renderer->fragmentTextureSamplerBindings[index].sampler)
    {
        renderer->fragmentTextureSamplerBindings[index].sampler = gpuSampler;
        renderer->needFragmentSamplerBind = 1;
    }
}

static void SDLGPU_ApplyVertexBufferBindings(
    FNA3D_Renderer *driverData,
	FNA3D_VertexBufferBinding *bindings,
	int32_t numBindings,
	uint8_t bindingsUpdated,
	int32_t baseVertex
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    MOJOSHADER_sdlShader *vertexShader, *blah;
    void* bindingsResult;
    FNA3D_VertexBufferBinding *src, *dst;
    int32_t i, bindingsIndex;
    uint32_t hash;

    /* link/compile shader program if it hasn't been yet */
    if (!MOJOSHADER_sdlCheckProgramStatus(renderer->mojoshaderContext))
    {
        MOJOSHADER_sdlLinkProgram(renderer->mojoshaderContext);
    }

	/* Check VertexBufferBindings */
	MOJOSHADER_sdlGetBoundShaders(renderer->mojoshaderContext, &vertexShader, &blah);

	bindingsResult = PackedVertexBufferBindingsArray_Fetch(
		renderer->vertexBufferBindingsCache,
		bindings,
		numBindings,
		vertexShader,
		&bindingsIndex,
		&hash
	);

    if (bindingsResult == NULL)
    {
        PackedVertexBufferBindingsArray_Insert(
			&renderer->vertexBufferBindingsCache,
			bindings,
			numBindings,
			vertexShader,
			(void*) 69420
		);
    }

    if (bindingsUpdated)
    {
        renderer->numVertexBindings = numBindings;
        for (i = 0; i < numBindings; i += 1)
        {
            src = &bindings[i];
            dst = &renderer->vertexBindings[i];
            dst->vertexBuffer = src->vertexBuffer;
            dst->vertexOffset = src->vertexOffset;
            dst->instanceFrequency = src->instanceFrequency;
            dst->vertexDeclaration.vertexStride = src->vertexDeclaration.vertexStride;
            dst->vertexDeclaration.elementCount = src->vertexDeclaration.elementCount;
            SDL_memcpy(
                dst->vertexDeclaration.elements,
                src->vertexDeclaration.elements,
                sizeof(FNA3D_VertexElement) * src->vertexDeclaration.elementCount
            );
        }
    }

    if (bindingsIndex != renderer->currentVertexBufferBindingsIndex)
    {
        renderer->currentVertexBufferBindingsIndex = bindingsIndex;
        renderer->needNewGraphicsPipeline = 1;
    }

    /* Don't actually bind buffers yet because pipelines are lazily bound */
    for (i = 0; i < numBindings; i += 1)
    {
        renderer->vertexBufferBindings[i].gpuBuffer = (SDL_GpuBuffer*) bindings[i].vertexBuffer;
        renderer->vertexBufferBindings[i].offset = (bindings[i].vertexOffset + baseVertex) * bindings[i].vertexDeclaration.vertexStride;
    }

    renderer->needVertexBufferBind = 1;
}

static void SDLGPU_SetViewport(
    FNA3D_Renderer *driverData,
	FNA3D_Viewport *viewport
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDL_GpuViewport gpuViewport;

    gpuViewport.x = (float) viewport->x;
    gpuViewport.y = (float) viewport->y;
    gpuViewport.w = (float) viewport->w;
    gpuViewport.h = (float) viewport->h;
    gpuViewport.minDepth = viewport->minDepth;
    gpuViewport.maxDepth = viewport->maxDepth;

    SDL_GpuSetViewport(
        renderer->device,
        renderer->commandBuffer,
        &gpuViewport
    );
}

static void SDLGPU_SetScissorRect(
    FNA3D_Renderer *driverData,
	FNA3D_Rect *scissor
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    renderer->scissorRect.x = scissor->x;
    renderer->scissorRect.y = scissor->y;
    renderer->scissorRect.w = scissor->w;
    renderer->scissorRect.h = scissor->h;

    SDL_GpuSetScissor(
        renderer->device,
        renderer->commandBuffer,
        &renderer->scissorRect
    );
}

static void SDLGPU_GetBlendFactor(
	FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    blendFactor->r = (uint8_t) SDL_roundf(renderer->blendConstants[0] * 255.0f);
    blendFactor->g = (uint8_t) SDL_roundf(renderer->blendConstants[1] * 255.0f);
    blendFactor->b = (uint8_t) SDL_roundf(renderer->blendConstants[2] * 255.0f);
    blendFactor->a = (uint8_t) SDL_roundf(renderer->blendConstants[3] * 255.0f);
}

static void SDLGPU_SetBlendFactor(
    FNA3D_Renderer *driverData,
	FNA3D_Color *blendFactor
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    if (
        blendFactor->r != renderer->blendConstants[0] ||
        blendFactor->g != renderer->blendConstants[1] ||
        blendFactor->b != renderer->blendConstants[2] ||
        blendFactor->a != renderer->blendConstants[3]
    ) {
        renderer->blendConstants[0] = blendFactor->r;
        renderer->blendConstants[1] = blendFactor->g;
        renderer->blendConstants[2] = blendFactor->b;
        renderer->blendConstants[3] = blendFactor->a;

        renderer->needNewGraphicsPipeline = 1;
    }
}

static int32_t SDLGPU_GetMultiSampleMask(
    FNA3D_Renderer *driverData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    return (int32_t) renderer->multisampleMask;
}

static void SDLGPU_SetMultiSampleMask(
    FNA3D_Renderer *driverData,
    int32_t mask
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    if (renderer->multisampleMask != (uint32_t) mask)
    {
        renderer->multisampleMask = (uint32_t) mask;
        renderer->needNewGraphicsPipeline = 1;
    }
}

static int32_t SDLGPU_GetReferenceStencil(
    FNA3D_Renderer *driverData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    return (int32_t) renderer->stencilReference;
}

static void SDLGPU_SetReferenceStencil(
    FNA3D_Renderer *driverData,
    int32_t ref
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    if (renderer->stencilReference != (uint32_t) ref)
    {
        renderer->stencilReference = (uint32_t) ref;
        renderer->needNewGraphicsPipeline = 1;
    }
}

static void SDLGPU_SetBlendState(
    FNA3D_Renderer *driverData,
    FNA3D_BlendState *blendState
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDLGPU_SetBlendFactor(
        driverData,
        &blendState->blendFactor
    );

    SDLGPU_SetMultiSampleMask(
        driverData,
        blendState->multiSampleMask
    );

    renderer->blendState[0].blendEnable         = 1;
    renderer->blendState[0].srcColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorSourceBlend];
    renderer->blendState[0].dstColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorDestinationBlend];
    renderer->blendState[0].colorBlendOp        = XNAToSDL_BlendOp[blendState->colorBlendFunction];
    renderer->blendState[0].srcAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaSourceBlend];
    renderer->blendState[0].dstAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaDestinationBlend];
    renderer->blendState[0].alphaBlendOp        = XNAToSDL_BlendOp[blendState->alphaBlendFunction];
    renderer->blendState[0].colorWriteMask      = (SDL_GpuColorComponentFlags) blendState->colorWriteEnable;

    renderer->blendState[1].blendEnable         = 1;
    renderer->blendState[1].srcColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorSourceBlend];
    renderer->blendState[1].dstColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorDestinationBlend];
    renderer->blendState[1].colorBlendOp        = XNAToSDL_BlendOp[blendState->colorBlendFunction];
    renderer->blendState[1].srcAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaSourceBlend];
    renderer->blendState[1].dstAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaDestinationBlend];
    renderer->blendState[1].alphaBlendOp        = XNAToSDL_BlendOp[blendState->alphaBlendFunction];
    renderer->blendState[1].colorWriteMask      = (SDL_GpuColorComponentFlags) blendState->colorWriteEnable1;

    renderer->blendState[2].blendEnable         = 1;
    renderer->blendState[2].srcColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorSourceBlend];
    renderer->blendState[2].dstColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorDestinationBlend];
    renderer->blendState[2].colorBlendOp        = XNAToSDL_BlendOp[blendState->colorBlendFunction];
    renderer->blendState[2].srcAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaSourceBlend];
    renderer->blendState[2].dstAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaDestinationBlend];
    renderer->blendState[2].alphaBlendOp        = XNAToSDL_BlendOp[blendState->alphaBlendFunction];
    renderer->blendState[2].colorWriteMask      = (SDL_GpuColorComponentFlags) blendState->colorWriteEnable2;

    renderer->blendState[3].blendEnable         = 1;
    renderer->blendState[3].srcColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorSourceBlend];
    renderer->blendState[3].dstColorBlendFactor = XNAToSDL_BlendFactor[blendState->colorDestinationBlend];
    renderer->blendState[3].colorBlendOp        = XNAToSDL_BlendOp[blendState->colorBlendFunction];
    renderer->blendState[3].srcAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaSourceBlend];
    renderer->blendState[3].dstAlphaBlendFactor = XNAToSDL_BlendFactor[blendState->alphaDestinationBlend];
    renderer->blendState[3].alphaBlendOp        = XNAToSDL_BlendOp[blendState->alphaBlendFunction];
    renderer->blendState[3].colorWriteMask      = (SDL_GpuColorComponentFlags) blendState->colorWriteEnable3;
}

static void SDLGPU_SetDepthStencilState(
	FNA3D_Renderer *driverData,
	FNA3D_DepthStencilState *depthStencilState
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	/* TODO: Arrange these checks in an optimized priority */
	if (	renderer->depthStencilState.depthBufferEnable != depthStencilState->depthBufferEnable ||
		renderer->depthStencilState.depthBufferWriteEnable != depthStencilState->depthBufferWriteEnable ||
		renderer->depthStencilState.depthBufferFunction != depthStencilState->depthBufferFunction ||
		renderer->depthStencilState.stencilEnable != depthStencilState->stencilEnable ||
		renderer->depthStencilState.stencilMask != depthStencilState->stencilMask ||
		renderer->depthStencilState.stencilWriteMask != depthStencilState->stencilWriteMask ||
		renderer->depthStencilState.twoSidedStencilMode != depthStencilState->twoSidedStencilMode ||
		renderer->depthStencilState.stencilFail != depthStencilState->stencilFail ||
		renderer->depthStencilState.stencilDepthBufferFail != depthStencilState->stencilDepthBufferFail ||
		renderer->depthStencilState.stencilPass != depthStencilState->stencilPass ||
		renderer->depthStencilState.stencilFunction != depthStencilState->stencilFunction ||
		renderer->depthStencilState.ccwStencilFail != depthStencilState->ccwStencilFail ||
		renderer->depthStencilState.ccwStencilDepthBufferFail != depthStencilState->ccwStencilDepthBufferFail ||
		renderer->depthStencilState.ccwStencilPass != depthStencilState->ccwStencilPass ||
		renderer->depthStencilState.ccwStencilFunction != depthStencilState->ccwStencilFunction ||
		renderer->depthStencilState.referenceStencil != depthStencilState->referenceStencil	)
	{
		renderer->needNewGraphicsPipeline = 1;

		SDL_memcpy(
			&renderer->depthStencilState,
			depthStencilState,
			sizeof(FNA3D_DepthStencilState)
		);
	}

    SDLGPU_SetReferenceStencil(
        driverData,
        depthStencilState->referenceStencil
    );
}

static void SDLGPU_ApplyRasterizerState(
    FNA3D_Renderer *driverData,
	FNA3D_RasterizerState *rasterizerState
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    float realDepthBias;

    if (rasterizerState->scissorTestEnable != renderer->rasterizerState.scissorTestEnable)
    {
        renderer->rasterizerState.scissorTestEnable = rasterizerState->scissorTestEnable;
        SDL_GpuSetScissor(
            renderer->device,
            renderer->commandBuffer,
            &renderer->scissorRect
        );
    }

	realDepthBias = rasterizerState->depthBias * XNAToSDL_DepthBiasScale(
        renderer->currentDepthFormat
	);

    if (
        rasterizerState->cullMode != renderer->rasterizerState.cullMode ||
        rasterizerState->fillMode != renderer->rasterizerState.fillMode ||
        rasterizerState->multiSampleAntiAlias != renderer->rasterizerState.multiSampleAntiAlias ||
        realDepthBias != renderer->rasterizerState.depthBias ||
        rasterizerState->slopeScaleDepthBias != renderer->rasterizerState.slopeScaleDepthBias
    ) {
        renderer->rasterizerState.cullMode = rasterizerState->cullMode;
        renderer->rasterizerState.fillMode = rasterizerState->fillMode;
        renderer->rasterizerState.multiSampleAntiAlias = rasterizerState->multiSampleAntiAlias;
        renderer->rasterizerState.depthBias = realDepthBias;
        renderer->rasterizerState.slopeScaleDepthBias = rasterizerState->slopeScaleDepthBias;
        renderer->needNewGraphicsPipeline = 1;
    }
}

/* Actually bind all deferred state before drawing! */
static void SDLGPU_INTERNAL_BindDeferredState(
    SDLGPU_Renderer *renderer,
    SDL_GpuPrimitiveType primitiveType,
    SDL_GpuBuffer *indexBuffer, /* can be NULL */
    SDL_GpuIndexElementSize indexElementSize
) {
    if (primitiveType != renderer->currentPrimitiveType)
    {
        renderer->currentPrimitiveType = primitiveType;
        renderer->needNewGraphicsPipeline = 1;
    }

    SDLGPU_INTERNAL_BeginRenderPass(renderer);
    SDLGPU_INTERNAL_BindGraphicsPipeline(renderer);

    if (renderer->needVertexSamplerBind || renderer->needFragmentSamplerBind)
    {
        if (renderer->needVertexSamplerBind)
        {
            SDL_GpuBindVertexSamplers(
                renderer->device,
                renderer->commandBuffer,
                renderer->vertexTextureSamplerBindings
            );
        }

        if (renderer->needFragmentSamplerBind)
        {
            SDL_GpuBindFragmentSamplers(
                renderer->device,
                renderer->commandBuffer,
                renderer->fragmentTextureSamplerBindings
            );
        }
    }

    if (
        indexBuffer != NULL &&
        renderer->indexBufferBinding.gpuBuffer != (SDL_GpuBuffer*) indexBuffer
    ) {
        renderer->indexBufferBinding.gpuBuffer = (SDL_GpuBuffer*) indexBuffer;

        SDL_GpuBindIndexBuffer(
            renderer->device,
            renderer->commandBuffer,
            &renderer->indexBufferBinding,
            indexElementSize
        );
    }

    if (renderer->needVertexBufferBind)
    {
        SDL_GpuBindVertexBuffers(
            renderer->device,
            renderer->commandBuffer,
            0,
            renderer->numVertexBindings,
            renderer->vertexBufferBindings
        );
    }
}

static void SDLGPU_DrawInstancedPrimitives(
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
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

	/* Note that minVertexIndex/numVertices are NOT used! */

    SDLGPU_INTERNAL_BindDeferredState(
        renderer,
        XNAToSDL_PrimitiveType[primitiveType],
        (SDL_GpuBuffer*) indices,
        XNAToSDL_IndexElementSize[indexElementSize]
    );

    SDL_GpuDrawInstancedPrimitives(
        renderer->device,
        renderer->commandBuffer,
        baseVertex,
        startIndex,
        primitiveCount,
        instanceCount
    );
}

static void SDLGPU_DrawIndexedPrimitives(
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
    SDLGPU_DrawInstancedPrimitives(
        driverData,
        primitiveType,
        baseVertex,
        minVertexIndex,
        numVertices,
        startIndex,
        primitiveCount,
        1,
        indices,
        indexElementSize
    );
}

static void SDLGPU_DrawPrimitives(
    FNA3D_Renderer *driverData,
	FNA3D_PrimitiveType primitiveType,
	int32_t vertexStart,
	int32_t primitiveCount
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDLGPU_INTERNAL_BindDeferredState(
        renderer,
        XNAToSDL_PrimitiveType[primitiveType],
        NULL,
        SDL_GPU_INDEXELEMENTSIZE_16BIT
    );

    SDL_GpuDrawPrimitives(
        renderer->device,
        renderer->commandBuffer,
        vertexStart,
        primitiveCount
    );
}

/* Backbuffer Functions */

static void SDLGPU_INTERNAL_DestroyFauxBackbuffer(
    SDLGPU_Renderer *renderer
) {
    SDL_GpuQueueDestroyTexture(
        renderer->device,
        renderer->fauxBackbufferColor
    );

    if (renderer->fauxBackbufferDepthStencil != NULL)
    {
        SDL_GpuQueueDestroyTexture(
            renderer->device,
            renderer->fauxBackbufferDepthStencil
        );
    }

    renderer->fauxBackbufferColor = NULL;
    renderer->fauxBackbufferDepthStencil = NULL;
}

static void SDLGPU_INTERNAL_CreateFauxBackbuffer(
    SDLGPU_Renderer *renderer,
	FNA3D_PresentationParameters *presentationParameters
) {
    SDL_GpuTextureCreateInfo backbufferCreateInfo;

    backbufferCreateInfo.width = presentationParameters->backBufferWidth;
    backbufferCreateInfo.height = presentationParameters->backBufferHeight;
    backbufferCreateInfo.depth = 1;
    backbufferCreateInfo.format = XNAToSDL_SurfaceFormat[presentationParameters->backBufferFormat];
    backbufferCreateInfo.isCube = 0;
    backbufferCreateInfo.layerCount = 1;
    backbufferCreateInfo.levelCount = 1;
    backbufferCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT | SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;
    backbufferCreateInfo.sampleCount = XNAToSDL_SampleCount(presentationParameters->multiSampleCount);

    renderer->fauxBackbufferColor = SDL_GpuCreateTexture(
        renderer->device,
        &backbufferCreateInfo
    );

    if (presentationParameters->depthStencilFormat != FNA3D_DEPTHFORMAT_NONE)
    {
        backbufferCreateInfo.format = XNAToSDL_DepthFormat(presentationParameters->depthStencilFormat);
        backbufferCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT;

        renderer->fauxBackbufferDepthStencil = SDL_GpuCreateTexture(
            renderer->device,
            &backbufferCreateInfo
        );
    }

    renderer->fauxBackbufferWidth = presentationParameters->backBufferWidth;
    renderer->fauxBackbufferHeight = presentationParameters->backBufferHeight;

    renderer->fauxBackbufferColorFormat = presentationParameters->backBufferFormat;
    renderer->fauxBackbufferDepthStencilFormat = presentationParameters->depthStencilFormat;

    renderer->fauxBackbufferSampleCount = presentationParameters->multiSampleCount;
}

static void SDLGPU_ResetBackbuffer(
    FNA3D_Renderer *driverData,
	FNA3D_PresentationParameters *presentationParameters
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDLGPU_INTERNAL_FlushCommands(renderer);

    SDLGPU_INTERNAL_DestroyFauxBackbuffer(renderer);
    SDLGPU_INTERNAL_CreateFauxBackbuffer(
        renderer,
        presentationParameters
    );

    SDL_GpuUnclaimWindow(
        renderer->device,
        renderer->mainWindowHandle
    );

    if (!SDL_GpuClaimWindow(
        renderer->device,
        presentationParameters->deviceWindowHandle,
        XNAToSDL_PresentMode[presentationParameters->presentationInterval]
    )) {
        FNA3D_LogError("Failed to claim window!");
        return;
    }

    renderer->mainWindowHandle = presentationParameters->deviceWindowHandle;
}

static void SDLGPU_ReadBackbuffer(
    FNA3D_Renderer *driverData,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	void* data,
	int32_t dataLength
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDLGPU_INTERNAL_GetTextureData(
        renderer,
        renderer->fauxBackbufferColor,
        (uint32_t) x,
        (uint32_t) y,
        0,
        (uint32_t) w,
        (uint32_t) h,
        1,
        0,
        0,
        data,
        (uint32_t) dataLength
    );
}

static void SDLGPU_GetBackbufferSize(
    FNA3D_Renderer *driverData,
	int32_t *w,
	int32_t *h
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    *w = (int32_t) renderer->fauxBackbufferWidth;
    *h = (int32_t) renderer->fauxBackbufferHeight;
}

static FNA3D_SurfaceFormat SDLGPU_GetBackbufferSurfaceFormat(
	FNA3D_Renderer *driverData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    return renderer->fauxBackbufferColorFormat;
}

static FNA3D_DepthFormat SDLGPU_GetBackbufferDepthFormat(
    FNA3D_Renderer *driverData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    return renderer->fauxBackbufferDepthStencilFormat;
}

static int32_t SDLGPU_GetBackbufferMultiSampleCount(
    FNA3D_Renderer *driverData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    return renderer->fauxBackbufferSampleCount;
}

/* Textures */

static SDLGPU_TextureHandle* SDLGPU_INTERNAL_CreateTextureWithHandle(
    SDLGPU_Renderer *renderer,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    SDL_GpuTextureFormat format,
    uint32_t layerCount,
    uint32_t levelCount,
    SDL_GpuTextureUsageFlags usageFlags,
    SDL_GpuSampleCount sampleCount
) {
    SDL_GpuTextureCreateInfo textureCreateInfo;
    SDL_GpuTexture *texture;
    SDLGPU_TextureHandle *textureHandle;

    textureCreateInfo.width = width;
    textureCreateInfo.height = height;
    textureCreateInfo.depth = depth;
    textureCreateInfo.format = format;
    textureCreateInfo.layerCount = layerCount;
    textureCreateInfo.levelCount = levelCount;
    textureCreateInfo.isCube = layerCount == 6;
    textureCreateInfo.usageFlags = usageFlags;
    textureCreateInfo.sampleCount = sampleCount;

    texture = SDL_GpuCreateTexture(
        renderer->device,
        &textureCreateInfo
    );

    if (texture == NULL)
    {
        FNA3D_LogError("Failed to create texture!");
        return NULL;
    }

    textureHandle = SDL_malloc(sizeof(SDLGPU_TextureHandle));
    textureHandle->texture = texture;
    textureHandle->createInfo = textureCreateInfo;

    return textureHandle;
}

static FNA3D_Texture* SDLGPU_CreateTexture2D(
    FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
        (SDLGPU_Renderer*) driverData,
        (uint32_t) width,
        (uint32_t) height,
        1,
        XNAToSDL_SurfaceFormat[format],
        1,
        levelCount,
        SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT,
        SDL_GPU_SAMPLECOUNT_1
    );
}

static FNA3D_Texture* SDLGPU_CreateTexture3D(
   	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t width,
	int32_t height,
	int32_t depth,
	int32_t levelCount
) {
    return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
        (SDLGPU_Renderer*) driverData,
        (uint32_t) width,
        (uint32_t) height,
        (uint32_t) depth,
        XNAToSDL_SurfaceFormat[format],
        1,
        levelCount,
        SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT,
        SDL_GPU_SAMPLECOUNT_1
    );
}

static FNA3D_Texture* SDLGPU_CreateTextureCube(
    FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t size,
	int32_t levelCount,
	uint8_t isRenderTarget
) {
    SDL_GpuTextureUsageFlags usageFlags = SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;

    if (isRenderTarget)
    {
        usageFlags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT;
    }

    return (FNA3D_Texture*) SDLGPU_INTERNAL_CreateTextureWithHandle(
        (SDLGPU_Renderer*) driverData,
        (uint32_t) size,
        (uint32_t) size,
        1,
        XNAToSDL_SurfaceFormat[format],
        6,
        levelCount,
        usageFlags,
        SDL_GPU_SAMPLECOUNT_1
    );
}

static FNA3D_Renderbuffer* SDLGPU_GenColorRenderbuffer(
	FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount,
	FNA3D_Texture *texture
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
    SDLGPU_Renderbuffer *colorBufferHandle;

    /* Recreate texture with appropriate settings */
    SDL_GpuQueueDestroyTexture(renderer->device, textureHandle->texture);

    textureHandle->createInfo.sampleCount = XNAToSDL_SampleCount(multiSampleCount);
    textureHandle->createInfo.usageFlags =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT |
        SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;

    textureHandle->texture = SDL_GpuCreateTexture(
        renderer->device,
        &textureHandle->createInfo
    );

    if (textureHandle->texture == NULL)
    {
        FNA3D_LogError("Failed to recreate color buffer texture!");
        return NULL;
    }

    colorBufferHandle = SDL_malloc(sizeof(SDLGPU_Renderbuffer));
    colorBufferHandle->texture = textureHandle->texture;
    colorBufferHandle->isDepth = 0;

    return (FNA3D_Renderbuffer*) colorBufferHandle;
}

static FNA3D_Renderbuffer* SDLGPU_GenDepthStencilRenderbuffer(
    FNA3D_Renderer *driverData,
	int32_t width,
	int32_t height,
	FNA3D_DepthFormat format,
	int32_t multiSampleCount
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDL_GpuTextureCreateInfo textureCreateInfo;
    SDL_GpuTexture *texture;
    SDLGPU_Renderbuffer *renderbuffer;

    textureCreateInfo.width = (uint32_t) width;
    textureCreateInfo.height = (uint32_t) height;
    textureCreateInfo.depth = 0;
    textureCreateInfo.format = XNAToSDL_DepthFormat(format);
    textureCreateInfo.layerCount = 0;
    textureCreateInfo.levelCount = 0;
    textureCreateInfo.isCube = 0;
    textureCreateInfo.usageFlags = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT;
    textureCreateInfo.sampleCount = XNAToSDL_SampleCount(multiSampleCount);

    texture = SDL_GpuCreateTexture(
        renderer->device,
        &textureCreateInfo
    );

    if (texture == NULL)
    {
        FNA3D_LogError("Failed to create depth stencil buffer!");
        return NULL;
    }

    renderbuffer = SDL_malloc(sizeof(SDLGPU_Renderbuffer));
    renderbuffer->texture = texture;
    renderbuffer->isDepth = 1;

    return (FNA3D_Renderbuffer*) renderbuffer;
}

static void SDLGPU_AddDisposeTexture(
    FNA3D_Renderer *driverData,
	FNA3D_Texture *texture
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDL_GpuQueueDestroyTexture(
        renderer->device,
        textureHandle->texture
    );

    SDL_free(textureHandle);
}

static void SDLGPU_AddDisposeRenderbuffer(
    FNA3D_Renderer *driverData,
	FNA3D_Renderbuffer *renderbuffer
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_Renderbuffer *renderbufferHandle = (SDLGPU_Renderbuffer*) renderbuffer;

    if (renderbufferHandle->isDepth)
    {
        SDL_GpuQueueDestroyTexture(
            renderer->device,
            renderbufferHandle->texture
        );
    }

    SDL_free(renderbufferHandle);
}

static void SDLGPU_INTERNAL_SetTextureData(
    SDLGPU_Renderer *renderer,
    SDL_GpuTexture *texture,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t w,
    uint32_t h,
    uint32_t d,
    uint32_t layer,
    uint32_t mipLevel,
    void* data,
    uint32_t dataLength
) {
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;
    SDL_GpuBufferCopy copyParams;
    SDL_GpuTextureRegion textureRegion;
    SDL_GpuBufferImageCopy textureCopyParams;

     /* Recreate transfer buffer if necessary */
    if (renderer->textureUploadBufferOffset + dataLength >= renderer->textureUploadBufferSize)
    {
        SDL_GpuQueueDestroyTransferBuffer(
            renderer->device,
            renderer->textureUploadBuffer
        );

        renderer->textureUploadBufferSize = dataLength;
        renderer->textureUploadBufferOffset = 0;
        renderer->textureUploadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_TEXTURE,
            renderer->textureUploadBufferSize
        );
    }

    copyParams.srcOffset = 0;
    copyParams.dstOffset = renderer->textureUploadBufferOffset;
    copyParams.size = dataLength;

    SDL_GpuSetTransferData(
        renderer->device,
        data,
        renderer->textureUploadBuffer,
        &copyParams,
        renderer->textureUploadBufferOffset == 0 ? SDL_GPU_TRANSFEROPTIONS_CYCLE : SDL_GPU_TRANSFEROPTIONS_UNSAFE
    );

    textureRegion.textureSlice.texture = textureHandle->texture;
    textureRegion.textureSlice.layer = layer;
    textureRegion.textureSlice.mipLevel = mipLevel;
    textureRegion.x = x;
    textureRegion.y = y;
    textureRegion.z = z;
    textureRegion.w = w;
    textureRegion.h = h;
    textureRegion.d = d;

    textureCopyParams.bufferOffset = renderer->textureUploadBufferOffset;
    textureCopyParams.bufferStride = 0;      /* default, assume tightly packed */
    textureCopyParams.bufferImageHeight = 0; /* default, assume tightly packed */

    SDL_GpuUploadToTexture(
        renderer->device,
        renderer->commandBuffer,
        renderer->textureUploadBuffer,
        &textureRegion,
        &textureCopyParams,
        SDL_GPU_TEXTUREWRITEOPTIONS_SAFE /* FIXME: should we cycle here? */
    );

    renderer->textureUploadBufferOffset += dataLength;
}

static void SDLGPU_SetTextureData2D(
	FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        textureHandle->texture,
        (uint32_t) x,
        (uint32_t) y,
        0,
        (uint32_t) w,
        (uint32_t) h,
        1,
        0,
        (uint32_t) level,
        data,
        dataLength
    );
}

static void SDLGPU_SetTextureData3D(
    FNA3D_Renderer *driverData,
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
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        textureHandle->texture,
        (uint32_t) x,
        (uint32_t) y,
        (uint32_t) z,
        (uint32_t) w,
        (uint32_t) h,
        (uint32_t) d,
        0,
        (uint32_t) level,
        data,
        dataLength
    );
}

static void SDLGPU_SetTextureDataCube(
    FNA3D_Renderer *driverData,
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
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        textureHandle->texture,
        (uint32_t) x,
        (uint32_t) y,
        0,
        (uint32_t) w,
        (uint32_t) h,
        1,
        (uint32_t) cubeMapFace,
        (uint32_t) level,
        data,
        dataLength
    );
}

static void SDLGPU_SetTextureDataYUV(
    FNA3D_Renderer *driverData,
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
    SDLGPU_TextureHandle *yHandle = (SDLGPU_TextureHandle*) y;
    SDLGPU_TextureHandle *uHandle = (SDLGPU_TextureHandle*) u;
    SDLGPU_TextureHandle *vHandle = (SDLGPU_TextureHandle*) v;

	int32_t yDataLength = BytesPerImage(yWidth, yHeight, FNA3D_SURFACEFORMAT_ALPHA8);
	int32_t uvDataLength = BytesPerImage(uvWidth, uvHeight, FNA3D_SURFACEFORMAT_ALPHA8);

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        yHandle->texture,
        0,
        0,
        0,
        (uint32_t) yWidth,
        (uint32_t) yHeight,
        1,
        0,
        0,
        data,
        yDataLength
    );

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        uHandle->texture,
        0,
        0,
        0,
        (uint32_t) uvWidth,
        (uint32_t) uvHeight,
        1,
        0,
        0,
        (uint8_t*) data + yDataLength,
        uvDataLength
    );

    SDLGPU_INTERNAL_SetTextureData(
        (SDLGPU_Renderer*) driverData,
        vHandle->texture,
        0,
        0,
        0,
        (uint32_t) uvWidth,
        (uint32_t) uvHeight,
        1,
        0,
        0,
        (uint8_t*) data + yDataLength + uvDataLength,
        uvDataLength
    );
}

static void SDLGPU_GetTextureData2D(
    FNA3D_Renderer *driverData,
	FNA3D_Texture *texture,
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	int32_t level,
	void* data,
	int32_t dataLength
) {
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDLGPU_INTERNAL_GetTextureData(
        (SDLGPU_Renderer*) driverData,
        textureHandle->texture,
        (uint32_t) x,
        (uint32_t) y,
        0,
        (uint32_t) w,
        (uint32_t) h,
        1,
        0,
        level,
        data,
        (uint32_t) dataLength
    );
}

static void SDLGPU_GetTextureData3D(
    FNA3D_Renderer *driverData,
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
    FNA3D_LogError(
		"GetTextureData3D is unsupported!"
	);
}

static void SDLGPU_GetTextureDataCube(
    FNA3D_Renderer *driverData,
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
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDLGPU_INTERNAL_GetTextureData(
        (SDLGPU_Renderer*) driverData,
        textureHandle->texture,
        (uint32_t) x,
        (uint32_t) y,
        0,
        (uint32_t) w,
        (uint32_t) h,
        1,
        (uint32_t) cubeMapFace,
        level,
        data,
        dataLength
    );
}

/* Buffers */

static FNA3D_Buffer* SDLGPU_GenVertexBuffer(
    FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    return (FNA3D_Buffer*) SDL_GpuCreateGpuBuffer(
        renderer->device,
        SDL_GPU_BUFFERUSAGE_VERTEX_BIT,
        sizeInBytes
    );
}

static FNA3D_Buffer* SDLGPU_GenIndexBuffer(
    FNA3D_Renderer *driverData,
	uint8_t dynamic,
	FNA3D_BufferUsage usage,
	int32_t sizeInBytes
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    return (FNA3D_Buffer*) SDL_GpuCreateGpuBuffer(
        renderer->device,
        SDL_GPU_BUFFERUSAGE_INDEX_BIT,
        sizeInBytes
    );
}

static void SDLGPU_AddDisposeVertexBuffer(
    FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDL_GpuQueueDestroyGpuBuffer(
        renderer->device,
        (SDL_GpuBuffer*) buffer
    );
}

static void SDLGPU_AddDisposeIndexBuffer(
    FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;

    SDL_GpuQueueDestroyGpuBuffer(
        renderer->device,
        (SDL_GpuBuffer*) buffer
    );
}

static void SDLGPU_INTERNAL_SetBufferData(
    SDLGPU_Renderer *renderer,
    SDL_GpuBuffer *buffer,
    uint32_t dstOffset,
    void *data,
    uint32_t dataLength,
    SDL_GpuBufferWriteOptions option
) {
    SDL_GpuBufferCopy transferCopyParams;
    SDL_GpuBufferCopy uploadParams;

    /* Recreate transfer buffer if necessary */
    if (renderer->bufferUploadBufferOffset + dataLength >= renderer->bufferUploadBufferSize)
    {
        SDL_GpuQueueDestroyTransferBuffer(
            renderer->device,
            renderer->bufferUploadBuffer
        );

        renderer->bufferUploadBufferSize = dataLength;
        renderer->bufferUploadBufferOffset = 0;
        renderer->bufferUploadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_BUFFER,
            renderer->bufferUploadBufferSize
        );
    }

    transferCopyParams.srcOffset = 0;
    transferCopyParams.dstOffset = renderer->bufferUploadBufferOffset;
    transferCopyParams.size = dataLength;

    SDL_GpuSetTransferData(
        renderer->device,
        data,
        renderer->bufferUploadBuffer,
        &transferCopyParams,
        renderer->bufferUploadBufferOffset == 0 ? SDL_GPU_TRANSFEROPTIONS_CYCLE : SDL_GPU_TRANSFEROPTIONS_UNSAFE
    );

    uploadParams.srcOffset = renderer->bufferUploadBufferOffset;
    uploadParams.dstOffset = dstOffset;
    uploadParams.size = dataLength;

    SDL_GpuUploadToBuffer(
        renderer->device,
        renderer->commandBuffer,
        renderer->bufferUploadBuffer,
        buffer,
        &uploadParams,
        option
    );

    renderer->bufferUploadBufferOffset += dataLength;
}

static void SDLGPU_SetVertexBufferData(
	FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride,
	FNA3D_SetDataOptions options
) {
    SDL_GpuBufferWriteOptions option;

    /* FIXME: what about NONE? */
    if (options == FNA3D_SETDATAOPTIONS_DISCARD)
    {
        option = SDL_GPU_BUFFERWRITEOPTIONS_CYCLE;
    }
    else
    {
        option = SDL_GPU_BUFFERWRITEOPTIONS_UNSAFE;
    }

    SDLGPU_INTERNAL_SetBufferData(
        (SDLGPU_Renderer*) driverData,
        (SDL_GpuBuffer*) buffer,
        (uint32_t) offsetInBytes,
        data,
        elementCount * vertexStride,
        option
    );
}

static SDLGPU_SetIndexBufferData(
    FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength,
	FNA3D_SetDataOptions options
) {
    SDL_GpuBufferWriteOptions option;

    /* FIXME: what about NONE? */
    if (options == FNA3D_SETDATAOPTIONS_DISCARD)
    {
        option = SDL_GPU_BUFFERWRITEOPTIONS_CYCLE;
    }
    else
    {
        option = SDL_GPU_BUFFERWRITEOPTIONS_UNSAFE;
    }

    SDLGPU_INTERNAL_SetBufferData(
        (SDLGPU_Renderer*) driverData,
        (SDL_GpuBuffer*) buffer,
        (uint32_t) offsetInBytes,
        data,
        dataLength,
        options
    );
}

static void SDLGPU_INTERNAL_GetBufferData(
    SDLGPU_Renderer *renderer,
    SDL_GpuBuffer *buffer,
    uint32_t offset,
    void *data,
    uint32_t dataLength
) {
    SDL_GpuBufferCopy downloadParams;
    SDL_GpuBufferCopy copyParams;

    /* Flush and stall so the data is up to date */
    SDLGPU_INTERNAL_FlushCommandsAndStall(renderer);

    /* Create transfer buffer if necessary */
    if (renderer->bufferDownloadBuffer == NULL)
    {
        renderer->bufferDownloadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_BUFFER,
            dataLength
        );

        renderer->bufferDownloadBufferSize = dataLength;
    }
    else if (renderer->bufferDownloadBufferSize < dataLength)
    {
        SDL_GpuQueueDestroyTransferBuffer(
            renderer->device,
            renderer->bufferDownloadBuffer
        );

        renderer->bufferDownloadBuffer = SDL_GpuCreateTransferBuffer(
            renderer->device,
            SDL_GPU_TRANSFERUSAGE_TEXTURE,
            dataLength
        );

        renderer->bufferDownloadBufferSize = dataLength;
    }

    /* Set up buffer download */
    downloadParams.srcOffset = offset;
    downloadParams.dstOffset = 0;
    downloadParams.size = dataLength;

    SDL_GpuDownloadFromBuffer(
        renderer->device,
        buffer,
        renderer->bufferDownloadBuffer,
        &copyParams,
        SDL_GPU_TRANSFEROPTIONS_CYCLE
    );

    /* Copy into data pointer */
    copyParams.srcOffset = 0;
    copyParams.dstOffset = 0;
    copyParams.size = dataLength;

    SDL_GpuGetTransferData(
        renderer->device,
        renderer->bufferDownloadBuffer,
        data,
        &copyParams
    );
}

static void SDLGPU_GetVertexBufferData(
    FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t elementCount,
	int32_t elementSizeInBytes,
	int32_t vertexStride
) {
    SDLGPU_INTERNAL_GetBufferData(
        (SDLGPU_Renderer*) driverData,
        (SDL_GpuBuffer*) buffer,
        offsetInBytes,
        data,
        elementCount * vertexStride
    );
}

static void SDLGPU_GetIndexBufferData(
    FNA3D_Renderer *driverData,
	FNA3D_Buffer *buffer,
	int32_t offsetInBytes,
	void* data,
	int32_t dataLength
) {
    SDLGPU_INTERNAL_GetBufferData(
        (SDLGPU_Renderer*) driverData,
        (SDL_GpuBuffer*) buffer,
        offsetInBytes,
        data,
        dataLength
    );
}

/* Effects */

static void SDLGPU_CreateEffect(
    FNA3D_Renderer *driverData,
	uint8_t *effectCode,
	uint32_t effectCodeLength,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    MOJOSHADER_effectShaderContext shaderBackend;
    SDLGPU_Effect *result;
    int32_t i;

	shaderBackend.shaderContext = renderer->mojoshaderContext;
	shaderBackend.compileShader = (MOJOSHADER_compileShaderFunc) MOJOSHADER_sdlCompileShader;
	shaderBackend.shaderAddRef = (MOJOSHADER_shaderAddRefFunc) MOJOSHADER_sdlShaderAddRef;
	shaderBackend.deleteShader = (MOJOSHADER_deleteShaderFunc) MOJOSHADER_sdlDeleteShader;
	shaderBackend.getParseData = (MOJOSHADER_getParseDataFunc) MOJOSHADER_sdlGetShaderParseData;
	shaderBackend.bindShaders = (MOJOSHADER_bindShadersFunc) MOJOSHADER_sdlBindShaders;
	shaderBackend.getBoundShaders = (MOJOSHADER_getBoundShadersFunc) MOJOSHADER_sdlGetBoundShaders;
	shaderBackend.mapUniformBufferMemory = (MOJOSHADER_mapUniformBufferMemoryFunc) MOJOSHADER_sdlMapUniformBufferMemory;
	shaderBackend.unmapUniformBufferMemory = (MOJOSHADER_unmapUniformBufferMemoryFunc) MOJOSHADER_sdlUnmapUniformBufferMemory;
	shaderBackend.getError = (MOJOSHADER_getErrorFunc) MOJOSHADER_sdlGetError;
	shaderBackend.m = NULL;
	shaderBackend.f = NULL;
	shaderBackend.malloc_data = NULL;

    *effectData = MOJOSHADER_compileEffect(
        effectCode,
        effectCodeLength,
        NULL,
        0,
        NULL,
        0,
        &shaderBackend
    );

    for (i = 0; i < (*effectData)->error_count; i += 1)
	{
		FNA3D_LogError(
			"MOJOSHADER_compileEffect Error: %s",
			(*effectData)->errors[i].error
		);
	}

    result = (SDLGPU_Effect*) SDL_malloc(sizeof(SDLGPU_Effect));
    result->effect = *effectData;
    *effect = (FNA3D_Effect*) result;
}

static void SDLGPU_CloneEffect(
    FNA3D_Renderer *driverData,
	FNA3D_Effect *cloneSource,
	FNA3D_Effect **effect,
	MOJOSHADER_effect **effectData
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_Effect *sdlCloneSource = (SDLGPU_Effect*) cloneSource;
    SDLGPU_Effect *result;

    *effectData = MOJOSHADER_cloneEffect(sdlCloneSource->effect);
	if (*effectData == NULL)
	{
		FNA3D_LogError(MOJOSHADER_sdlGetError(renderer->mojoshaderContext));
	}

    result = (SDLGPU_Effect*) SDL_malloc(sizeof(SDLGPU_Effect));
    result->effect = *effectData;
    *effect = (FNA3D_Effect*) result;
}

/* TODO: check if we need to defer this */
static void SDLGPU_AddDisposeEffect(
    FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
    MOJOSHADER_effect *effectData = gpuEffect->effect;

    if (effectData == renderer->currentEffect)
    {
        MOJOSHADER_effectEndPass(renderer->currentEffect);
        MOJOSHADER_effectEnd(renderer->currentEffect);
        renderer->currentEffect = NULL;
        renderer->currentTechnique = NULL;
        renderer->currentPass = 0;
    }
    MOJOSHADER_deleteEffect(effectData);
    SDL_free(gpuEffect);
}

static void SDLGPU_SetEffectTechnique(
    FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectTechnique *technique
) {
    SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
    MOJOSHADER_effectSetTechnique(gpuEffect->effect, technique);
}

static void SDLGPU_ApplyEffect(
    FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	uint32_t pass,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_Effect *gpuEffect = (SDLGPU_Effect*) effect;
    MOJOSHADER_effect *effectData = gpuEffect->effect;
    const MOJOSHADER_effectTechnique *technique = gpuEffect->effect->current_technique;
    uint32_t numPasses;

    renderer->needFragmentSamplerBind = 1;
    renderer->needVertexSamplerBind = 1;
    renderer->needNewGraphicsPipeline = 1;

    if (effectData == renderer->currentEffect)
    {
        if (
            technique == renderer->currentTechnique &&
            pass == renderer->currentPass
        ) {
            MOJOSHADER_effectCommitChanges(
                renderer->currentEffect
            );

            return;
        }

        MOJOSHADER_effectEndPass(renderer->currentEffect);
        MOJOSHADER_effectBeginPass(renderer->currentEffect, pass);
        renderer->currentTechnique = technique;
        renderer->currentPass = pass;

        return;
    }
    else if (renderer->currentEffect != NULL)
    {
        MOJOSHADER_effectEndPass(renderer->currentEffect);
        MOJOSHADER_effectEnd(renderer->currentEffect);
    }

    MOJOSHADER_effectBegin(
        effectData,
        &numPasses,
        0,
        stateChanges
    );

    MOJOSHADER_effectBeginPass(effectData, pass);
    renderer->currentEffect = effectData;
    renderer->currentTechnique = technique;
    renderer->currentPass = pass;
}

static void SDLGPU_BeginPassRestore(
    FNA3D_Renderer *driverData,
	FNA3D_Effect *effect,
	MOJOSHADER_effectStateChanges *stateChanges
) {
    MOJOSHADER_effect *effectData = ((SDLGPU_Effect*) effect)->effect;
	uint32_t whatever;

	MOJOSHADER_effectBegin(
			effectData,
			&whatever,
			1,
			stateChanges
	);
	MOJOSHADER_effectBeginPass(effectData, 0);
}

static void SDLGPU_EndPassRestore(
	FNA3D_Renderer *driverData,
	FNA3D_Effect *effect
) {
	MOJOSHADER_effect *effectData = ((SDLGPU_Effect*) effect)->effect;
	MOJOSHADER_effectEndPass(effectData);
	MOJOSHADER_effectEnd(effectData);
}

/* Queries */

static FNA3D_Query* SDLGPU_CreateQuery(FNA3D_Renderer *driverData)
{
    /* TODO */
    return NULL;
}

static void SDLGPU_AddDisposeQuery(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
    /* TODO */
}

static void SDLGPU_QueryBegin(
    FNA3D_Renderer *driverData,
    FNA3D_Query *query
) {
    /* TODO */
}

static void SDLGPU_QueryEnd(
    FNA3D_Renderer *driverData,
    FNA3D_Query *query
) {
    /* TODO */
}

static uint8_t SDLGPU_QueryComplete(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
    /* TODO */
    return 1;
}

static int32_t SDLGPU_QueryPixelCount(
	FNA3D_Renderer *driverData,
	FNA3D_Query *query
) {
    /* TODO */
    return 0;
}

static uint8_t SDLGPU_SupportsDXT1(FNA3D_Renderer *driverData)
{
    /* TODO */
    return 1;
}

static uint8_t SDLGPU_SupportsS3TC(FNA3D_Renderer *driverData)
{
    /* TODO */
    return 1;
}

static uint8_t SDLGPU_SupportsBC7(FNA3D_Renderer *driverData)
{
    /* TODO */
    return 1;
}

static uint8_t SDLGPU_SupportsHardwareInstancing(FNA3D_Renderer *driverData)
{
    /* TODO */
    return 1;
}

static uint8_t SDLGPU_SupportsNoOverwrite(FNA3D_Renderer *driverData)
{
    /* TODO */
	return 1;
}

static uint8_t SDLGPU_SupportsSRGBRenderTargets(FNA3D_Renderer *driverData)
{
    /* TODO */
    return 1;
}

static void SDLGPU_GetMaxTextureSlots(
    FNA3D_Renderer *driverData,
	int32_t *textures,
	int32_t *vertexTextures
) {
    /* TODO */
    *textures = MAX_TEXTURE_SAMPLERS;
    *vertexTextures = MAX_VERTEXTEXTURE_SAMPLERS;
}

static int32_t SDLGPU_GetMaxMultiSampleCount(
	FNA3D_Renderer *driverData,
	FNA3D_SurfaceFormat format,
	int32_t multiSampleCount
) {
    /* TODO */
    return 8;
}

/* Debugging */

static void SDLGPU_SetStringMarker(
    FNA3D_Renderer *driverData,
    const char *text
) {
    /* TODO */
}

static void SDLGPU_SetTextureName(
    FNA3D_Renderer *driverData,
    FNA3D_Texture *texture,
    const char *text
) {
    SDLGPU_Renderer *renderer = (SDLGPU_Renderer*) driverData;
    SDLGPU_TextureHandle *textureHandle = (SDLGPU_TextureHandle*) texture;

    SDL_GpuSetTextureName(
        renderer->device,
        textureHandle->texture,
        text
    );
}

/* External Interop */

static void SDLGPU_GetSysRenderer(
	FNA3D_Renderer *driverData,
	FNA3D_SysRendererEXT *sysrenderer
) {
    /* TODO */
    SDL_memset(sysrenderer, '\0', sizeof(FNA3D_SysRendererEXT));
}

static FNA3D_Texture* SDLGPU_CreateSysTexture(
	FNA3D_Renderer *driverData,
	FNA3D_SysTextureEXT *systexture
) {
    /* TODO */
    return NULL;
}

/* Initialization */

static uint8_t SDLGPU_PrepareWindowAttributes(uint32_t *flags)
{
    SDL_GpuBackend selectedBackend =
        SDL_GpuSelectBackend(
            preferredBackends,
            2,
            flags
        );

    if (selectedBackend == SDL_GPU_BACKEND_INVALID)
    {
        FNA3D_LogError("Failed to select backend!");
        return 0;
    }

    return 1;
}

static FNA3D_Device* SDLGPU_CreateDevice(
    FNA3D_PresentationParameters *presentationParameters,
    uint8_t debugMode
) {
    SDLGPU_Renderer *renderer;
    SDL_GpuDevice *device;
    FNA3D_Device *result;

    requestedPresentationParameters = *presentationParameters;
    device = SDL_GpuCreateDevice(debugMode);

    if (device == NULL)
    {
        FNA3D_LogError("Failed to create SDLGPU device!");
        return NULL;
    }

    result = SDL_malloc(sizeof(FNA3D_Device));
    ASSIGN_DRIVER(SDLGPU)

    renderer = SDL_malloc(sizeof(SDLGPU_Renderer));
    SDL_memset(renderer, '\0', sizeof(SDLGPU_Renderer));
    renderer->device = device;

    result->driverData = (FNA3D_Renderer*) renderer;

    if (!SDL_GpuClaimWindow(
        renderer->device,
        presentationParameters->deviceWindowHandle,
        XNAToSDL_PresentMode[presentationParameters->presentationInterval]
    )) {
        FNA3D_LogError("Failed to claim window!");
        return NULL;
    }

    renderer->mainWindowHandle = presentationParameters->deviceWindowHandle;

    SDLGPU_INTERNAL_CreateFauxBackbuffer(
        renderer,
        presentationParameters
    );

    if (renderer->fauxBackbufferColor == NULL)
    {
        FNA3D_LogError("Failed to create faux backbuffer!");
        return NULL;
    }

    renderer->textureUploadBufferSize = 8388608; /* 8 MiB */
    renderer->textureUploadBufferOffset = 0;
    renderer->textureUploadBuffer = SDL_GpuCreateTransferBuffer(
        renderer->device,
        SDL_GPU_TRANSFERUSAGE_TEXTURE,
        renderer->textureUploadBufferSize
    );

    if (renderer->textureUploadBuffer == NULL)
    {
        FNA3D_LogError("Failed to create texture transfer buffer!");
        return NULL;
    }

    renderer->bufferUploadBufferSize = 8388608; /* 8 MiB */
    renderer->bufferUploadBufferOffset = 0;
    renderer->bufferUploadBuffer = SDL_GpuCreateTransferBuffer(
        renderer->device,
        SDL_GPU_TRANSFERUSAGE_BUFFER,
        renderer->bufferUploadBufferSize
    );

    renderer->mojoshaderContext = MOJOSHADER_sdlCreateContext(
        device,
        selectedBackend,
        NULL,
        NULL,
        NULL
    );

    /* Acquire command buffer, we are ready for takeoff */

    SDLGPU_ResetCommandBufferState(renderer);

    return result;
}

FNA3D_Driver SDLGPUDriver = {
    "SDLGPU",
    SDLGPU_PrepareWindowAttributes,
    SDLGPU_CreateDevice
};

#else

extern int this_tu_is_empty;

#endif /* FNA3D_DRIVER_SDL */
