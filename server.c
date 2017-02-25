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
#include <time.h>

#include "framebuffer.c"
#include "histogram.c"

typedef struct server_t server_t;

typedef struct
{
	server_t *server;
	int socket;
	atomic_bool lock;

	struct timespec last_msg_time;

	int offset_x, offset_y;
	int buffer_used;
	char buffer[65536];
} client_connection_t;

typedef int server_flags_t;
#define SERVER_NONE              0
#define SERVER_FADE_OUT_ENABLED  1
#define SERVER_HISTOGRAM_ENABLED 2

struct server_t
{
	framebuffer_t framebuffer;
	histogram_t histogram;

	uint16_t port;
	int socket;
	int timeout;
	int threads;
	server_flags_t flags;
	int fade_interval;
	
	volatile int running;
	int frame;
	uint32_t pixels_received_per_second;
	struct timespec prev_second;
	
	pthread_t listen_thread;
	pthread_t *client_threads;
	client_connection_t *connections;
	int connection_capacity;
	atomic_uint connection_count;
	atomic_uint connection_current;
	atomic_uint pixels_per_second_counter;
	atomic_uint_fast64_t total_pixels_received;
};

#include "commandhandler.c"

static void server_client_disconnect(client_connection_t *client)
{
	int socket = client->socket;
	client->socket = 0;
	client->offset_x = 0;
	client->offset_y = 0;
	client->buffer_used = 0;
	close(socket);
	atomic_fetch_add(&client->server->connection_count, -1);
	//printf("client disconnected\n");
}

static void server_poll_client_connection(client_connection_t *client, struct timespec *now)
{
	if (now->tv_sec - client->last_msg_time.tv_sec >= client->server->timeout) // ignores the nsec for now...
	{
		printf("server closed connection after timeout\n");
		server_client_disconnect(client);
		return;
	}

	int read_size = recv(client->socket, client->buffer + client->buffer_used, sizeof(client->buffer) - client->buffer_used , MSG_DONTWAIT);
	if(read_size > 0)
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
				if (status == COMMAND_SUCCESS)
				{
					clock_gettime(CLOCK_MONOTONIC, &client->last_msg_time);
				}
				else if (status == COMMAND_CLOSE)
				{
					//printf("server closed connection\n");
					server_client_disconnect(client);
					return;
				}
				start = end + 1;
			}
			end++;
		}

		int offset = start - client->buffer;
		int count = client->buffer_used - offset;
		if (count == client->buffer_used || offset == client->buffer_used)
			client->buffer_used = 0;
		else if (offset > 0 && count > 0)
		{
			memmove(client->buffer, start, count);
			client->buffer_used -= offset;
		}
	}
	else if (read_size == 0) // = disconnected
	{
		//printf("client closed connection\n");
		server_client_disconnect(client);
	}
	else if (errno != EAGAIN && errno != ECONNRESET)
	{
		fprintf(stderr, "client recv error: %d on socket %d\n", errno, client->socket);
	}
}

