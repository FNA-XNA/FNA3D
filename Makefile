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

# Includes/Libraries
INCLUDES = -Iinclude `sdl2-config --cflags`
DEPENDENCIES = `sdl2-config --libs`

# Source
FNA3DSRC = \
	src/FNA3D.c \
	src/FNA3D_OpenGL.c

# Objects
FNA3DOBJ = $(FNA3DSRC:%.c=%.o)

# Targets
all: $(FNA3DOBJ)
	$(CC) $(CFLAGS) -shared -o $(PREFIX)FNA3D.$(SUFFIX) $(FNA3DOBJ) $(DEPENDENCIES) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(INCLUDES) $(DEFINES)

clean:
	rm -f $(FNA3DOBJ) $(PREFIX)FNA3D.$(SUFFIX)
