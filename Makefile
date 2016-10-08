CC = gcc
EXE_SDL = pixel_sdl
EXE_PI = pixel_pi

# configuration
PIXEL_WIDTH = 800
PIXEL_HEIGHT = 600
HOST = propaganda
PORT = 1234
CONNECTION_TIMEOUT = 5 # seconds
UDP_PROTOCOL = 1
FADE_OUT = 1
SERVE_HISTOGRAM = 1 # over http on same port
FULLSCREEN_START = 0 # sdl only
PROFILING = 0

DEFINES = -DPIXEL_WIDTH=$(PIXEL_WIDTH) -DPIXEL_HEIGHT=$(PIXEL_HEIGHT) -DHOST=$(HOST) -DPORT=$(PORT) -DUDP_PROTOCOL=$(UDP_PROTOCOL)
DEFINES += -DCONNECTION_TIMEOUT=$(CONNECTION_TIMEOUT) -DFADE_OUT=$(FADE_OUT) -DSERVE_HISTOGRAM=$(SERVE_HISTOGRAM)
DEFINES += -DFULLSCREEN_START=$(FULLSCREEN_START)
DEFINES += -DRMT_ENABLED=$(PROFILING) #-DRMT_USE_OPENGL=1
IP = $(shell ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $$2}' | cut -f1 -d'/')
INFO = $(IP):$(PORT) $(PIXEL_WIDTH)x$(PIXEL_HEIGHT)

all:
	@echo "please execute either 'make sdl' or 'make pi'."



LDFLAGS_PI = -O3 -Wl,--whole-archive -L/opt/vc/lib/ -lGLESv2 -lEGL -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -L/opt/vc/src/hello_pi/libs/vgfont -ldl -lm -Wl,--no-whole-archive -rdynamic
CFLAGS_PI = -c -O3 -g -DUSE_OPENGL -DUSE_EGL -DIS_RPI -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -I./ -I/opt/vc/src/hello_pi/libs/ilclient -I/opt/vc/src/hello_pi/libs/vgfont -g -Wno-deprecated-declarations -Wno-missing-braces

pi: $(EXE_PI)

$(EXE_PI): main_pi.o
	$(CC) -o $@ main_pi.o $(LDFLAGS_PI)

main_pi.o: main_pi.c
	$(CC) $(CFLAGS_PI) $(DEFINES) $< -o $@



LDFLAGS_SDL = `pkg-config --libs sdl2` -lm -ldl -lGL -lpthread -O3 -fsanitize=address -g -fno-omit-frame-pointer
CFLAGS_SDL = -isystem "Remotery/lib/" -Wall -Wextra -Werror -pedantic -std=c11 -c `pkg-config --cflags sdl2` -O3 -fsanitize=address -g -fno-omit-frame-pointer

sdl: $(EXE_SDL)

$(EXE_SDL): main_sdl.o
	$(CC) -o $@ main_sdl.o $(LDFLAGS_SDL)

main_sdl.o: main_sdl.c
	$(CC) $(CFLAGS_SDL) $(DEFINES) $< -o $@



clean:
	rm -rf *.o $(EXE_SDL) $(EXE_PI)

run:
	./$(EXE_SDL) &
	./$(EXE_PI) &
	convert -size 320x20 xc:Transparent -pointsize 20 -fill black -draw "text 2,19 '$(INFO)'" -fill white -draw "text 0,17 '$(INFO)'" ip.png
	watch -n 10 python client.py 127.0.0.1 $(PORT) ip.png > /dev/null
