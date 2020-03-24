# Makefile for FNA3D
# Written by Ethan "flibitijibibo" Lee

# System information
UNAME = $(shell uname)
ARCH = $(shell uname -m)

# Detect Windows target
WINDOWS_TARGET=0
ifeq ($(OS), Windows_NT) # cygwin/msys2
	WINDOWS_TARGET=1
endif
ifneq (,$(findstring w64-mingw32,$(CC))) # mingw-w64 on Linux
	WINDOWS_TARGET=1
endif

# Compiler
ifeq ($(WINDOWS_TARGET),1)
	SUFFIX = dll
	LDFLAGS += -static-libgcc
else ifeq ($(UNAME), Darwin)
	CC += -mmacosx-version-min=10.9
	PREFIX = lib
	SUFFIX = dylib
	CFLAGS += -fpic -fPIC
else
	PREFIX = lib
	SUFFIX = so
	CFLAGS += -fpic -fPIC
endif

CFLAGS += -g -O3

# Drivers
DEFINES += -DFNA3D_DRIVER_OPENGL

# Includes/Libraries
INCLUDES = -Iinclude -IMojoShader `sdl2-config --cflags`
DEPENDENCIES = `sdl2-config --libs`

# MojoShader Configuration
DEFINES += \
	-DMOJOSHADER_NO_VERSION_INCLUDE \
	-DMOJOSHADER_EFFECT_SUPPORT \
	-DMOJOSHADER_DEPTH_CLIPPING \
	-DMOJOSHADER_FLIP_RENDERTARGET \
	-DMOJOSHADER_XNA4_VERTEX_TEXTURES \
	-DSUPPORT_PROFILE_ARB1=0 \
	-DSUPPORT_PROFILE_ARB1_NV=0 \
	-DSUPPORT_PROFILE_BYTECODE=0 \
	-DSUPPORT_PROFILE_D3D=0
ifeq ($(UNAME), Darwin)
	DEFINES += -DSUPPORT_PROFILE_METAL=1
	DEPENDENCIES += -lobjc
else
	DEFINES += -DSUPPORT_PROFILE_METAL=0
endif

# Source
FNA3DSRC = \
	src/FNA3D.c \
	src/FNA3D_Driver_OpenGL.c
MOJOSHADERSRC = \
	MojoShader/mojoshader.c \
	MojoShader/mojoshader_effects.c \
	MojoShader/mojoshader_common.c \
	MojoShader/mojoshader_opengl.c \
	MojoShader/mojoshader_metal.c \
	MojoShader/profiles/mojoshader_profile_common.c \
	MojoShader/profiles/mojoshader_profile_glsl.c \
	MojoShader/profiles/mojoshader_profile_metal.c \
	MojoShader/profiles/mojoshader_profile_spirv.c

# Objects
FNA3DOBJ = $(FNA3DSRC:%.c=%.o)
MOJOSHADEROBJ = $(MOJOSHADERSRC:%.c=%.o)

# Targets
all: $(FNA3DOBJ) $(MOJOSHADEROBJ)
	$(CC) $(CFLAGS) -shared -o $(PREFIX)FNA3D.$(SUFFIX) $(FNA3DOBJ) $(MOJOSHADEROBJ) $(DEPENDENCIES) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDES) $(DEFINES)

clean:
	rm -f $(FNA3DOBJ) $(MOJOSHADEROBJ) $(PREFIX)FNA3D.$(SUFFIX)
