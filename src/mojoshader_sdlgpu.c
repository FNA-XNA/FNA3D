/* Begin mojoshader.h additions */

#include "mojoshader.h" /* Pretend this is the rest of the header */

/* SDL_GPU interface... */

typedef struct MOJOSHADER_sdlContext MOJOSHADER_sdlContext;
typedef struct MOJOSHADER_sdlShader MOJOSHADER_sdlShader;
typedef struct MOJOSHADER_sdlProgram MOJOSHADER_sdlProgram;

#ifndef SDL_GPU_H
typedef struct SDL_GpuDevice SDL_GpuDevice;
typedef struct SDL_GpuShaderModule SDL_GpuShaderModule;
typedef struct SDL_GpuCommandBuffer SDL_GpuCommandBuffer;
#endif /* SDL_GPU_H */

/*
 * Prepares a context to manage SDL_gpu shaders.
 *
 * You do not need to call this if all you want is MOJOSHADER_parse().
 *
 * (device) refers to the SDL_GpuDevice.
 *
 * You can only have one MOJOSHADER_sdlContext per actual SDL_gpu context, or
 *  undefined behaviour will result.
 *
 * As MojoShader requires some memory to be allocated, you may provide a
 *  custom allocator to this function, which will be used to allocate/free
 *  memory. They function just like malloc() and free(). We do not use
 *  realloc(). If you don't care, pass NULL in for the allocator functions.
 *  If your allocator needs instance-specific data, you may supply it with the
 *  (malloc_d) parameter. This pointer is passed as-is to your (m) and (f)
 *  functions.
 *
 * Returns a new context on success, NULL on error.
 */
DECLSPEC MOJOSHADER_sdlContext *MOJOSHADER_sdlCreateContext(SDL_GpuDevice *device,
                                                            MOJOSHADER_malloc m,
                                                            MOJOSHADER_free f,
                                                            void *malloc_d);

/*
 * Get any error state we might have picked up.
 *
 * Returns a human-readable string. This string is for debugging purposes, and
 *  not guaranteed to be localized, coherent, or user-friendly in any way.
 *  It's for programmers!
 *
 * The latest error may remain between calls. New errors replace any existing
 *  error. Don't check this string for a sign that an error happened, check
 *  return codes instead and use this for explanation when debugging.
 *
 * Do not free the returned string: it's a pointer to a static internal
 *  buffer. Do not keep the pointer around, either, as it's likely to become
 *  invalid as soon as you call into MojoShader again.
 *
 * This call does NOT require a valid MOJOSHADER_sdlContext to have been made
 *  current. The error buffer is shared between contexts, so you can get
 *  error results from a failed MOJOSHADER_sdlCreateContext().
 */
DECLSPEC const char *MOJOSHADER_sdlGetError(MOJOSHADER_sdlContext *ctx);

/*
 * Deinitialize MojoShader's SDL_gpu shader management.
 *
 * You must call this once, while your SDL_GpuDevice is still valid. This should
 * be the last MOJOSHADER_sdl* function you call until you've prepared a context
 * again.
 *
 * This will clean up resources previously allocated, and may call into SDL_gpu.
 *
 * This will not clean up shaders and programs you created! Please call
 *  MOJOSHADER_sdlDeleteShader() and MOJOSHADER_sdlDeleteProgram() to clean
 *  those up before calling this function!
 *
 * This function destroys the MOJOSHADER_sdlContext you pass it.
 */
DECLSPEC void MOJOSHADER_sdlDestroyContext(MOJOSHADER_sdlContext *ctx);

/*
 * Compile a buffer of Direct3D shader bytecode into an SDL_gpu shader module.
 *
 *   (tokenbuf) is a buffer of Direct3D shader bytecode.
 *   (bufsize) is the size, in bytes, of the bytecode buffer.
 *   (swiz), (swizcount), (smap), and (smapcount) are passed to
 *   MOJOSHADER_parse() unmolested.
 *
 * Returns NULL on error, or a shader handle on success.
 *
 * Compiled shaders from this function may not be shared between contexts.
 */
DECLSPEC MOJOSHADER_sdlShader *MOJOSHADER_sdlCompileShader(MOJOSHADER_sdlContext *ctx,
                                                           const char *mainfn,
                                                           const unsigned char *tokenbuf,
                                                           const unsigned int bufsize,
                                                           const MOJOSHADER_swizzle *swiz,
                                                           const unsigned int swizcount,
                                                           const MOJOSHADER_samplerMap *smap,
                                                           const unsigned int smapcount);

