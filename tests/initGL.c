#include "FNA3D.h"
#include <SDL.h>
#include <stdio.h>

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
		640,
		480,
		(SDL_WindowFlags) flags
	);
	if (window == NULL)
	{
		printf(
			"ERROR: Could not create window! %s\n",
			SDL_GetError()
		);
		exit(1);
	}

	/* Create the device */
	pp.deviceWindowHandle = window;
	device = FNA3D_CreateDevice(&pp);
	if (device == NULL)
	{
		printf("ERROR: Could not create device!\n");
		exit(1);
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
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}