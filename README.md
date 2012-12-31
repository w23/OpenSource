OpenSource
==========

This all began more than ten years ago when I was playing Half-Life for the first time and noticed
that some maps should really overlap if they were connected the way they were at the loading location.
(e.g. there's this huge area, then a shallow path, right turn, load, right turn again and then another huge area).

I've been wondering since, what would the map of entire Half-Life look like if you merge all the maps together...

...

Then, after all these years, I suddenly realized that it's actually not that hard.

So, here you go, this utility does exactly that -- it loads a valve bsp file, then
finds all the connections and loads them next.

## What it loads
 - all the bsp brushed geometry, including some weird detached stuff
 - lightmaps

## What it does not load
 - models
 - material textures (they take fuckload of videomem and look like shit anyway)
 - bsp visibility info, as there's obviously no point in doing that

# Supported platforms
 - Linux
 - Mac OS X
 - Windows
 - Raspberry Pi

# Supported games
Everything with VBSP version 19 should work. Tested on Half-Life: Source and Half-Life 2.

Half-Life 2 has some issues:
 * no displacements
 * incorrect parsing of lightmap format for ver20 vbsp

# How to

1. Get an appropriate binary from bin/, or compile one if you're on linux or raspberry (see below)
2. Get a Half-Life: Source (not the vanilla one) gcf file and extract it somewhere
3. win32/osx: drag-n-drop c0a0.bsp onto application; linux/rpi: see below
4. Wait 10-30 seconds (I was too lazy to code a loader screen)
5. ?????
6. Have fun


# How to compile (linux/raspberry)
Prerequisites:
* common:
 * gcc
 * make
* Linux:
 * libSDL
* Raspberry:
 * broadcom sdk in /opt/vc (pre-installed on raspbian)
 * no libSDL, no X11 required!
 * /dev/input/event0 and 1 are expected to be a mouse and a keyboard respectively

1. git clone https://github.com/w23/OpenSource.git
2. cd OpenSource
2. git submodule init
3. git submodule update
4. make -C 3p/kapusha -j5
 - raspberry: make -C 3p/kapusha RPI=1
5. make -j5
 - raspberry: make RPI=1
6. ./OpenSource /path/to/gcf/root/hl1/ c0a0
 - raspberry: ./OpenSource /path/to/gcf/root/hl1/ c0a0 32

Other platforms (win32/osx): there are nice project files!

# Next stop: the huge world of Portal 2
