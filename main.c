#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <SDL.h>

#define BUFSIZE 2048

#define XSTR(a) #a
#define STR(a) XSTR(a)

uint32_t* pixels;
volatile int running = 1;
volatile int client_thread_count = 0;
volatile int server_sock;

void * handle_client(void *);
void * handle_clients(void *);

void set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
   if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
      uint8_t *pixel = ((uint8_t*)pixels) + (y * PIXEL_WIDTH + x) * 4; // BGRA
      if(a == 255){ // fast & usual path
         pixel[2] = r;
         pixel[1] = g;
         pixel[0] = b;
      }
      else{
         float alpha = a / 255.0f, nalpha = 1.0f - alpha;
         pixel[2] = (uint8_t)(r * alpha + pixel[2] * nalpha);
         pixel[1] = (uint8_t)(g * alpha + pixel[1] * nalpha);
         pixel[0] = (uint8_t)(b * alpha + pixel[0] * nalpha);
      }
   }
}

void * handle_client(void *s){
   client_thread_count++;
   int sock = *(int*)s;
   char buf[BUFSIZE];
   int read_size, read_pos = 0;
   uint32_t x,y,c;
   while(running && (read_size = recv(sock , buf + read_pos, sizeof(buf) - read_pos , 0)) > 0){
      read_pos += read_size;
      int found = 1;
      while (found){
         found = 0;
         for (int i = 0; i < read_pos; i++){
            if (buf[i] == '\n'){
               buf[i] = 0;
               if(!strncmp(buf, "PX ", 3)){ // ...don't ask :D...
                  char *pos1 = buf + 3;
                  x = strtoul(buf + 3, &pos1, 10);
                  if(buf != pos1){
                     pos1++;
                     char *pos2 = pos1;
                     y = strtoul(pos1, &pos2, 10);
                     if(pos1 != pos2){
                        pos2++;
                        pos1 = pos2;
                        c = strtoul(pos2, &pos1, 16);
                        if(pos2 != pos1){
                           uint8_t r, g, b, a;
                           int codelen = pos1 - pos2;
                           if(codelen > 6){ // rgba
                              r = c >> 24;
                              g = c >> 16;
                              b = c >> 8;
                              a = c;
                           }
                           else if(codelen > 2){ // rgb
                              r = c >> 16;
                              g = c >> 8;
                              b = c;
                              a = 255;
                           }
                           else{ // gray
                              r = c;
                              g = c;
                              b = c;
                              a = 255;
                           }
                           set_pixel(x, y, r, g, b, a);
                        }
                        else if((x >= 0 && x <= PIXEL_WIDTH) && (y >= 0 && y <= PIXEL_HEIGHT)){
                           char colorout[8];
                           sprintf(colorout, "%06x\n", pixels[y * PIXEL_WIDTH + x] & 0xffffff);
                           send(sock, colorout, sizeof(colorout) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
                        }
                     }
                  }
               }
               else if(!strncmp(buf, "SIZE", 4)){
                  static const char out[] = "SIZE " STR(PIXEL_WIDTH) " " STR(PIXEL_HEIGHT) "\n";
                  send(sock, out, sizeof(out) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
               }
               else if(!strncmp(buf, "CONNECTIONS", 11)){
                  char out[32];
                  sprintf(out, "CONNECTIONS %d\n", client_thread_count);
                  send(sock, out, strlen(out), MSG_DONTWAIT | MSG_NOSIGNAL);
               }
               else if(!strncmp(buf, "HELP", 4)){
                  static const char out[] =
                     "send pixel: 'PX {x} {y} {GG or RRGGBB or RRGGBBAA as HEX}\\n'; "
                     "request pixel: 'PX {x} {y}\\n'; "
                     "request resolution: 'SIZE\\n'; "
                     "request client connection count: 'CONNECTIONS\\n'; "
                     "request this help message: 'HELP\\n';\n";
                  send(sock, out, sizeof(out) - 1, MSG_DONTWAIT | MSG_NOSIGNAL);
               }
               else{
                  printf("BULLSHIT[%i]: ", i);
                  for (int j = 0; j < i; j++)
                     printf("%c", buf[j]);
                  printf("\n");
               }
               int offset = i + 1;
               int count = read_pos - offset;
               if (count > 0)
                  memmove(buf, buf + offset, count); // TODO: ring buffer?
               read_pos -= offset;
               found = 1;
               break;
            }
         }
         if (sizeof(buf) - read_pos == 0){ // received only garbage for a whole buffer. start over!
            buf[sizeof(buf) - 1] = 0;
            printf("BULLSHIT BUFFER: %s\n", buf);
            read_pos = 0;
         }
      }
   }
   close(sock);
   printf("Client disconnected\n");
   fflush(stdout);
   client_thread_count--;
   return 0;
}

void * handle_clients(void * foobar){
   pthread_t thread_id;
   int client_sock;
   socklen_t addr_len;
   struct sockaddr_in addr;
   addr_len = sizeof(addr);
   struct timeval tv;
   
   printf("Starting Server...\n");
   
   server_sock = socket(PF_INET, SOCK_STREAM, 0);

   tv.tv_sec = 5;
   tv.tv_usec = 0;

   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(PORT);
   addr.sin_family = AF_INET;
   
   if (server_sock == -1){
      perror("socket() failed");
      return 0;
   }
   
   if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
      printf("setsockopt(SO_REUSEADDR) failed\n");
   if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0)
      printf("setsockopt(SO_REUSEPORT) failed\n");

   int retries;
   for (retries = 0; bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 && retries < 10; retries++){
      perror("bind() failed ...retry in 5s");
      usleep(5000000);
   }
   if (retries == 10)
      return 0;

   if (listen(server_sock, 3) == -1){
      perror("listen() failed");
      return 0;
   }
   printf("Listening...\n");
   
   setsockopt(server_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv,sizeof(struct timeval));
   setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

   while(running){
      client_sock = accept(server_sock, (struct sockaddr*)&addr, &addr_len);
      if(client_sock > 0){
         printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
         if( pthread_create( &thread_id , NULL ,  handle_client , (void*) &client_sock) < 0)
         {
            close(client_sock);
            perror("could not create thread");
         }
      }
   }
   close(server_sock);
   return 0;
}

int main(){
   SDL_Init(SDL_INIT_VIDEO);
   SDL_ShowCursor(0);

   SDL_Window* window = SDL_CreateWindow(
      "pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
   SDL_RenderClear(renderer);
   
   SDL_Texture* sdlTexture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      PIXEL_WIDTH, PIXEL_HEIGHT);
   if(!sdlTexture){
      printf("could not create texture");
      SDL_Quit();
      return 1;
   }
   
   pixels = calloc(PIXEL_WIDTH * PIXEL_HEIGHT * 4, 1);

   pthread_t thread_id;
   if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0){
      perror("could not create thread");
      free(pixels);
      SDL_Quit();
      return 1;
   }
   
   while(42){
      SDL_UpdateTexture(sdlTexture, NULL, pixels, PIXEL_WIDTH * sizeof(uint32_t));
      SDL_RenderCopy(renderer, sdlTexture, NULL, NULL);
      SDL_RenderPresent(renderer);
      SDL_Event event;
      if(SDL_PollEvent(&event)){
         if(event.type == SDL_QUIT){
            break;
         }
         if(event.type == SDL_KEYDOWN){
            if(event.key.keysym.sym == SDLK_q){
               break;
            }
            if(event.key.keysym.sym == SDLK_f){
               uint32_t flags = SDL_GetWindowFlags(window);
               SDL_SetWindowFullscreen(window,
                  (flags & SDL_WINDOW_FULLSCREEN) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
               printf("Toggled Fullscreen\n");
            }
         }
      }
   }

   running = 0;
   printf("Shutting Down...\n");
   SDL_DestroyWindow(window);
   while (client_thread_count)
      usleep(100000);
   close(server_sock);
   pthread_join(thread_id, NULL);
   free(pixels);
   SDL_Quit();
   return 0;
}
