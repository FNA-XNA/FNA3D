# CMake Project for FNA3D
# Written by @NeroBurner
cmake_minimum_required(VERSION 2.8.12)
project(FNA3D)

# Options
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Version
SET(LIB_MAJOR_VERSION "0")
SET(LIB_MINOR_VERSION "20")
SET(LIB_REVISION "04")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

# Build Type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	# By default, we use Release
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE "Release" CACHE
		STRING "Choose the type of build." FORCE
	)

	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
		STRINGS "Debug" "Release" "RelWithDebInfo"
	)
endif()

# Platform Flags
if(APPLE)
	set(CMAKE_MACOSX_RPATH ON)
	set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
	set(LOBJC "objc -framework Carbon")
elseif(WIN32)
	# "FNA3D.dll", not "libFNA3D.dll"
	set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()

# Defines
add_definitions(
	-DFNA3D_DRIVER_OPENGL
)
add_definitions(
	-DMOJOSHADER_NO_VERSION_INCLUDE
	-DMOJOSHADER_USE_SDL_STDLIB
	-DMOJOSHADER_EFFECT_SUPPORT
	-DMOJOSHADER_DEPTH_CLIPPING
	-DMOJOSHADER_FLIP_RENDERTARGET
	-DMOJOSHADER_XNA4_VERTEX_TEXTURES
	-DSUPPORT_PROFILE_ARB1=0
	-DSUPPORT_PROFILE_ARB1_NV=0
	-DSUPPORT_PROFILE_BYTECODE=0
	-DSUPPORT_PROFILE_D3D=0
)
if(APPLE)
	add_definitions(-DSUPPORT_PROFILE_METAL=1)
else()
	add_definitions(-DSUPPORT_PROFILE_METAL=0)
endif()

# Source lists
add_library(FNA3D
	# Public Headers
	include/FNA3D.h
	include/FNA3D_Image.h
	# Internal Headers
	src/FNA3D_CommandStream.h
	src/FNA3D_Driver.h
	src/FNA3D_Driver_OpenGL.h
	src/FNA3D_Driver_OpenGL_glfuncs.h
	# Source Files
	src/FNA3D.c
	src/FNA3D_CommandStream.c
	src/FNA3D_Driver_OpenGL.c
	src/FNA3D_Image.c
)
add_library(mojoshader STATIC
	MojoShader/mojoshader.c
	MojoShader/mojoshader_effects.c
	MojoShader/mojoshader_common.c
	MojoShader/mojoshader_opengl.c
	MojoShader/mojoshader_metal.c
	MojoShader/profiles/mojoshader_profile_common.c
	MojoShader/profiles/mojoshader_profile_glsl.c
	MojoShader/profiles/mojoshader_profile_metal.c
	MojoShader/profiles/mojoshader_profile_spirv.c
)

# Build flags
if(NOT MSVC)
	set_property(TARGET FNA3D PROPERTY COMPILE_FLAGS "-std=c99 -Wall -pedantic")
endif()
if(BUILD_SHARED_LIBS)
	set_property(TARGET mojoshader PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# FNA3D folders as includes, for other targets to consume
target_include_directories(FNA3D PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(mojoshader PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/MojoShader>
)

# MinGW builds should statically link libgcc
if(MINGW)
	target_link_libraries(FNA3D PRIVATE -static-libgcc)
endif()

# Soname
set_target_properties(FNA3D PROPERTIES OUTPUT_NAME "FNA3D"
	VERSION ${LIB_VERSION}
	SOVERSION ${LIB_MAJOR_VERSION}
)

# Internal Dependencies
target_link_libraries(FNA3D PRIVATE mojoshader ${LOBJC})

# SDL2 Dependency
if (DEFINED SDL2_INCLUDE_DIRS AND DEFINED SDL2_LIBRARIES)
	message(STATUS "using pre-defined SDL2 variables SDL2_INCLUDE_DIRS and SDL2_LIBRARIES")
	target_include_directories(FNA3D PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
	target_include_directories(mojoshader PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
	target_link_libraries(FNA3D PUBLIC ${SDL2_LIBRARIES})
else()
	# Only try to autodetect if both SDL2 variables aren't explicitly set
	find_package(SDL2 CONFIG)
	if (TARGET SDL2::SDL2)
		message(STATUS "using TARGET SDL2::SDL2")
		target_link_libraries(FNA3D PUBLIC SDL2::SDL2)
		target_link_libraries(mojoshader PUBLIC SDL2::SDL2)
	elseif (TARGET SDL2)
		message(STATUS "using TARGET SDL2")
		target_link_libraries(FNA3D PUBLIC SDL2)
		target_link_libraries(mojoshader PUBLIC SDL2)
	else()
		message(STATUS "no TARGET SDL2::SDL2, or SDL2, using variables")
		target_include_directories(FNA3D PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
		target_include_directories(mojoshader PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
		target_link_libraries(FNA3D PUBLIC ${SDL2_LIBRARIES})
	endif()
endif()
