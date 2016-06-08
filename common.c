#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/thread.h>

#define XSTR(a) #a
#define STR(a) XSTR(a)

int bytesPerPixel;
uint8_t* pixels;
volatile int running = 1;
volatile int client_count = 0;
struct event_base *base;
volatile evutil_socket_t listener;

void set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a){
   if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
      uint8_t *pixel = ((uint8_t*)pixels) + (y * PIXEL_WIDTH + x) * bytesPerPixel; // RGB(A)
      if(a == 255){ // fast & usual path
         pixel[0] = r;
         pixel[1] = g;
         pixel[2] = b;
      }
      else{
         float alpha = a / 255.0f, nalpha = 1.0f - alpha;
         pixel[0] = (uint8_t)(r * alpha + pixel[0] * nalpha);
         pixel[1] = (uint8_t)(g * alpha + pixel[1] * nalpha);
         pixel[2] = (uint8_t)(b * alpha + pixel[2] * nalpha);
      }
   }
}

void handle_message(char *msg, struct evbuffer *output){
   if(!strncmp(msg, "PX ", 3)){
      char *x_end = msg + 3;
      uint32_t x = strtoul(msg + 3, &x_end, 10);
      if(msg + 3 == x_end) // no x?
         return;

      char *y_end = ++x_end;
      uint32_t y = strtoul(x_end, &y_end, 10);
      if(x_end == y_end) // no y?
         return;

      char *c_end = ++y_end;
      uint32_t c = strtoul(y_end, &c_end, 16);
      if(y_end == c_end) { // no color?
         if((x >= 0 && x <= PIXEL_WIDTH) && (y >= 0 && y <= PIXEL_HEIGHT)) {
            char out[8];
            sprintf(out, "%06x\n", pixels[y * PIXEL_WIDTH + x] & 0xffffff);
            evbuffer_add(output, out, sizeof(out) - 1);
         }
         return;
      }

      uint8_t r, g, b, a;
      int c_len = c_end - y_end;
           if(c_len > 6){ r = c >> 24; g = c >> 16; b = c >> 8; a = c  ; } // rgba
      else if(c_len > 2){ r = c >> 16; g = c >>  8; b = c     ; a = 255; } // rgb
      else              { r = c      ; g = c      ; b = c     ; a = 255; } // gray
      set_pixel(x, y, r, g, b, a);
      return;
   }

   if(!strncmp(msg, "SIZE", 4)) {
      static const char out[] = "SIZE " STR(PIXEL_WIDTH) " " STR(PIXEL_HEIGHT) "\n";
      evbuffer_add(output, out, sizeof(out) - 1);
      return;
   }

   if(!strncmp(msg, "CONNECTIONS", 11)) {
      char out[32];
      sprintf(out, "CONNECTIONS %d\n", client_count);
      evbuffer_add(output, out, strlen(out));
      return;
   }

   if(!strncmp(msg, "HELP", 4)) {
      static const char out[] =
         "send pixel: 'PX {x} {y} {GG or RRGGBB or RRGGBBAA as HEX}\\n'; "
         "request pixel: 'PX {x} {y}\\n'; "
         "request resolution: 'SIZE\\n'; "
         "request client connection count: 'CONNECTIONS\\n'; "
         "request this help message: 'HELP\\n';\n";
      evbuffer_add(output, out, sizeof(out) - 1);
      return;
   }

   evbuffer_add(output, "ERROR: UNKNOWN COMMAND\n", 6);
}

#define MAX_LINE 64
void client_read(struct bufferevent *bev, void *ctx){
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);
    char *line;
    while ((line = evbuffer_readln(input, NULL, EVBUFFER_EOL_ANY))) {
        handle_message(line, output);
        free(line);
    }

    if (evbuffer_get_length(input) >= MAX_LINE) {
        /* Too long; just remove what there is and go on so that the buffer doesn't grow infinitely long. */
        char buf[1024];
        while (evbuffer_get_length(input)) {
            /*int n =*/ evbuffer_remove(input, buf, sizeof(buf));
        }
    }
}

void client_error(struct bufferevent *bev, short error, void *ctx){
    if (error & BEV_EVENT_EOF) {
        /* connection has been closed, do any clean up here */
        --client_count;
    } else if (error & BEV_EVENT_ERROR) {
        fprintf(stderr, "ERROR: %d\n", errno);
        // --client_count; // ?
    } else if (error & BEV_EVENT_TIMEOUT) {
        /* must be a timeout event handle, handle it */
        --client_count;
    }
    bufferevent_free(bev);
}

