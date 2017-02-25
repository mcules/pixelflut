typedef int command_status_t;
#define COMMAND_SUCCESS 0 // keeps connection alive for further commands
#define COMMAND_ERROR   1 // can close connection in case of errors
#define COMMAND_CLOSE   2 // always closes connection (after http response etc.)

static inline char * itoa(int n, char *s)
{
	int i, j, l, sign;
	char c;
	if ((sign = n) < 0)  /* record sign */
		n = -n;          /* make n positive */
	i = 0;
	do {       /* generate digits in reverse order */
		s[i++] = n % 10 + '0';   /* get next digit */
	} while ((n /= 10) > 0);     /* delete it */
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	l = i;
	for (i = 0, j = l-1; i<j; i++, j--)
	{
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
	return s + l;
}

static inline int atoi_simple(char *str, char **next)
{
	int res = 0;
	int sign = 1;
	if (*str == '-')
	{
		sign = -1;
		++str;
	}
	while (*str >= '0' && *str <= '9')
	{
		res = res * 10 + (*str - '0');
		++str;
	}
	*next = str;
	return sign * res;
}

static command_status_t command_handler(client_connection_t *client, char *cmd)
{
	server_t *server = client->server;
	framebuffer_t *framebuffer = &server->framebuffer;
	if(cmd[0] == 'P' && cmd[1] == 'X' && cmd[2] == ' ')
	{
		char *pos1 = cmd + 3;
		int x = atoi_simple(cmd + 3, &pos1);
		if (cmd == pos1)
			return COMMAND_ERROR;
		char *pos2 = ++pos1;
		int y = atoi_simple(pos1, &pos2);
		if (pos1 == pos2)
			return COMMAND_ERROR;
		x += client->offset_x;
		y += client->offset_y;
		if ((uint32_t)x >= framebuffer->width || (uint32_t)y >= framebuffer->height)
			return COMMAND_ERROR;
		pos1 = ++pos2;

		uint32_t c = 0;
		pos1 = pos2;
		for (int i = 0; i < 8; i++)
		{
			if (*pos1 >= '0' && *pos1 <= '9')
				c = c << 4 | (*pos1 - '0');
			else if (*pos1 >= 'a' && *pos1 <= 'f')
				c = c << 4 | (*pos1 - 'a' + 10);
			else if (*pos1 >= 'A' && *pos1 <= 'F')
				c = c << 4 | (*pos1 - 'A' + 10);
			else
				break;
			pos1++;
		}
		if (pos2 == pos1) // no color specified -> color request
		{
			char colorout[30];
			uint8_t *pixel = framebuffer->pixels + (y * framebuffer->width + x) * framebuffer->bytesPerPixel; // RGB(A)
			snprintf(colorout, sizeof(colorout), "PX %d %d %02x%02x%02x\n", x, y, pixel[0], pixel[1], pixel[2]);
			send(client->socket, colorout, strlen(colorout), MSG_DONTWAIT | MSG_NOSIGNAL);
			return COMMAND_SUCCESS;
		}

		int codelen = pos1 - pos2;
		uint8_t r, g, b, a;
		if (codelen > 6) { r = c >> 24; g = c >> 16; b = c >> 8; a =   c; } else // rgba
		if (codelen > 2) { r = c >> 16; g = c >>  8; b = c     ; a = 255; } else // rgb
		                 { r = c      ; g = c      ; b = c     ; a = 255; }      // gray

		uint8_t *pixel = framebuffer->pixels + (y * framebuffer->width + x) * framebuffer->bytesPerPixel; // RGB(A)

		if (server->flags & SERVER_HISTOGRAM_ENABLED)
			server->histogram.buckets[r >> 5][g >> 5][b >> 5]++; // color statistics

		if (a == 255) // fast & usual path
		{
			pixel[0] = r;
			pixel[1] = g;
			pixel[2] = b;
		}
		else
		{
			int alpha = a * 65793;
			int nalpha = (255 - a) * 65793;
			pixel[0] = (uint8_t)(r * alpha + pixel[0] * nalpha) >> 16;
			pixel[1] = (uint8_t)(g * alpha + pixel[1] * nalpha) >> 16;
			pixel[2] = (uint8_t)(b * alpha + pixel[2] * nalpha) >> 16;
		}
		
		atomic_fetch_add(&server->total_pixels_received, 1);
		atomic_fetch_add(&server->pixels_per_second_counter, 1);
		
		return COMMAND_SUCCESS;
	}
	else if(!strncmp(cmd, "OFFSET ", 7))
	{
		int32_t x, y;
		if (sscanf(cmd + 7, "%d %d", &x, &y) != 2)
			return COMMAND_ERROR;
		client->offset_x = x;
		client->offset_y = y;
		return COMMAND_SUCCESS;
	}
	else if(!strncmp(cmd, "SIZE", 4))
	{
		char out[32];
		int l = sprintf(out, "SIZE %d %d\n", framebuffer->width, framebuffer->height);
		send(client->socket, out, l, MSG_DONTWAIT | MSG_NOSIGNAL);
		return COMMAND_SUCCESS;
	}
	else if(!strncmp(cmd, "CONNECTIONS", 11))
	{
		char out[32];
		int l = sprintf(out, "CONNECTIONS %d\n", server->connection_count);
		send(client->socket, out, l, MSG_DONTWAIT | MSG_NOSIGNAL);
		return COMMAND_SUCCESS;
	}
	else if(!strncmp(cmd, "HELP", 4))
	{
		static const char out[] =
			"send pixel: 'PX {x} {y} {GG or RRGGBB or RRGGBBAA as HEX}\\n'; "
			"set offset for future pixels: 'OFFSET {x} {y}\\n'; "
			"request pixel: 'PX {x} {y}\\n'; "
			"request resolution: 'SIZE\\n'; "
			"request client connection count: 'CONNECTIONS\\n'; "
			"request this help message: 'HELP\\n';\n";
		send(client->socket, out, sizeof(out) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
		return COMMAND_SUCCESS;
	}
	else if(server->flags & SERVER_HISTOGRAM_ENABLED && !strncmp(cmd, "GET", 3)) // obviously totally HTTP compliant!
	{
		if (!strncmp(cmd + 4, "/data.json", 10))
		{
			char out[16384];
			strcpy(out, "HTTP/1.1 200 OK\r\n\r\n[");
			char *hp = out + sizeof("HTTP/1.1 200 OK\r\n\r\n[") - 1;
			uint32_t *buckets = server->histogram.buckets[0][0];
			for (uint32_t hi = 0; hi < 8 * 8 * 8; hi++)
			{
				hp = itoa(buckets[hi], hp);
				*hp++ = ',';
			}
			hp[-1] = ']';
			hp[0] = 0;
			send(client->socket, out, strlen(out), MSG_DONTWAIT | MSG_NOSIGNAL);
			return COMMAND_CLOSE;
		}
		else
		{
			send(client->socket, server->histogram.index_html, server->histogram.index_html_len, MSG_DONTWAIT | MSG_NOSIGNAL);
			return COMMAND_CLOSE;
		}
	}

	return COMMAND_ERROR;
}
