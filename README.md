OpenSource
==========
This branch is a work-in-progress reimplementation of original OpenSource in bare C.
It now contains only a very simple Half-Life 2 VBSP map loader.

It supports:
- basic face geometry
- displacements
- base[0] textures
- DXT1 textures
- Linux/X11 and Raspberry Pi (w/o X11)

Issues:
- displacement lightmap coordinates are sometimes off
- no pakfile lump support (a lot of materials are missing)
- no water shaders
- no transparency
- DXT5 textures are broken
