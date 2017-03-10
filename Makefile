CC = gcc
EXE_SDL = pixel_sdl
EXE_PI = pixel_pi

all:
	@echo "please execute either 'make sdl' or 'make pi'."


LDFLAGS_PI = -O3 -Wl,--whole-archive -L/opt/vc/lib/ -lGLESv2 -lEGL -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -L/opt/vc/src/hello_pi/libs/vgfont -ldl -lm -Wl,--no-whole-archive -rdynamic -fomit-frame-pointer -flto
CFLAGS_PI = -c -O3 -march=native -DUSE_OPENGL -DUSE_EGL -DIS_RPI -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -I./ -I/opt/vc/src/hello_pi/libs/ilclient -I/opt/vc/src/hello_pi/libs/vgfont -Wno-deprecated-declarations -Wno-missing-braces -fomit-frame-pointer

pi: $(EXE_PI)

$(EXE_PI): main_pi.o
	$(CC) -o $@ main_pi.o $(LDFLAGS_PI)

main_pi.o: main_pi.c
	$(CC) $(CFLAGS_PI) $< -o $@



LDFLAGS_SDL = `pkg-config --libs sdl2` -lm -ldl -lGL -lpthread -O3 -flto -fomit-frame-pointer
CFLAGS_SDL = -Wall -Wextra -Werror -pedantic -std=c11 -c `pkg-config --cflags sdl2` -O3 -flto -march=native -fomit-frame-pointer

sdl: $(EXE_SDL)

$(EXE_SDL): main_sdl.o
	$(CC) -o $@ main_sdl.o $(LDFLAGS_SDL)

main_sdl.o: main_sdl.c
	$(CC) $(CFLAGS_SDL) $< -o $@



clean:
	rm -rf *.o $(EXE_SDL) $(EXE_PI)