static void *server_client_thread(void *param)
{
	server_t *server = (server_t*)param;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int clockSkipCounter = 0;

	while(server->running || server->connection_count)
	{
		// only update the current time once in a while to save cpu time
		if (++clockSkipCounter > 1024)
		{
			clockSkipCounter = 0;
			clock_gettime(CLOCK_MONOTONIC, &now);
		}

		unsigned int index = server->connection_current;
		while (!atomic_compare_exchange_weak(&server->connection_current, &index, index + 1))
			index = server->connection_current;
		
		client_connection_t *client = server->connections + index % server->connection_capacity;
		if (!atomic_flag_test_and_set(&client->lock))
		{
			if (client->socket)
			{
				if (server->running)
					server_poll_client_connection(client, &now);
				else
					server_client_disconnect(client);
			}
			atomic_flag_clear(&client->lock);
		}
	}
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
	addr.sin_port = htons(server->port);
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

	if (listen(server->socket, 32) == -1)
	{
		perror("listen() failed");
		return 0;
	}
	printf("Listening...\n");

	setsockopt(server->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
	setsockopt(server->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

	while (server->running)
	{
		for (int i = 0; i < server->connection_capacity && server->running; i++)
		{
			if (!server->connections[i].socket)
			{
				client_connection_t *client = server->connections + i;
				client->server = server;
				int client_socket = accept(server->socket, (struct sockaddr*)&addr, &addr_len);
				if (client_socket > 0)
				{
					clock_gettime(CLOCK_MONOTONIC, &client->last_msg_time);
					client->socket = client_socket;
					atomic_fetch_add(&server->connection_count, 1);
					//printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
				}
			}
		}
	}
	close(server->socket);
	
	printf("Ended listenting.\n");
	return 0;
}

static int server_start(
	server_t *server, uint16_t port, int max_connections, int timeout, int threads,
	int width, int height, int bytesPerPixel, int fade_interval, server_flags_t flags)
{
	server->port = port;
	server->socket = 0;
	server->timeout = timeout;
	server->threads = threads;
	server->flags = flags;
	server->fade_interval = fade_interval;
	server->running = 1;
	server->frame = 0;

	server->connections = calloc(max_connections, sizeof(client_connection_t));
	if (!server->connections)
	{
		perror("could not allocate max connections");
		server->running = 0;
		return 0;
	}
	server->connection_capacity = max_connections;
	
	assert(fade_interval > 0);

	framebuffer_init(&server->framebuffer, width, height, bytesPerPixel);
	
	if (flags & SERVER_HISTOGRAM_ENABLED)
		histogram_init(&server->histogram);

	if (pthread_create(&server->listen_thread, NULL, server_listen_thread, server) < 0)
	{
		perror("could not create listen thread");
		server->running = 0;
		free(server->connections);
		framebuffer_free(&server->framebuffer);
		return 0;
	}
	
	server->client_threads = calloc(threads, sizeof(pthread_t));
	for (int i = 0; i < threads; i++)
		if (pthread_create(&server->client_threads[i], NULL, server_client_thread, server) < 0)
			perror("could not create client thread");

	clock_gettime(CLOCK_MONOTONIC, &server->prev_second);

	return 1;
}

static void server_stop(server_t *server)
{
	server->running = 0;
	close(server->socket);

	printf("Closing %d client connections ...\n", server->connection_count);
	while (server->connection_count)
		usleep(100000);

	printf("Joining threads ... ");
	pthread_join(server->listen_thread, NULL);
	for (int i = 0; i < server->threads; i++)
		pthread_join(server->client_threads[i], NULL);
	free(server->client_threads);

	printf("Destroying framebuffer...\n");
	framebuffer_free(&server->framebuffer);

	if (server->flags & SERVER_HISTOGRAM_ENABLED)
	{
		printf("Destroying histogram...\n");
		histogram_free(&server->histogram);
	}

	free(server->connections);
}

static void server_update(server_t *server)
{
	if (server->frame % server->fade_interval == 0)
	{
		if (server->flags & SERVER_FADE_OUT_ENABLED)
			framebuffer_fade_out(&server->framebuffer);
		if (server->flags & SERVER_HISTOGRAM_ENABLED)
			histogram_update(&server->histogram);
	}
	
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	float delta = (float)((double)(time.tv_sec - server->prev_second.tv_sec) + 0.000000001 * (double)(time.tv_nsec - server->prev_second.tv_nsec));
	if (delta >= 1.0f)
	{
		server->prev_second = time;
		uint32_t pixels_per_second_counter = server->pixels_per_second_counter;
		while (!atomic_compare_exchange_weak(&server->pixels_per_second_counter, &pixels_per_second_counter, 0))
			pixels_per_second_counter = server->pixels_per_second_counter;
		server->pixels_received_per_second = pixels_per_second_counter;
	}

	server->frame++;
}