/*
 * Increments a shader's internal refcount.
 *
 * To decrement the refcount, call MOJOSHADER_sdlDeleteShader().
 */
DECLSPEC void MOJOSHADER_sdlShaderAddRef(MOJOSHADER_sdlShader *shader);

/*
 * Decrements a shader's internal refcount, and deletes if the refcount is zero.
 *
 * To increment the refcount, call MOJOSHADER_sdlShaderAddRef().
 */
DECLSPEC void MOJOSHADER_sdlDeleteShader(MOJOSHADER_sdlContext *ctx,
                                         MOJOSHADER_sdlShader *shader);

/*
 * Get the MOJOSHADER_parseData structure that was produced from the
 *  call to MOJOSHADER_sdlCompileShader().
 *
 * This data is read-only, and you should NOT attempt to free it. This
 *  pointer remains valid until the shader is deleted.
 */
DECLSPEC const MOJOSHADER_parseData *MOJOSHADER_sdlGetShaderParseData(
                                                  MOJOSHADER_sdlShader *shader);

/*
 * Link a vertex and pixel shader into a working SDL_gpu shader program.
 *  (vshader) or (pshader) can NOT be NULL, unlike OpenGL.
 *
 * You can reuse shaders in various combinations across
 *  multiple programs, by relinking different pairs.
 *
 * It is illegal to give a vertex shader for (pshader) or a pixel shader
 *  for (vshader).
 *
 * Once you have successfully linked a program, you may render with it.
 *
 * Returns NULL on error, or a program handle on success.
 */
DECLSPEC MOJOSHADER_sdlProgram *MOJOSHADER_sdlLinkProgram(MOJOSHADER_sdlContext *context,
                                                          MOJOSHADER_sdlShader *vshader,
                                                          MOJOSHADER_sdlShader *pshader);

/*
 * This binds the program to the active context, and does nothing particularly
 * special until you start working with uniform buffers or shader modules.
 *
 * After binding a program, you should update any uniforms you care about
 *  with MOJOSHADER_sdlMapUniformBufferMemory() (etc), set any vertex arrays
 *  using MOJOSHADER_sdlGetVertexAttribLocation(), and finally call
 *  MOJOSHADER_sdlGetShaderModules() to get the final modules. Then you may
 *  begin building your pipeline state objects.
 */
DECLSPEC void MOJOSHADER_sdlBindProgram(MOJOSHADER_sdlContext *context,
                                        MOJOSHADER_sdlProgram *program);

/*
 * Free the resources of a linked program. This will delete the shader modules
 *  and free memory.
 *
 * If the program is currently bound by MOJOSHADER_sdlBindProgram(), it will
 *  be deleted as soon as it becomes unbound.
 */
DECLSPEC void MOJOSHADER_sdlDeleteProgram(MOJOSHADER_sdlContext *context,
                                          MOJOSHADER_sdlProgram *program);

/*
 * This "binds" individual shaders, which effectively means the context
 *  will store these shaders for later retrieval. No actual binding or
 *  pipeline creation is performed.
 *
 * This function is only for convenience, specifically for compatibility
 *  with the effects API.
 */
DECLSPEC void MOJOSHADER_sdlBindShaders(MOJOSHADER_sdlContext *ctx,
                                        MOJOSHADER_sdlShader *vshader,
                                        MOJOSHADER_sdlShader *pshader);

/*
 * This queries for the shaders currently bound to the active context.
 *
 * This function is only for convenience, specifically for compatibility
 *  with the effects API.
 */
DECLSPEC void MOJOSHADER_sdlGetBoundShaders(MOJOSHADER_sdlContext *ctx,
                                            MOJOSHADER_sdlShader **vshader,
                                            MOJOSHADER_sdlShader **pshader);

/*
 * Fills register pointers with pointers that are directly used to push uniform
 *  data to the Vulkan shader context.
 *
 * This function is really just for the effects API, you should NOT be using
 *  this unless you know every single line of MojoShader from memory.
 */
DECLSPEC void MOJOSHADER_sdlMapUniformBufferMemory(MOJOSHADER_sdlContext *ctx,
                                                   float **vsf, int **vsi, unsigned char **vsb,
                                                   float **psf, int **psi, unsigned char **psb);

/*
 * Tells the context that you are done with the memory mapped by
 *  MOJOSHADER_sdlMapUniformBufferMemory().
 */
DECLSPEC void MOJOSHADER_sdlUnmapUniformBufferMemory(MOJOSHADER_sdlContext *ctx);

