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

#define BUFSIZE 2048

#define XSTR(a) #a
#define STR(a) XSTR(a)

int bytesPerPixel;
uint8_t* pixels;
volatile int running = 1;
volatile int client_thread_count = 0;
volatile int server_sock;
volatile unsigned int histogram[8][8][8] = {0};
volatile unsigned int total = 0;

uint8_t* index_html = 0;
int index_html_len = 0;

void * handle_client(void *);
void * handle_clients(void *);

void set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
   if(x < PIXEL_WIDTH && y < PIXEL_HEIGHT){
      uint8_t *pixel = ((uint8_t*)pixels) + (y * PIXEL_WIDTH + x) * bytesPerPixel; // RGB(A)
      histogram[r >> 5][g >> 5][b >> 5]++; // color statistics
      total++;
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

void update_pixels()
{
   static int frame = 0;

   if (frame % 4 == 0)
   {
      // fade out
      for (int y = 0; y < PIXEL_HEIGHT; y++)
      {
         for (int x = 0; x < PIXEL_WIDTH; x++)
         {
            uint8_t *pixel = ((uint8_t*)pixels) + (y * PIXEL_WIDTH + x) * bytesPerPixel; // RGB(A)
            pixel[0] = pixel[0] ? pixel[0] - 1 : pixel[0];
            pixel[1] = pixel[1] ? pixel[1] - 1 : pixel[1];
            pixel[2] = pixel[2] ? pixel[2] - 1 : pixel[2];
         }
      }
      
      // update histogram
      for (int r = 0; r < 8; r++)
         for (int g = 0; g < 8; g++)
            for (int b = 0; b < 8; b++)
               histogram[r][g][b] *= 0.99;
   }
   
   frame++;
}

static void loadIndexHtml()
{
   const char http_ok[] = "HTTP/1.1 200 OK\r\n\r\n";

   FILE *file = fopen("index.html", "rt");
   if (!file)
   {
      fprintf(stderr, "Could not open index.html!\n");
      return;
   }
   fseek(file, 0, SEEK_END);
   long len = ftell(file);
   fseek(file, 0, SEEK_SET);

   index_html_len = len + sizeof(http_ok) - 1;
   index_html = malloc(index_html_len);
   memcpy(index_html, http_ok, sizeof(http_ok) - 1);
   int read = fread(index_html + sizeof(http_ok) - 1, 1, len, file);
   if (!read)
      fprintf(stderr, "Could not read index.html!\n");
   fclose(file);
}

char * itoa(int n, char *s)
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
   for (i = 0, j = l-1; i<j; i++, j--) {
      c = s[i];
      s[i] = s[j];
      s[j] = c;
   }
   return s + l;
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
         int i;
         for (i = 0; i < read_pos; i++){
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
               else if(!strncmp(buf, "GET", 3)){ // obviously totally HTTP compliant!
                  char out[16384];
                  if (!strncmp(buf + 4, "/data.json", 10)){
                     strcpy(out, "HTTP/1.1 200 OK\r\n\r\nvar data=[");
                     char *hp = out + sizeof("HTTP/1.1 200 OK\r\n\r\nvar data=[") - 1;
                     for (int hi = 0; hi < 8 * 8 * 8; hi++){
                        hp = itoa(((unsigned int*)&histogram[0][0][0])[hi], hp);
                        *hp++ = ',';
                     }
                     hp[-1] = ']';
                     hp[0] = 0;
                     //printf(out);
                     send(sock, out, strlen(out), MSG_DONTWAIT | MSG_NOSIGNAL);
                  }
                  else{
                     free(index_html);
                     loadIndexHtml(); // debug
                     send(sock, index_html, index_html_len, MSG_DONTWAIT | MSG_NOSIGNAL);
                  }
                  goto disconnect;
               }
               else{
                  /*printf("BULLSHIT[%i]: ", i);
                  int j;
                  for (j = 0; j < i; j++)
                     printf("%c", buf[j]);
                  printf("\n");*/
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
            //buf[sizeof(buf) - 1] = 0;
            //printf("BULLSHIT BUFFER: %s\n", buf);
            read_pos = 0;
         }
      }
   }
disconnect:
   close(sock);
   //printf("Client disconnected\n");
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
         //printf("Client %s connected\n", inet_ntoa(addr.sin_addr));
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

void * handle_udp(void * foobar){
   int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (s < 0){
      perror("udp socket() failed");
      return 0;
   }

   if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0)
      printf("udp setsockopt(SO_REUSEADDR) failed\n");

   struct sockaddr_in si_me = {};
   si_me.sin_family = AF_INET;
   si_me.sin_port = htons(PORT);
   si_me.sin_addr.s_addr = htonl(INADDR_ANY);
   if (bind(s, (struct sockaddr*)&si_me, sizeof(si_me)) < 0){
      perror("udp bind() failed");
      return 0;
   }

   #define UDP_BUFFER_SIZE 65507
   while(running){
      uint8_t buf[UDP_BUFFER_SIZE];
      int n = recv(s, buf, UDP_BUFFER_SIZE, 0);
      if (n < 0){
         perror("udp recv() failed");
         return 0;
      }
      if (n > 6)
      {
         uint16_t *p = (uint16_t*)buf;
         int x = p[0], y = p[1], stride = p[2];
         uint8_t *data = buf + 6;
         int pixelCount = (n - 6) / 3;
         if (x + stride <= PIXEL_WIDTH)
         {
            while(y < PIXEL_HEIGHT && pixelCount)
            {
               int linePixelCount = pixelCount > stride ? stride : pixelCount;
               uint8_t *pixel = pixels + (y * PIXEL_WIDTH + x) * bytesPerPixel;
               int i;
               for (i = 0; i < linePixelCount; i++)
               {
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

pthread_t thread_id;
pthread_t udp_thread_id;
int server_start()
{
   loadIndexHtml();

   pixels = calloc(PIXEL_WIDTH * PIXEL_HEIGHT, bytesPerPixel);

   if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0){
      perror("could not create tcp thread");
      running = 0;
      free(pixels);
      return 0;
   }

   if(pthread_create(&udp_thread_id , NULL, handle_udp , NULL) < 0){
      perror("could not create udp thread");
      running = 0;
      free(pixels);
      return 0;
   }

   return 1;
}

void server_stop()
{
   running = 0;
   printf("Shutting Down...\n");
   //while (client_thread_count)
   //   usleep(100000);
   close(server_sock);
   //pthread_join(thread_id, NULL);
   //pthread_join(udp_thread_id, NULL);
   free(pixels);
}
