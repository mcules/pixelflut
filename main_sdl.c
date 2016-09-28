#include "server.c"
#include <SDL.h>

int main()
{
	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(0);

	SDL_Window* window = SDL_CreateWindow(
		"pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	#if FULLSCREEN_START
		| SDL_WINDOW_FULLSCREEN
	#endif
	);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderClear(renderer);

	SDL_Texture* sdlTexture = SDL_CreateTexture(
		renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
		PIXEL_WIDTH, PIXEL_HEIGHT);
	if(!sdlTexture)
	{
		printf("could not create texture");
		SDL_Quit();
		return 1;
	}
	
	server_t server = {0};
	server_flags_t flags = SERVER_NONE;
	flags |= FADE_OUT ? SERVER_FADE_OUT_ENABLED : 0;
	flags |= SERVE_HISTOGRAM ? SERVER_HISTOGRAM_ENABLED : 0;
	if (!server_start(&server, PIXEL_WIDTH, PIXEL_HEIGHT, 4, CONNECTION_TIMEOUT, flags))
	{
		SDL_Quit();
		return 1;
	}

	while(42)
	{
		server_update(&server);
		SDL_UpdateTexture(sdlTexture, NULL, server.framebuffer.pixels,
			server.framebuffer.width * server.framebuffer.bytesPerPixel);
		SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
		SDL_RenderPresent(renderer);
		SDL_Event event;
		if(SDL_PollEvent(&event))
		{
			if(event.type == SDL_QUIT)
			{
				break;
			}
			if(event.type == SDL_KEYDOWN)
			{
				if(event.key.keysym.sym == SDLK_q)
				{
					break;
				}
				if(event.key.keysym.sym == SDLK_f)
				{
					uint32_t flags = SDL_GetWindowFlags(window);
					SDL_SetWindowFullscreen(window, 
						(flags & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
					printf("Toggled Fullscreen\n");
				}
			}
		}
	}

	server_stop(&server);

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
