OpenSource
==========
This branch is a **work-in-progress** reimplementation of original OpenSource in bare C.
It now contains only a very simple Half-Life 2 VBSP map loader.
It it not ready for any use.

If you wish, you could check out the *old* branch for the 5 years old version that was used for Half-Life 1 merging.

It supports:
- basic face geometry
- displacements
- base[0] textures
- DXT1 textures
- Linux/X11 and Raspberry Pi (w/o X11)
- reading VPK files
- reading materials from pakfile lumps (to a degree)

Issues:
- displacement lightmap coordinates are sometimes off
- no water shaders
- no transparency
- some textures are broken