void accept_client(evutil_socket_t listener, short event, void *arg){
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);
    if (fd < 0) {
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        close(fd);
    } else {
        struct bufferevent *bev;
        evutil_make_socket_nonblocking(fd);
        bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        bufferevent_set_timeouts(bev, &tv, &tv);
        bufferevent_setcb(bev, client_read, NULL, client_error, NULL);
        bufferevent_setwatermark(bev, EV_READ, 0, MAX_LINE);
        bufferevent_enable(bev, EV_READ|EV_WRITE);
        client_count++;
    }
}

void * handle_clients(void * foobar){
    struct sockaddr_in sin;
    struct event *listener_event;

    if (evthread_use_pthreads() < 0)
       perror("evthread_use_pthreads");

    base = event_base_new();
    if (!base) {
        perror("event_base_new");
        return NULL;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(PORT);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    evutil_make_socket_nonblocking(listener);

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return NULL;
    }

    if (listen(listener, FD_SETSIZE) < 0) {
        perror("listen");
        return NULL;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(listener, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, accept_client, (void*)base);
    if (!listener_event) {
        perror("event_new");
        return NULL;
    }
    event_add(listener_event, NULL);

    event_base_dispatch(base);
    return NULL;
}

#if SUPER_SECRET_UDP_BACKDOOR
volatile int listener_udp;
void * handle_udp(void * foobar){
   listener_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (listener_udp < 0) {
      perror("udp socket() failed");
      return 0;
   }

   if (setsockopt(listener_udp, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
      printf("udp setsockopt(SO_REUSEADDR) failed\n");

   struct sockaddr_in si_me = {};
   si_me.sin_family = AF_INET;
   si_me.sin_port = htons(PORT);
   si_me.sin_addr.s_addr = htonl(INADDR_ANY);
   if (bind(listener_udp, (struct sockaddr*)&si_me, sizeof(si_me)) < 0) {
      perror("udp bind() failed");
      return 0;
   }

   #define UDP_BUFFER_SIZE 65507
   while(running) {
      uint8_t buf[UDP_BUFFER_SIZE];
      int n = recv(listener_udp, buf, UDP_BUFFER_SIZE, 0);
      if (n < 0) {
         perror("udp recv() failed");
         return 0;
      }
      if (n > 6) {
         uint16_t *p = (uint16_t*)buf;
         int x = p[0], y = p[1], stride = p[2];
         uint8_t *data = buf + 6;
         int pixelCount = (n - 6) / 3;
         if (x + stride <= PIXEL_WIDTH) {
            while(y < PIXEL_HEIGHT && pixelCount) {
               int linePixelCount = pixelCount > stride ? stride : pixelCount;
               uint8_t *pixel = pixels + (y * PIXEL_WIDTH + x) * bytesPerPixel;
               int i;
               for (i = 0; i < linePixelCount; i++) {
                  pixel[0] = data[0];
                  pixel[1] = data[1];
                  pixel[2] = data[2];
                  pixel += bytesPerPixel;
                  data += 3;
               }
               y++;
               pixelCount -= linePixelCount;
            }
         }
      }
   }
   return 0;
}
#endif

pthread_t thread_id;
#if SUPER_SECRET_UDP_BACKDOOR
pthread_t udp_thread_id;
#endif
int server_start()
{
   pixels = calloc(PIXEL_WIDTH * PIXEL_HEIGHT, bytesPerPixel);

   if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0){
      perror("could not create tcp thread");
      running = 0;
      free(pixels);
      return 0;
   }

#if SUPER_SECRET_UDP_BACKDOOR
   if(pthread_create(&udp_thread_id , NULL, handle_udp , NULL) < 0){
      perror("could not create udp thread");
      running = 0;
      free(pixels);
      return 0;
   }
#endif

   return 1;
}

void server_stop()
{
   running = 0;
   printf("Shutting Down.\n");
   close(listener);
#if SUPER_SECRET_UDP_BACKDOOR
   close(listener_udp);
#endif
   printf("Stopped listening.\n");
   if (event_base_loopexit(base, NULL) < 0)
      perror("event_base_loopexit");
   printf("Exited event loop?\n");
   //pthread_join(thread_id, NULL); // TODO: really terminate the event loop!
   //printf("Joined main server thread...\n");
//#if SUPER_SECRET_UDP_BACKDOOR
   //pthread_join(udp_thread_id, NULL); // TODO: really terminate the thread!
   //printf("Joined secondary server thread.\n");
//#endif
   free(pixels);
}
