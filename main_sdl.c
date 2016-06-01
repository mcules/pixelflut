#include "common.c"
#include <SDL.h>

int main(){
   SDL_Init(SDL_INIT_VIDEO);
   SDL_ShowCursor(0);

   SDL_Window* window = SDL_CreateWindow(
      "pixel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      PIXEL_WIDTH, PIXEL_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
   SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
   SDL_RenderClear(renderer);
   
   SDL_Texture* sdlTexture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
      PIXEL_WIDTH, PIXEL_HEIGHT);
   if(!sdlTexture){
      printf("could not create texture");
      SDL_Quit();
      return 1;
   }
   bytesPerPixel = 4;
   pixels = calloc(PIXEL_WIDTH * PIXEL_HEIGHT * bytesPerPixel, 1);

   pthread_t thread_id;
   if(pthread_create(&thread_id , NULL, handle_clients , NULL) < 0){
      perror("could not create thread");
      free(pixels);
      SDL_Quit();
      return 1;
   }
   
   while(42){
      SDL_UpdateTexture(sdlTexture, NULL, pixels, PIXEL_WIDTH * bytesPerPixel);
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
