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
	
	server_t *server = calloc(1, sizeof(server_t));
	server_flags_t flags = SERVER_NONE;
	flags |= FADE_OUT ? SERVER_FADE_OUT_ENABLED : 0;
	flags |= SERVE_HISTOGRAM ? SERVER_HISTOGRAM_ENABLED : 0;
	if (!server_start(server, PORT, PIXEL_WIDTH, PIXEL_HEIGHT, 4, CONNECTION_TIMEOUT, 2, flags, 4))
	{
		SDL_Quit();
		return 1;
	}
	
	char *text_additional = "echo \"PX <X> <Y> <RRGGBB>\\n\" > nc " str(HOST) " " str(PORT);
	if (SERVE_HISTOGRAM)
		text_additional = "http://" str(HOST) ":" str(PORT);
	int text_position[2] = { 32, PIXEL_HEIGHT - 64 };
	int text_size = 14;
	uint8_t text_color[3] = { 255, 255, 255 };
	uint8_t text_bgcolor[4] = { 32, 32, 32, 255 };

	while(42)
	{
		server_update(server);

		framebuffer_write_text_with_background(
			&server->framebuffer,
			text_position[0], text_position[1], text_additional, text_size,
			text_color[0], text_color[1], text_color[2],
			text_bgcolor[0], text_bgcolor[1], text_bgcolor[2], text_bgcolor[3]);
		
		char text[1024];
		sprintf(text, "connections: %4u; pixels: %10" PRId64 "; p/s: %8u",
			server->connection_count, server->total_pixels_received, server->pixels_received_per_second);
		framebuffer_write_text_with_background(
			&server->framebuffer,
			text_position[0], text_position[1] + text_size, text, text_size,
			text_color[0], text_color[1], text_color[2],
			text_bgcolor[0], text_bgcolor[1], text_bgcolor[2], text_bgcolor[3]);
		
		SDL_UpdateTexture(sdlTexture, NULL, server->framebuffer.pixels,
			server->framebuffer.width * server->framebuffer.bytesPerPixel);
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

	server_stop(server);
	free(server);

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
