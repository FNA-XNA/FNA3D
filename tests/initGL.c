#include "FNA3D.h"
#include <SDL.h>

#define WIDTH 640
#define HEIGHT 480

int main(int argc, char **argv)
{
	int flags;
	int quit;
	SDL_Event e;
	FNA3D_PresentationParameters pp;
	SDL_Window *window;
	FNA3D_Device *device;

	/* Set up SDL window */
	SDL_Init(SDL_INIT_VIDEO);
	flags = FNA3D_PrepareWindowAttributes(1);
	window = SDL_CreateWindow(
		"FNA3D Init GL",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		WIDTH,
		HEIGHT,
		(SDL_WindowFlags) flags
	);
	if (window == NULL)
	{
		SDL_LogError(
			SDL_LOG_CATEGORY_APPLICATION,
			SDL_GetError()
		);
		SDL_assert(0 && "Could not create window!");
	}

	/* Set up the presentation parameters */
	pp.backBufferWidth = WIDTH;
	pp.backBufferHeight = HEIGHT;
	pp.backBufferFormat = FNA3D_SURFACEFORMAT_COLOR;
	pp.multiSampleCount = 0;
	pp.deviceWindowHandle = window;
	pp.isFullScreen = 0;
	pp.depthStencilFormat = FNA3D_DEPTHFORMAT_D24S8;
	pp.presentationInterval = FNA3D_PRESENTINTERVAL_DEFAULT;
	pp.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	pp.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	/* Create the device */
	device = FNA3D_CreateDevice(&pp);
	if (device == NULL)
	{
		SDL_assert(0 && "Device could not be created!");
	}

	/* Main loop */
	quit = 0;
	while (!quit)
	{
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
			{
				quit = 1;
				break;
			}
		}
		SDL_Delay(16);
	}

	/* Cleanup */
	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

/* vim: set noexpandtab shiftwidth=8 tabstop=8: */
