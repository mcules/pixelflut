# pixel
Fast pixelflut server written in C.

# SDL2 version example installation
```
sudo apt-get install build-essentials libsdl2-dev git
git clone https://github.com/ands/pixel.git
cd pixel
make sdl
./pixel_sdl --help
```

# Raspberry Pi version installation (not regularly maintained)
```
sudo apt-get install build-essentials git
git clone https://github.com/ands/pixel.git
cd pixel
make pi
./pixel_pi
```

#TODO
- Use epoll() to check multiple sockets for I/O events