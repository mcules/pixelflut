#include "server.c"
#include <SDL.h>

#define BEGINS_WITH(str, test) (!strncmp(str, test, sizeof(test)))
#define OPTION(opt, desc) printf("\t" opt "\t" desc "\n")

int main(int argc, char **argv)
{
	int width = 0;
	int height = 0;
	uint16_t port = 1234;
	int connection_timeout = 5;
	int connections_max = 16;
	int threads = 3;
	int serve_histogram = 1;
	int start_fullscreen = 0;
	int fade_out = 0;
	int fade_interval = 4;
	int show_text = 1;
	
	for (int i = 1; i < argc; i++)
	{
		if (BEGINS_WITH(argv[i], "--help"))
		{
			printf("usage: %s [OPTION]...\n", argv[0]);
			printf("options:\n");
			OPTION("--width <pixels>", "\tFramebuffer width. Default: Screen width.");
			OPTION("--height <pixels>", "\tFramebuffer width. Default: Screen height.");
			OPTION("--port <port>", "\t\tTCP port. Default: 1234.");
			OPTION("--connection_timeout <seconds>", "Connection timeout on idle. Default: 5s.");
			OPTION("--connections_max <n>", "\tMaximum number of open connections. Default: 16.");
			OPTION("--threads <n>", "\t\tNumber of connection handler threads. Default: 3.");
			OPTION("--no-histogram", "\t\tDisable calculating and serving the histogram over HTTP.");
			OPTION("--fullscreen", "\t\tStart in fullscreen mode.");
			OPTION("--fade_out", "\t\tEnable fading out the framebuffer contents.");
			OPTION("--fade_interval <frames>", "Interval for fading out the framebuffer as number of displayed frames. Default: 4.");
			OPTION("--hide_text", "\t\tHide the overlay text.");
			return 0;
		}
		else if (BEGINS_WITH(argv[i], "--width") && i + 1 < argc)
			width = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--height") && i + 1 < argc)
			height = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--port") && i + 1 < argc)
			port = (uint16_t)strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--connection_timeout") && i + 1 < argc)
			connection_timeout = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--connections_max") && i + 1 < argc)
			connections_max = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--threads") && i + 1 < argc)
			threads = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--no-histogram"))
			serve_histogram = 0;
		else if (BEGINS_WITH(argv[i], "--fullscreen"))
			start_fullscreen = 1;
		else if (BEGINS_WITH(argv[i], "--fade_out"))
			fade_out = 1;
		else if (BEGINS_WITH(argv[i], "--fade_interval") && i + 1 < argc)
			fade_interval = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--hide_text"))
			show_text = 0;
		else
		{
			printf("unknown option \"%s\"\n", argv[i]);
			return 1;
		}
	}

	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(0);

	if (width == 0 || height == 0)
	{
		SDL_DisplayMode mode;
		if (SDL_GetCurrentDisplayMode(0, &mode))
		{
			printf("could not retrieve display mode\n");
			SDL_Quit();
			return 1;
		}

		width = start_fullscreen ? mode.w : mode.w - 160;
		height = start_fullscreen ? mode.h : mode.h - 160;
	}

	if (width < 1 || height < 1)
	{
		width = 640;
		height = 480;
	}

	if (connection_timeout < 1)
		connection_timeout = 1;

	if (connections_max < 1)
		connections_max = 1;

	if (threads < 1)
		threads = 1;

	if (fade_interval < 1)
		fade_interval = 1;

	uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
	window_flags |= start_fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
	SDL_Window* window = SDL_CreateWindow("pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, window_flags);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderClear(renderer);

	SDL_Texture* sdlTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	const int bytes_per_pixel = 4;
	if (!sdlTexture)
	{
		printf("could not create texture\n");
		SDL_Quit();
		return 1;
	}
	
	server_t *server = calloc(1, sizeof(server_t));
	server_flags_t flags = SERVER_NONE;
	flags |= fade_out ? SERVER_FADE_OUT_ENABLED : 0;
	flags |= serve_histogram ? SERVER_HISTOGRAM_ENABLED : 0;
	if (!server_start(
		server, port, connections_max, connection_timeout, threads,
		width, height, bytes_per_pixel, fade_interval, flags))
	{
		SDL_Quit();
		return 1;
	}
	
	char hostname[256] = "127.0.0.1";
	char text_additional[256];
	int text_position[2] = { 32, height - 64 };
	int text_size = 20;
	uint8_t text_color[3] = { 255, 255, 255 };
	uint8_t text_bgcolor[4] = { 32, 32, 32, 255 };

	if (show_text)
	{
		gethostname(hostname, sizeof(hostname));
		if (serve_histogram)
			snprintf(text_additional, sizeof(text_additional), "http://%s:%d", hostname, port);
		else
			snprintf(text_additional, sizeof(text_additional), "echo \"PX <X> <Y> <RRGGBB>\\n\" > nc %s %d", hostname, port);
	}

	while("the cat is sleeping on the keyboard")
	{
		server_update(server);

		if (show_text)
		{
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
		}
		
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
					SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
					printf("Toggled Fullscreen\n");
				}
			}
		}
	}

	exit(0); // TODO: fix hang on shutdown that happens otherwise

	server_stop(server);
	free(server);

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
