# Pixel
Fast pixelflut server written in C.

## Features
- Multithreaded
- Can display an overlay with some statistics
- Can fade out older pixels
- Serves real-time WebGL histogram and help text to browsers (same TCP port)

## Build SDL2 version (example)
```
sudo apt-get install build-essentials libsdl2-dev git
git clone https://github.com/ands/pixel.git
cd pixel
make sdl
./pixel_sdl --help
```

## Build Raspberry Pi version (not actively maintained)
```
sudo apt-get install build-essentials git
git clone https://github.com/ands/pixel.git
cd pixel
make pi
./pixel_pi
```

## TODO
- Use epoll() to check multiple sockets for I/O events at once