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

GL_PROC(GLubyte*, glGetString, (GLenum a))
GL_PROC(void, glGetIntegerv, (GLenum a, GLint *b))
GL_PROC(void, glEnable, (GLenum a))
GL_PROC(void, glDisable, (GLenum a))
GL_PROC(void, glViewport, (GLint a, GLint b, GLsizei c, GLsizei d))
GL_PROC(void, glDepthRange, (GLdouble a, GLdouble b))
GL_PROC(void, glScissor, (GLint a, GLint b, GLint c, GLint d))
GL_PROC(void, glBlendColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d))
GL_PROC(void, glBlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d))
GL_PROC(void, glBlendEquationSeparate, (GLenum a, GLenum b))
GL_PROC(void, glColorMask, (GLboolean a, GLboolean b, GLboolean c, GLboolean d))
GL_PROC(void, glColorMaski, (GLuint a, GLboolean b, GLboolean c, GLboolean d, GLboolean e))
GL_PROC(void, glSampleMaski, (GLuint a, GLuint b))
GL_PROC(void, glDepthMask, (GLboolean a))
GL_PROC(void, glDepthFunc, (GLenum a))
GL_PROC(void, glStencilMask, (GLint a))
GL_PROC(void, glStencilFuncSeparate, (GLenum a, GLenum b, GLint c, GLint d))
GL_PROC(void, glStencilOpSeparate, (GLenum a, GLenum b, GLenum c, GLenum d))
GL_PROC(void, glStencilFunc, (GLenum a, GLint b, GLint c))
GL_PROC(void, glStencilOp, (GLenum a, GLenum b, GLenum c))
GL_PROC(void, glFrontFace, (GLenum a))
GL_PROC(void, glPolygonMode, (GLenum a, GLenum b))
GL_PROC(void, glPolygonOffset, (GLfloat a, GLfloat b))
GL_PROC(void, glCreateTextures, (GLenum a, GLint b, GLuint *c))
GL_PROC(void, glDeleteTextures, (GLint a, const GLuint *b))
GL_PROC(void, glBindTextureUnit, (GLint a, GLuint b))
GL_PROC(void, glTextureStorage2D, (GLuint a, GLint b, GLenum c, GLint d, GLint e))
GL_PROC(void, glTextureSubImage2D, (GLuint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLenum g, GLenum h, const void* i))
GL_PROC(void, glCompressedTextureSubImage2D, (GLuint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLenum g, GLint h, const void* i))
GL_PROC(void, glTextureStorage3D, (GLuint a, GLint b, GLenum c, GLint d, GLint e, GLint f))
GL_PROC(void, glTextureSubImage3D, (GLuint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLenum i, GLenum j, const void* k))
GL_PROC(void, glCompressedTextureSubImage3D, (GLuint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLenum i, GLint j, const void* k))
GL_PROC(void, glGetTextureSubImage, (GLuint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLenum i, GLenum j, GLint k, const void* l))
GL_PROC(void, glTextureParameteri, (GLuint a, GLenum b, GLenum c))
GL_PROC(void, glCreateSamplers, (GLint a, GLuint *b))
GL_PROC(void, glDeleteSamplers, (GLint a, const GLuint *b))
GL_PROC(void, glBindSampler, (GLint a, GLuint b))
GL_PROC(void, glSamplerParameteri, (GLuint a, GLenum b, GLint c))
GL_PROC(void, glSamplerParameterf, (GLuint a, GLenum b, GLfloat c))
GL_PROC(void, glPixelStorei, (GLenum a, GLint b))
GL_PROC(void, glTexEnvi, (GLenum a, GLenum b, GLint c))
GL_PROC(void, glCreateBuffers, (GLint a, GLuint *b))
GL_PROC(void, glDeleteBuffers, (GLint a, const GLuint *b))
GL_PROC(void, glBindBuffer, (GLenum a, GLuint b))
GL_PROC(void, glNamedBufferData, (GLuint a, GLsizeiptr b, const void* c, GLenum d))
GL_PROC(void, glNamedBufferSubData, (GLuint a, GLintptr b, GLsizeiptr c, const void* d))
GL_PROC(void, glGetNamedBufferSubData, (GLuint a, GLintptr b, GLsizeiptr c, const void* d))
GL_PROC(void, glNamedBufferStorage, (GLuint a, GLsizeiptr b, const void* c, GLbitfield d))
GL_PROC(void*, glMapNamedBufferRange, (GLuint a, GLintptr b, GLsizeiptr c, GLenum d))
GL_PROC(void, glUnmapNamedBuffer, (GLuint a))
GL_PROC(void, glInvalidateBufferData, (GLuint a))
GL_PROC(void, glClearColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d))
GL_PROC(void, glClearDepth, (GLdouble a))
GL_PROC(void, glClearStencil, (GLint a))
GL_PROC(void, glClear, (GLenum a))
GL_PROC(void, glReadPixels, (GLint a, GLint b, GLint c, GLint d, GLenum e, GLenum f, void* g))
GL_PROC(void, glGenerateTextureMipmap, (GLuint a))
GL_PROC(void, glCreateFramebuffers, (GLint a, GLuint *b))
GL_PROC(void, glDeleteFramebuffers, (GLint a, const GLuint *b))
GL_PROC(void, glBindFramebuffer, (GLenum a, GLuint b))
GL_PROC(void, glNamedFramebufferTexture, (GLuint a, GLenum b, GLuint c, GLint d))
GL_PROC(void, glNamedFramebufferTextureLayer, (GLuint a, GLenum b, GLuint c, GLint d, GLint e))
GL_PROC(void, glNamedFramebufferRenderbuffer, (GLuint a, GLenum b, GLenum c, GLuint d))
GL_PROC(void, glNamedFramebufferDrawBuffers, (GLuint a, GLint b, const GLenum *c))
GL_PROC(void, glBlitNamedFramebuffer, (GLuint a, GLuint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLint i, GLint j, GLenum k, GLenum l))
GL_PROC(void, glInvalidateNamedFramebufferData, (GLuint a, GLint b, const GLenum *c))
GL_PROC(void, glCreateRenderbuffers, (GLint a, GLuint *b))
GL_PROC(void, glDeleteRenderbuffers, (GLint a, const GLuint *b))
GL_PROC(void, glNamedRenderbufferStorage, (GLuint a, GLenum b, GLint c, GLint d))
GL_PROC(void, glNamedRenderbufferStorageMultisample, (GLuint a, GLint b, GLenum c, GLint d, GLint e))
GL_PROC(void, glVertexAttribPointer, (GLint a, GLint b, GLenum c, GLboolean d, GLint e, const void* f))
GL_PROC(void, glVertexAttribDivisor, (GLint a, GLint b))
GL_PROC(void, glEnableVertexAttribArray, (GLint a))
GL_PROC(void, glDisableVertexAttribArray, (GLint a))
GL_PROC(void, glDrawRangeElements, (GLenum a, GLint b, GLint c, GLint d, GLenum e, const void* f))
GL_PROC(void, glDrawElementsInstancedBaseVertex, (GLenum a, GLint b, GLenum c, const void* d, GLint e, GLint f))
GL_PROC(void, glDrawRangeElementsBaseVertex, (GLenum a, GLint b, GLint c, GLint d, GLenum e, const void* f, GLint g))
GL_PROC(void, glDrawArrays, (GLenum a, GLint b, GLint c))
GL_PROC(void, glGenQueries, (GLint a, GLuint *b))
GL_PROC(void, glDeleteQueries, (GLint a, const GLuint *b))
GL_PROC(void, glBeginQuery, (GLenum a, GLuint b))
GL_PROC(void, glEndQuery, (GLenum a))
GL_PROC(void, glGetQueryObjectuiv, (GLuint a, GLenum b, GLuint *c))
GL_PROC(GLubyte*, glGetStringi, (GLenum a, GLuint b))
GL_PROC(void, glGenVertexArrays, (GLint a, GLuint *b))
GL_PROC(void, glDeleteVertexArrays, (GLint a, const GLuint *b))
GL_PROC(void, glBindVertexArray, (GLuint a))

/* "NOTE: when implemented in an OpenGL ES context, all entry points defined
 * by this extension must have a "KHR" suffix. When implemented in an
 * OpenGL context, all entry points must have NO suffix, as shown below."
 * https://www.khronos.org/registry/OpenGL/extensions/KHR/KHR_debug.txt
 */
GL_PROC_EXT(KHR_debug, KHR, void, glDebugMessageCallback, (DEBUGPROC a, const GLvoid *b))
GL_PROC_EXT(KHR_debug, KHR, void, glDebugMessageControl, (GLenum a, GLenum b, GLenum c, GLsizei d, const GLuint *e, GLboolean f))

/* Nice feature for apitrace */
GL_PROC_EXT(GREMEDY_string_marker, , void, glStringMarkerGREMEDY, (GLsizei a, const GLchar *b))

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
