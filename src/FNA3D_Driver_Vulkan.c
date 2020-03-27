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

#if FNA3D_DRIVER_VULKAN

#include "FNA3D_Driver.h"
#include "FNA3D_Driver_Vulkan.h"

#include <SDL.h>
#include <SDL_syswm.h>

/* Internal Structures */

typedef struct VulkanTexture
{
    uint32_t handle;
    uint8_t hasMipmaps;
    int32_t width;
    int32_t height;
    FNA3D_SurfaceFormat format;
    FNA3D_TextureAddressMode wrapS;
    FNA3D_TextureAddressMode wrapT;
    FNA3D_TextureAddressMode wrapR;
    FNA3D_TextureFilter filter;
    float anisotropy;
    int32_t maxMipmapLevel;
    float lodBias;
} VulkanTexture;

static VulkanTexture NullTexture =
{
    .handle = 0,
    .width = 0,
    .height = 0,
    .format = FNA3D_SURFACEFORMAT_SINGLE,
    .wrapS = FNA3D_TEXTUREADDRESSMODE_WRAP,
    .wrapT = FNA3D_TEXTUREADDRESSMODE_WRAP,
    .wrapR = FNA3D_TEXTUREADDRESSMODE_WRAP,
    .filter = FNA3D_TEXTUREFILTER_LINEAR,
    .anisotropy = 0,
    .maxMipmapLevel = 0,
    .lodBias = 0
};

#endif /* FNA_3D_DRIVER_VULKAN */
