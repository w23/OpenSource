OpenSource
==========
A utility for loading and rendering many Source VBSP maps together as a single giant mesh. It can be used to see how big the game world is, just for amusement.

## Current status
This branch is a **work-in-progress** reimplementation of original OpenSource in bare C.
It should be generally stable. However, it still has a lot of visual glitches and is not ready for any use outside development yet. See issues.

If you wish, you could check out the *old* branch for the 5 years old version that was used for Half-Life 1 merging.

## What works
- It builds and runs on Linux/X11 and Raspberry Pi (bare vc libs, w/o X11); No Windows or macOS support yet, stay tuned.
- VBSP format version 19 and 20, most of the maps from these games:
  - Half-Life: Source
  - Half-Life 2
  - Half-Life 2: Episode One
  - Half-Life 2: Episode Two
  - Portal (Portal 2 wasn't tested yet)
- Basic support for the following map features:
  - Face geometry
  - Displacements
  - Base[0] textures
  - DXT1 textures
  - Reading VPK files
  - Reading materials from pakfile lumps