/*
 * Returns the minimum required size of the uniform buffer for this shader.
 *  You will need this to fill out the SDL_GpuGraphicsPipelineCreateInfo struct.
 */
DECLSPEC int MOJOSHADER_sdlGetUniformBufferSize(MOJOSHADER_sdlShader *shader);

/*
 * Pushes the uniform buffer updates for the currently bound program.
 *
 * This function will record calls to SDL_GpuPush*ShaderUniforms into the
 *  passed command buffer.
 */
DECLSPEC void MOJOSHADER_sdlUpdateUniformBuffers(MOJOSHADER_sdlContext *ctx,
                                                 SDL_GpuCommandBuffer *cb);

/*
 * Return the location of a vertex attribute for the given shader.
 *
 * (usage) and (index) map to Direct3D vertex declaration values: COLOR1 would
 *  be MOJOSHADER_USAGE_COLOR and 1.
 *
 * The return value is the index of the attribute to be used to create
 *  an SDL_GpuVertexAttribute, or -1 if the stream is not used.
 */
DECLSPEC int MOJOSHADER_sdlGetVertexAttribLocation(MOJOSHADER_sdlShader *vert,
                                                   MOJOSHADER_usage usage,
                                                   int index);

/*
 * Get the SDL_GpuShaderModules from the currently bound shader program.
 */
DECLSPEC void MOJOSHADER_sdlGetShaderModules(MOJOSHADER_sdlContext *ctx,
                                             SDL_GpuShaderModule **vmodule,
                                             SDL_GpuShaderModule **pmodule);

/* End mojoshader.h additions */

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

#include <SDL3/SDL.h>

/* SDL_shader */

#ifndef SDL3_SHADER_LIBRARY
#if defined(_WIN32)
#define SDL3_SHADER_LIBRARY "SDL3_shader.dll"
#elif defined(__APPLE__)
#define SDL3_SHADER_LIBRARY "libSDL3_shader.0.dylib"
#else
#define SDL3_SHADER_LIBRARY "libSDL3_shader.so.0"
#endif
#endif /* SDL3_SHADER_LIBRARY */

typedef const char* (*SDLCALL SHD_TranslateFromSPIRV_func)(SDL_GpuBackend backend,
                                                           const char* spirv,
                                                           size_t spirv_size,
                                                           size_t* output_size);

/* Max entries for each register file type */
#define MAX_REG_FILE_F 8192
#define MAX_REG_FILE_I 2047
#define MAX_REG_FILE_B 2047

struct MOJOSHADER_sdlContext
{
    SDL_GpuDevice *device;
    SDL_GpuBackend backend;
    const char *profile;

    MOJOSHADER_malloc malloc_fn;
    MOJOSHADER_free free_fn;
    void *malloc_data;

    void *sdl_shader_dll;
    SHD_TranslateFromSPIRV_func SHD_TranslateFromSPIRV;

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

    MOJOSHADER_sdlShader *bound_vshader;
    MOJOSHADER_sdlShader *bound_pshader;
    MOJOSHADER_sdlProgram *bound_program;
    HashTable *linker_cache;
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

/* Error state... */

static char error_buffer[1024] = { '\0' };

static void set_error(const char *str)
{
    snprintf(error_buffer, sizeof (error_buffer), "%s", str);
} // set_error

static inline void out_of_memory(void)
{
    set_error("out of memory");
} // out_of_memory

/* Internals */

typedef struct BoundShaders
{
    MOJOSHADER_sdlShader *vertex;
    MOJOSHADER_sdlShader *fragment;
} BoundShaders;

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
    SDL_GpuCommandBuffer *commandBuffer,
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
    } // if
    else
    {
        regF = ctx->ps_reg_file_f;
        regI = ctx->ps_reg_file_i;
        regB = ctx->ps_reg_file_b;
    } // else
    content_size = 0;

    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        const int32_t size = arrayCount ? arrayCount : 1;
        content_size += size * 16;
    } // for

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
        } // switch

        offset += size * 16;
    } // for

    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
    {
        SDL_GpuPushVertexShaderUniforms(
            ctx->device,
            commandBuffer,
            contents,
            content_size
        );
    } // if
    else
    {
        SDL_GpuPushFragmentShaderUniforms(
            ctx->device,
            commandBuffer,
            contents,
            content_size
        );
    } // else

    ctx->free_fn(contents, ctx->malloc_data);
} // update_uniform_buffer

/* Public API */

