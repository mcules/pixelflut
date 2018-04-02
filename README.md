# Pixel
Fast pixelflut server written in C. It is a collaborative coding game. Project the pixelflut-server onto a wall where many people can see it. You can set single pixel by sending a string like "PX [x] [y] [color]\n" "PX 100 300 00FF12\n". Use netcat, python or what ever you want.

## Hardware requirements
Every x86 dual-core with a little bit of graphics power (for 2D SDL) should work. On an Core i3-4010U you can easily utilize 1 GBit Nic. On large events 10 GBit fiber and a few more CPU-Cores are even more fun. On real Server-Hardware you want to add a graphics card. 1 Thread per CPU-Core seems to be good.

## Features
- Multithreaded
- Can display an overlay with some statistics
- Serves real-time WebGL histogram and help text to browsers (same TCP port)

## Build
On a clean Debian installation with "SSH server" and "standard system utilities" selected during setup:
```
apt update
apt install xorg git build-essential pkg-config libsdl2-dev -y
git clone https://github.com/larsmm/pixel.git
cd pixel
make sdl
make pi  # Build Raspberry Pi version (not actively maintained)
./pixel_sdl --help
```

## Connection limit
Best practise: set overall limit of the pixelflut-server high (--connections_max 1000) and limit max connections to pixelflut-port 1234 per IP via iptables to 10-20:
```
nano iptables.save
```
paste (set limit in --connlimit-above):
```
*filter
:INPUT ACCEPT [0:0]
:FORWARD ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
-A INPUT -p tcp -m tcp --dport 1234 --tcp-flags FIN,SYN,RST,ACK SYN -m connlimit --connlimit-above 20 --connlimit-mask 32 --connlimit-saddr -j REJECT
COMMIT
```
Strg+x, y, return to save.
Activate:
```
iptables-restore < iptables.save
```

## Start Server
start x server first, then start pixelflut:
```
startx &  # start in background
./pixel_sdl --connections_max 1000 --threads 4 --fullscreen
```

## Stop Server
Press q or Strg+c

## Multiple screens
Normally pixelflut should run on a second screen (Projector). You have to tell pixelflut which display to use:
```
DISPLAY=:0.1 ./pixel_sdl --connections_max 1000 --threads 4 --fullscreen
```
- If you expand the main display the main display will be ":0.0" and the Projector ":0.1".
- If you duplicate the main display the main display will be ":0.0" and the Projector ":1.0".
- If you have only one display it will be ":0.0".

# Display driver
Sometimes the free NVidia driver has problems on multiple displays. So install the proprietary driver:
1. detect the chip and find the right driver:
```
apt install nvidia-detect nvidia-xconfig
nvidia-detect
```
2. install driver:
```
apt install for example: nvidia-legacy-340xx-driver
reboot
```
3. configure your displays:
```
nvidia-settings
nvidia-xconfig
```
restart x server

## Prevent standby
If you are using a notebook and want to close the lit. To disable all standby stuff:
```
systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
```

## TODO
- Use epoll() to check multiple sockets for I/O events at once
