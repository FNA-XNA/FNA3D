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

#ifndef FNA3D_DRIVER_OPENGL_H
#define FNA3D_DRIVER_OPENGL_H

/* Types */
typedef unsigned int	GLenum;
typedef unsigned int	GLbitfield;
typedef void		GLvoid;
typedef unsigned int	GLuint;
typedef int		GLint;
typedef unsigned char	GLubyte;
typedef int		GLsizei;
typedef float		GLfloat;
typedef float		GLclampf;
typedef double		GLdouble;
typedef char		GLchar;
typedef uintptr_t	GLsizeiptr;
typedef intptr_t	GLintptr;
typedef unsigned char	GLboolean;

/* Hint */
#define GL_DONT_CARE					0x1100

/* One */
#define GL_ZERO 					0x0000
#define GL_ONE  					0x0001

/* True/False */
#define GL_FALSE					0x0000
#define GL_TRUE 					0x0001

/* Strings */
#define GL_VENDOR					0x1F00
#define GL_RENDERER					0x1F01
#define GL_VERSION					0x1F02
#define GL_EXTENSIONS					0x1F03

/* Types */
#define GL_UNSIGNED_BYTE				0x1401
#define GL_UNSIGNED_INT 				0x1405
#define GL_FLOAT					0x1406

/* Call Lists */
#define GL_TRIANGLES					0x0004
#define GL_TRIANGLE_STRIP				0x0005

/* Textures */
#define GL_UNPACK_ALIGNMENT				0x0CF5
#define GL_TEXTURE_2D					0x0DE1
#define GL_TEXTURE_WRAP_S				0x2802
#define GL_TEXTURE_WRAP_T				0x2803
#define GL_TEXTURE_MAG_FILTER				0x2800
#define GL_TEXTURE_MIN_FILTER				0x2801
#define GL_TEXTURE_BASE_LEVEL				0x813C
#define GL_TEXTURE_MAX_LEVEL				0x813D
#define GL_NEAREST					0x2600
#define GL_LINEAR					0x2601
#define GL_CLAMP					0x2900
#define GL_REPEAT					0x2901
#define GL_RGBA 					0x1908
#define GL_CLAMP_TO_EDGE				0x812F
#define GL_TEXTURE0					0x84C0

/* Clearing */
#define GL_COLOR_BUFFER_BIT				0x4000

/* Blending */
#define GL_BLEND					0x0BE2
#define GL_SRC_COLOR					0x0300
#define GL_SRC_ALPHA					0x0302
#define GL_ONE_MINUS_SRC_ALPHA  			0x0303

/* Framebuffers */
#define GL_FRAMEBUFFER  				0x8D40
#define GL_RENDERBUFFER 				0x8D41
#define GL_DEPTH_COMPONENT				0x1902
#define GL_DEPTH_ATTACHMENT				0x8D00
#define GL_COLOR_ATTACHMENT0				0x8CE0

/* Debug */
#define GL_DEBUG_TYPE_ERROR				0x824C
#define GL_DEBUG_TYPE_OTHER				0x8251

/* Severity */
#define GL_DEBUG_SEVERITY_HIGH  			0x9146
#define GL_DEBUG_SEVERITY_MEDIUM			0x9147
#define GL_DEBUG_SEVERITY_LOW				0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION  		0x826B

/* In case this needs to be exported in a certain way... */
#ifdef _WIN32 /* Windows OpenGL uses stdcall */
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

/* Debug callback typedef */
typedef void (GLAPIENTRY *DEBUGPROC)(
	GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar *message,
	const void *userParam
);

/* Function typedefs */
#define GL_PROC(ext, ret, func, parms) \
	typedef ret (GLAPIENTRY *glfntype_##func) parms;
#include "glfuncs.h"
#undef GL_PROC

/* glGetString is a bit different since we load it early */
typedef const GLubyte* (GLAPIENTRY *glfntype_glGetString)(GLenum a);

#endif