MOJOSHADER_sdlContext *MOJOSHADER_sdlCreateContext(
    SDL_GpuDevice *device,
    MOJOSHADER_malloc m,
    MOJOSHADER_free f,
    void *malloc_d
) {
    MOJOSHADER_sdlContext* resultCtx;
    void *sdl_shader;
    SHD_TranslateFromSPIRV_func SHD_TranslateFromSPIRV;
    SDL_GpuBackend backend = SDL_GpuGetBackend(device);

    if (m == NULL) m = MOJOSHADER_internal_malloc;
    if (f == NULL) f = MOJOSHADER_internal_free;

    if (backend == SDL_GPU_BACKEND_VULKAN)
    {
        sdl_shader = NULL;
        SHD_TranslateFromSPIRV = NULL;
    } // if
    else
    {
        sdl_shader = SDL_LoadObject(SDL3_SHADER_LIBRARY);
        if (sdl_shader == NULL)
        {
            set_error("Backend requires SDL_shader, which is unavailable");
            return NULL;
        } // if
        SHD_TranslateFromSPIRV = (SHD_TranslateFromSPIRV_func) SDL_LoadFunction(sdl_shader,
                                                                                "SHD_TranslateFromSPIRV");
        if (SHD_TranslateFromSPIRV == NULL)
        {
            set_error("Backend requires SHD_TranslateFromSPIRV, which is unavailable");
            SDL_UnloadObject(sdl_shader);
            return NULL;
        } // if
    } // else

    resultCtx = (MOJOSHADER_sdlContext*) m(sizeof(MOJOSHADER_sdlContext), malloc_d);
    if (resultCtx == NULL)
    {
        out_of_memory();
        goto init_fail;
    } // if

    SDL_memset(resultCtx, '\0', sizeof(MOJOSHADER_sdlContext));
    resultCtx->device = device;
    resultCtx->backend = backend;
    resultCtx->profile = "spirv"; /* always use spirv and interop with SDL3_shader */

    resultCtx->sdl_shader_dll = sdl_shader;
    resultCtx->SHD_TranslateFromSPIRV = SHD_TranslateFromSPIRV;

    resultCtx->malloc_fn = m;
    resultCtx->free_fn = f;
    resultCtx->malloc_data = malloc_d;

    return resultCtx;

init_fail:
    if (resultCtx != NULL)
        f(resultCtx, malloc_d);
    return NULL;
} // MOJOSHADER_sdlCreateContext

const char *MOJOSHADER_sdlGetError(
    MOJOSHADER_sdlContext *ctx
) {
    return error_buffer;
} // MOJOSHADER_sdlGetError

void MOJOSHADER_sdlDestroyContext(
    MOJOSHADER_sdlContext *ctx
) {
    if (ctx->linker_cache)
        hash_destroy(ctx->linker_cache, ctx);

    if (ctx->sdl_shader_dll)
        SDL_UnloadObject(ctx->sdl_shader_dll);

    ctx->free_fn(ctx, ctx->malloc_data);
} // MOJOSHADER_sdlDestroyContext

static uint16_t shaderTagCounter = 1;

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
    MOJOSHADER_sdlShader *shader = NULL;;

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
    } // if

    shader = (MOJOSHADER_sdlShader*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlShader), ctx->malloc_data);
    if (shader == NULL)
    {
        out_of_memory();
        goto parse_shader_fail;
    } // if

    shader->parseData = pd;
    shader->refcount = 1;
    shader->tag = shaderTagCounter++;
    return shader;

parse_shader_fail:
    MOJOSHADER_freeParseData(pd);
    if (shader != NULL)
        ctx->free_fn(shader, ctx->malloc_data);
    return NULL;
} // MOJOSHADER_sdlCompileShader

