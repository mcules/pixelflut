#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>

#include "framebuffer.c"
#include "histogram.c"

typedef struct server_t server_t;

typedef struct
{
	server_t *server;
	pthread_t thread;
	int socket;
	int offset_x, offset_y;
	
	int buffer_used;
	char buffer[2048];
} client_connection_t;

typedef int server_flags_t;
#define SERVER_NONE              0
#define SERVER_FADE_OUT_ENABLED  1
#define SERVER_HISTOGRAM_ENABLED 2

struct server_t
{
	framebuffer_t framebuffer;
	histogram_t histogram;

	int socket;
	int timeout;
	volatile int running;
	server_flags_t flags;
	int frame;
	pthread_t thread;
	//pthread_t *threads; // TODO
	//client_connection_t *connections; // TODO
	atomic_uint connection_count;
};

#include "commandhandler.c"

static void server_update(server_t *server)
{
	if (server->frame % 4 == 0)
	{
		if (server->flags & SERVER_FADE_OUT_ENABLED)
			framebuffer_fade_out(&server->framebuffer);
		if (server->flags & SERVER_HISTOGRAM_ENABLED)
			histogram_update(&server->histogram);
	}

	server->frame++;
}

static void *server_client_thread(void *param)
{
	client_connection_t *client = (client_connection_t*)param;
	server_t *server = client->server;
	atomic_fetch_add(&server->connection_count, 1);
	int read_size;
	while(server->running && (read_size = recv(client->socket, client->buffer + client->buffer_used, sizeof(client->buffer) - client->buffer_used , 0)) > 0)
	{
		client->buffer_used += read_size;
		char *start, *end;
		start = end = client->buffer;
		while (end < client->buffer + client->buffer_used)
		{
			if (*end == '\n')
			{
				*end = 0;
				command_status_t status = command_handler(client, start);
				if (status != COMMAND_SUCCESS)
					goto disconnect;
				start = end + 1;
			}
			end++;
		}

		int offset = start - client->buffer;
		int count = end - start;
		if (offset > 0 && count > 0)
			memmove(client->buffer, start, count);
		client->buffer_used -= offset;
	}

disconnect:
	close(client->socket);
	free(client);
	atomic_fetch_add(&server->connection_count, -1);
	return 0;
}

static void *server_listen_thread(void *param)
{
	socklen_t addr_len;
	struct sockaddr_in addr;
	addr_len = sizeof(addr);
	struct timeval tv;

	printf("Starting Server...\n");

	server_t *server = (server_t*)param;
	server->socket = socket(PF_INET, SOCK_STREAM, 0);

	tv.tv_sec = server->timeout;
	tv.tv_usec = 0;

	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);
	addr.sin_family = AF_INET;

	if (server->socket == -1)
	{
		perror("socket() failed");
		return 0;
	}

	if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
		printf("setsockopt(SO_REUSEADDR) failed\n");
	if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0)
		printf("setsockopt(SO_REUSEPORT) failed\n");

	int retries;
	for (retries = 0; bind(server->socket, (struct sockaddr*)&addr, sizeof(addr)) == -1 && retries < 10; retries++)
	{
		perror("bind() failed ...retry in 5s");
		usleep(5000000);
	}
	if (retries == 10)
		return 0;

	if (listen(server->socket, 4) == -1)
	{
		perror("listen() failed");
		return 0;
	}
	printf("Listening...\n");

	setsockopt(server->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
	setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

	while (server->running)
	{
		client_connection_t *client = calloc(1, sizeof(client_connection_t));
		client->server = server;
		client->socket = accept(server->socket, (struct sockaddr*)&addr, &addr_len);
		if (client->socket > 0)
		{
			//printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
			if (pthread_create(&client->thread, NULL, server_client_thread, client) < 0)
			{
				close(client->socket);
				free(client);
				perror("could not create thread");
			}
		}
	}
	close(server->socket);
	
	printf("Ended listenting.\n");
	return 0;
}

static int server_start(
	server_t *server,
	int width, int height, int bytesPerPixel,
	int timeout, server_flags_t flags)
{
	server->socket = 0;
	server->timeout = timeout;
	server->running = 1;
	server->flags = flags;
	server->frame = 0;

	framebuffer_init(&server->framebuffer, width, height, bytesPerPixel);
	
	if (flags & SERVER_HISTOGRAM_ENABLED)
		histogram_init(&server->histogram);

	if (pthread_create(&server->thread, NULL, server_listen_thread, server) < 0)
	{
		perror("could not create tcp thread");
		server->running = 0;
		framebuffer_free(&server->framebuffer);
		return 0;
	}

	return 1;
}

static void server_stop(server_t *server)
{
	server->running = 0;
	printf("Shutting Down %d childs ...\n", server->connection_count);
	while (server->connection_count)
		usleep(100000);
	printf("Shutting Down socket ...\n");
	close(server->socket);

	printf("Joining threads ... ");
	pthread_join(server->thread, NULL);

	printf("Destroying framebuffer...\n");
	framebuffer_free(&server->framebuffer);

	if (server->flags & SERVER_HISTOGRAM_ENABLED)
	{
		printf("Destroying histogram...\n");
		histogram_free(&server->histogram);
	}
}
