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

What it loads:
 - all the bsp brushed geometry, including some weird detached stuff
 - lightmaps

What it does not load:
 - models
 - material textures (they take fuckload of videomem and look like shit anyway)
 - bsp visibility info, as there's obviously no point in doing that

How to:
1) Get an appropriate binary from bin/
2) Get a Half-Life: Source (not the vanilla one) gcf file and extract it somewhere
3) win32/osx: drag-n-drop c0a0.bsp onto application; linux/rpi: ???
4) Wait 10-30 seconds (I was too lazy to code a loader screen)
5) ?????
6) Have fun

Supported platforms:
 - Linux
 - Mac OS X
 - Windows
 - Raspberry Pi

Supported games:
 - Everything with VBSP version 19 should work, tested on Half-Life: Source, Half-Life 2 
(except there are no support for displacements, and ver20 levels look weird -- something is wrong with lightmaps format) 

Next stop: the huge world of Portal 2