MOJOSHADER_sdlProgram *MOJOSHADER_sdlLinkProgram(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *vshader,
    MOJOSHADER_sdlShader *pshader
) {
    MOJOSHADER_sdlProgram *result;
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
        return NULL;

    result = (MOJOSHADER_sdlProgram*) ctx->malloc_fn(sizeof(MOJOSHADER_sdlProgram), ctx->malloc_data);

    if (result == NULL)
    {
        out_of_memory();
        return NULL;
    } // if

    MOJOSHADER_spirv_link_attributes(vshader->parseData, pshader->parseData);
    v_shader_source = vshader->parseData->output;
    p_shader_source = pshader->parseData->output;
    v_shader_len = vshader->parseData->output_len - sizeof(SpirvPatchTable);
    p_shader_len = pshader->parseData->output_len - sizeof(SpirvPatchTable);

    if (ctx->SHD_TranslateFromSPIRV == NULL)
    {
        // No translation needed, just reassign
        v_transpiled_source = v_shader_source;
        v_transpiled_len = v_shader_len;
    } // if
    else
    {
        v_transpiled_source = ctx->SHD_TranslateFromSPIRV(
            ctx->backend,
            v_shader_source,
            v_shader_len,
            &v_transpiled_len
        );
    } // else

    if (v_transpiled_source == NULL)
    {
        set_error(SDL_GetError()); // Technically should be SHD_GetError, but eh
        return NULL;
    } // if

    if (ctx->SHD_TranslateFromSPIRV == NULL)
    {
        // No translation needed, just reassign
        p_transpiled_source = p_shader_source;
        p_transpiled_len = p_shader_len;
    } // if
    else
    {
        p_transpiled_source = ctx->SHD_TranslateFromSPIRV(
            ctx->backend,
            p_shader_source,
            p_shader_len,
            &p_transpiled_len
        );
    } // else

    if (p_transpiled_source == NULL)
    {
        set_error(SDL_GetError()); // Technically should be SHD_GetError, but eh
        ctx->free_fn((char*) v_transpiled_source, ctx->malloc_data);
        return NULL;
    } // if

    createInfo.code     = (uint8_t*) v_transpiled_source;
    createInfo.codeSize = v_transpiled_len;
    createInfo.type     = SDL_GPU_SHADERTYPE_VERTEX;

    result->vertexModule = SDL_GpuCreateShaderModule(
        ctx->device,
        &createInfo
    );

    if (v_transpiled_source != v_shader_source)
        ctx->free_fn((char*) v_transpiled_source, ctx->malloc_data);

    if (result->vertexModule == NULL)
    {
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    } // if

    createInfo.code     = (uint8_t*) p_transpiled_source;
    createInfo.codeSize = p_transpiled_len;
    createInfo.type     = SDL_GPU_SHADERTYPE_FRAGMENT;

    result->pixelModule = SDL_GpuCreateShaderModule(
        ctx->device,
        &createInfo
    );

    if (p_transpiled_source != p_shader_source)
        ctx->free_fn((char*) p_transpiled_source, ctx->malloc_data);

    if (result->pixelModule == NULL)
    {
        SDL_GpuQueueDestroyShaderModule(ctx->device, result->vertexModule);
        ctx->free_fn(result, ctx->malloc_data);
        return NULL;
    } // if

    result->vertexShader = vshader;
    result->pixelShader = pshader;

    return result;
} // MOJOSHADER_sdlLinkProgram

void MOJOSHADER_sdlShaderAddRef(MOJOSHADER_sdlShader *shader)
{
    if (shader != NULL)
        shader->refcount++;
} // MOJOSHADER_sdlShaderAddRef

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
                    } // if
                } // while
            } // if

            MOJOSHADER_freeParseData(shader->parseData);
            ctx->free_fn(shader, ctx->malloc_data);
        } // else
    } // if
} // MOJOSHADER_sdlDeleteShader

const MOJOSHADER_parseData *MOJOSHADER_sdlGetShaderParseData(
    MOJOSHADER_sdlShader *shader
) {
    return (shader != NULL) ? shader->parseData : NULL;
} // MOJOSHADER_sdlGetShaderParseData

void MOJOSHADER_sdlDeleteProgram(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlProgram *p
) {
    if (p->vertexModule != NULL)
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->vertexModule);
    if (p->pixelModule != NULL)
        SDL_GpuQueueDestroyShaderModule(ctx->device, p->pixelModule);
    ctx->free_fn(p, ctx->malloc_data);
} // MOJOSHADER_sdlDeleteProgram

void MOJOSHADER_sdlBindProgram(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlProgram *p
) {
    ctx->bound_program = p;
} // MOJOSHADER_sdlBindProgram

