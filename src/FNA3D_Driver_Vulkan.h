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

#ifndef FNA3D_DRIVER_VULKAN_H
#define FNA3D_DRIVER_VULKAN_H

#define VK_NO_PROTOTYPES
#include "vulkan.h"

/* In case this needs to be exported in a certain way... */
#ifdef _WIN32 /* Windows OpenGL uses stdcall */
#define VKAPIENTRY __stdcall
#else
#define VKAPIENTRY
#endif

#define MAX_MULTISAMPLE_MASK_SIZE 2

/* Instance Function typedefs */
#define VULKAN_INSTANCE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPIENTRY *vkfntype_##func) params;
#include "FNA3D_Driver_Vulkan_instance_funcs.h"
#undef VULKAN_INSTANCE_FUNCTION

#define VULKAN_DEVICE_FUNCTION(ext, ret, func, params) \
	typedef ret (VKAPIENTRY *vkfntype_##func) params;
#include "FNA3D_Driver_Vulkan_device_funcs.h"
#undef VULKAN_DEVICE_FUNCTION

#endif FNA3D_DRIVER_VULKAN_H
