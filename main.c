#include "server.c"
#include <SDL.h>
#include<netdb.h>
#include<ifaddrs.h>

#define BEGINS_WITH(str, test) (!strncmp(str, test, sizeof(test)))
#define OPTION(opt, desc) printf("\t" opt "\t" desc "\n")

int get_ip_of_default_gateway_interface (char* ip);

int main(int argc, char **argv)
{
	int width = 0;
	int height = 0;
	uint16_t port = 1234;
	int connection_timeout = 5;
	int connections_max = 1000;
	int threads = 4;
	int serve_histogram = 1;
	int start_fullscreen = 1;
	int fade_out = 0;
	int fade_interval = 4;
	int show_text = 1;
	int show_ip_instead_of_hostname = 0;
	char custom_ip_adress[256] = "";


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
			OPTION("--connections_max <n>", "\tMaximum number of open connections. Default: 1000.");
			OPTION("--threads <n>", "\t\tNumber of connection handler threads. Default: 4.");
			OPTION("--no-histogram", "\t\tDisable calculating and serving the histogram over HTTP.");
			OPTION("--window", "\t\tStart in window mode.");
			OPTION("--fade_out", "\t\tEnable fading out the framebuffer contents.");
			OPTION("--fade_interval <frames>", "Interval for fading out the framebuffer as number of displayed frames. Default: 4.");
			OPTION("--hide_text", "\t\tHide the overlay text.");
			OPTION("--show_ip_instead_of_hostname", "Show IPv4 of interface with default-gateway on overlay.");
			OPTION("--show_custom_ip <IP>", "\tShow specific IP instead of hostname.");
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
		else if (BEGINS_WITH(argv[i], "--window"))
			start_fullscreen = 0;
		else if (BEGINS_WITH(argv[i], "--fade_out"))
			fade_out = 1;
		else if (BEGINS_WITH(argv[i], "--fade_interval") && i + 1 < argc)
			fade_interval = strtol(argv[++i], 0, 10);
		else if (BEGINS_WITH(argv[i], "--hide_text"))
			show_text = 0;
		else if (BEGINS_WITH(argv[i], "--show_ip_instead_of_hostname"))
			show_ip_instead_of_hostname = 1;
		else if (BEGINS_WITH(argv[i], "--show_custom_ip"))
			strcpy(custom_ip_adress,argv[++i]);
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
			printf("could not retrieve display mode (hint 1: run as root; hint 2: specify the correct xServer: 'DISPLAY=:0.0 ./pixelflut')\n");
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

	char hostname_or_ip[256] = "";
	char text_additional[256];
	int text_position[2] = { 32, height - 64 };
	int text_size = 20;
	uint8_t text_color[3] = { 255, 255, 255 };
	uint8_t text_bgcolor[4] = { 32, 32, 32, 255 };

	if (show_text)
	{
		if (show_ip_instead_of_hostname) {
			get_ip_of_default_gateway_interface(hostname_or_ip);
		} else if (strncmp(custom_ip_adress, "", 256) == 0) {  // if custom ip is set
			gethostname(hostname_or_ip, sizeof(hostname_or_ip));
		} else {  // show hostname (default)
			strcpy(hostname_or_ip, custom_ip_adress);
		}

		if (serve_histogram)
			snprintf(text_additional, sizeof(text_additional), "http://%s:%d", hostname_or_ip, port);
		else
			snprintf(text_additional, sizeof(text_additional), "echo \"PX <X> <Y> <RRGGBB>\\n\" > nc %s %d", hostname_or_ip, port);
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
			sprintf(text, "connections: %4u; Megapixels: %10" PRId64 "; p/s: %8u",
				server->connection_count, server->total_pixels_received/1000000, server->pixels_received_per_second);
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

int get_ip_of_default_gateway_interface (char* ip)
{
	// source: http://www.binarytides.com/get-local-ip-c-linux/
	FILE *f;
	char line[100] , *p , *c;
	 
	f = fopen("/proc/net/route" , "r");
	 
	while(fgets(line , 100 , f))
	{
		p = strtok(line , " \t");
		c = strtok(NULL , " \t");
		 
		if(p!=NULL && c!=NULL)
		{
			if(strcmp(c , "00000000") == 0)
			{
				// printf("Default interface is : %s \n" , p);
				break;
			}
		}
	}
	 
	//which family do we require , AF_INET or AF_INET6
	int fm = AF_INET;
	struct ifaddrs *ifaddr, *ifa;
	int family , s;
	// char ip[NI_MAXHOST];
 
	if (getifaddrs(&ifaddr) == -1) 
	{
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}
 
	//Walk through linked list, maintaining head pointer so we can free list later
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
	{
		if (ifa->ifa_addr == NULL)
		{
			continue;
		}
 
		family = ifa->ifa_addr->sa_family;
 
		if(strcmp( ifa->ifa_name , p) == 0)
		{
			if (family == fm) 
			{
				s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , ip , NI_MAXHOST , NULL , 0 , NI_NUMERICHOST);
				 
				if (s != 0) 
				{
					printf("Error: can not get IP-Address. (getnameinfo() failed: %s) (quick fix: do not use --show_ip_instead_of_ipname\n", gai_strerror(s));
					exit(EXIT_FAILURE);
				}
				// printf("address: %s", ip);
				return 0; //erfolg
			}
			// printf("\n");
		}
	}
	freeifaddrs(ifaddr);
	return 0;
}