void MOJOSHADER_sdlBindShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader *vshader,
    MOJOSHADER_sdlShader *pshader
) {
    if (ctx->linker_cache == NULL)
    {
        ctx->linker_cache = hash_create(NULL, hash_shaders, match_shaders,
                                        nuke_shaders, 0, ctx->malloc_fn,
                                        ctx->free_fn, ctx->malloc_data);

        if (ctx->linker_cache == NULL)
        {
            out_of_memory();
            return;
        } // if
    } // if

    MOJOSHADER_sdlProgram *program = NULL;
    BoundShaders shaders;
    shaders.vertex = vshader;
    shaders.fragment = pshader;

    ctx->bound_vshader = vshader;
    ctx->bound_pshader = pshader;

    const void *val = NULL;
    if (hash_find(ctx->linker_cache, &shaders, &val))
        program = (MOJOSHADER_sdlProgram *) val;
    else
    {
        program = MOJOSHADER_sdlLinkProgram(ctx, vshader, pshader);
        if (program == NULL)
            return;

        BoundShaders *item = (BoundShaders *) ctx->malloc_fn(sizeof (BoundShaders),
                                                             ctx->malloc_data);
        if (item == NULL)
        {
            MOJOSHADER_sdlDeleteProgram(ctx, program);
            return;
        } // if

        memcpy(item, &shaders, sizeof (BoundShaders));
        if (hash_insert(ctx->linker_cache, item, program) != 1)
        {
            ctx->free_fn(item, ctx->malloc_data);
            MOJOSHADER_sdlDeleteProgram(ctx, program);
            out_of_memory();
            return;
        } // if
    } // else

    SDL_assert(program != NULL);
    ctx->bound_program = program;
} // MOJOSHADER_sdlBindShaders

void MOJOSHADER_sdlGetBoundShaders(
    MOJOSHADER_sdlContext *ctx,
    MOJOSHADER_sdlShader **vshader,
    MOJOSHADER_sdlShader **pshader
) {
    if (vshader != NULL)
    {
        if (ctx->bound_program != NULL)
            *vshader = ctx->bound_program->vertexShader;
        else
            *vshader = ctx->bound_vshader; // In case a pshader isn't set yet
    } // if
    if (pshader != NULL)
    {
        if (ctx->bound_program != NULL)
            *pshader = ctx->bound_program->pixelShader;
        else
            *pshader = ctx->bound_pshader; // In case a vshader isn't set yet
    } // if
} // MOJOSHADER_sdlGetBoundShaders

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
} // MOJOSHADER_sdlMapUniformBufferMemory

void MOJOSHADER_sdlUnmapUniformBufferMemory(MOJOSHADER_sdlContext *ctx)
{
    /* no-op! real work done in sdlUpdateUniformBuffers */
} // MOJOSHADER_sdlUnmapUniformBufferMemory

int MOJOSHADER_sdlGetUniformBufferSize(MOJOSHADER_sdlShader *shader)
{
    int32_t i;
    int32_t buflen = 0;
    const int32_t uniformSize = 16; // Yes, even the bool registers
    for (i = 0; i < shader->parseData->uniform_count; i++)
    {
        const int32_t arrayCount = shader->parseData->uniforms[i].array_count;
        buflen += (arrayCount ? arrayCount : 1) * uniformSize;
    } // for

    return buflen;
} // MOJOSHADER_sdlGetUniformBufferSize

void MOJOSHADER_sdlUpdateUniformBuffers(MOJOSHADER_sdlContext *ctx,
                                        SDL_GpuCommandBuffer *commandBuffer)
{
    if (MOJOSHADER_sdlGetUniformBufferSize(ctx->bound_program->vertexShader) > 0)
        update_uniform_buffer(ctx, commandBuffer, ctx->bound_program->vertexShader);
    if (MOJOSHADER_sdlGetUniformBufferSize(ctx->bound_program->pixelShader) > 0)
        update_uniform_buffer(ctx, commandBuffer, ctx->bound_program->pixelShader);
} // MOJOSHADER_sdlUpdateUniformBuffers

int MOJOSHADER_sdlGetVertexAttribLocation(
    MOJOSHADER_sdlShader *vert,
    MOJOSHADER_usage usage, int index
) {
    int32_t i;
    if (vert == NULL)
        return -1;

    for (i = 0; i < vert->parseData->attribute_count; i++)
    {
        if (vert->parseData->attributes[i].usage == usage &&
            vert->parseData->attributes[i].index == index)
        {
            return i;
        } // if
    } // for

    // failure
    return -1;
} // MOJOSHADER_sdlGetVertexAttribLocation

void MOJOSHADER_sdlGetShaderModules(
    MOJOSHADER_sdlContext *ctx,
    SDL_GpuShaderModule **vmodule,
    SDL_GpuShaderModule **pmodule
) {
    assert(ctx->bound_program != NULL);
    if (vmodule != NULL)
        *vmodule = ctx->bound_program->vertexModule;
    if (pmodule != NULL)
        *pmodule = ctx->bound_program->pixelModule;
} // MOJOSHADER_sdlGetShaderModules
