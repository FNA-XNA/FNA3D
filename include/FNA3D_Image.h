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

#ifndef FNA3D_IMAGE_H
#define FNA3D_IMAGE_H

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

/* Image Read API */

typedef int32_t (FNA3DCALL * FNA3D_Image_ReadFunc)(
	void* context,
	char *data,
	int32_t size
);
typedef void (FNA3DCALL * FNA3D_Image_SkipFunc)(
	void* context,
	int32_t n
);
typedef int32_t (FNA3DCALL * FNA3D_Image_EOFFunc)(void* context);

FNA3DAPI uint8_t* FNA3D_Image_Load(
	FNA3D_Image_ReadFunc readFunc,
	FNA3D_Image_SkipFunc skipFunc,
	FNA3D_Image_EOFFunc eofFunc,
	void* context,
	int32_t *w,
	int32_t *h,
	int32_t *len,
	int32_t forceW,
	int32_t forceH,
	uint8_t zoom
);

FNA3DAPI void FNA3D_Image_Free(uint8_t *mem);

/* Image Write API */

typedef void (FNA3DCALL * FNA3D_Image_WriteFunc)(
	void* context,
	void* data,
	int32_t size
);

FNA3DAPI void FNA3D_Image_SavePNG(
	FNA3D_Image_WriteFunc writeFunc,
	void* context,
	int32_t srcW,
	int32_t srcH,
	int32_t dstW,
	int32_t dstH,
	uint8_t *data
);

FNA3DAPI void FNA3D_Image_SaveJPG(
	FNA3D_Image_WriteFunc writeFunc,
	void* context,
	int32_t srcW,
	int32_t srcH,
	int32_t dstW,
	int32_t dstH,
	uint8_t *data,
	int32_t quality
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FNA3D_IMAGE_H */

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
