[![Build Status](https://travis-ci.org/w23/OpenSource.png)](https://travis-ci.org/w23/OpenSource) [![Build status](https://ci.appveyor.com/api/projects/status/rgu44jqi1kt2jpw9?svg=true)](https://ci.appveyor.com/project/w23/opensource)

OpenSource
==========
A utility for loading and rendering many Source VBSP maps together as a single giant mesh. It can be used to see how big the game world is, just for amusement.

## Current status
It is not production quality and is not ready for any unprofessional and unsupervised use. It still has a lot of visual and other glitches. See issues.
However, it should be generally stable. It does run on Raspberry Pi. The entire Half-Life 2 fits into < 512MiB video memory and renders ~1.5 million triangles at about 10fps.

If you wish, you could check out the *old* branch for the 5 years old version that was used for Half-Life 1 merging.

## What works
- It builds and runs on Windows, Linux/X11 and Raspberry Pi (bare vc libs, w/o X11); No macOS support yet, stay tuned.
- VBSP format version 19 and 20, most of the maps from these games:
  - Half-Life: Source
  - Half-Life 2
  - Half-Life 2: Episode One
  - Half-Life 2: Episode Two
  - Portal
  - Portal 2. Well, somewhat. Its levels are not positioned correctly, requiring a lot of manual config patching, which is not done.
- Basic support for the following map features:
  - Face geometry
  - Displacements
  - Base[0] textures
  - DXT1/3/5 textures
  - Reading VPK2 files
  - Reading materials from pakfile lumps
  - Packing textures with ETC1 on Raspberry Pi (packer is very naive and probably broken)

## Streaming (OH HOLD)
Development was done almost entirely live.

Stream links:
- [Twitch/ProvodGL](https://twitch.tv/provodgl)
- [YouTube](https://www.youtube.com/c/IvanAvdeev/live)
- [Goodgame.ru/w23](https://goodgame.ru/channel/w23/)
- [Peka2](http://peka2.tv/w23)

You can also check out [previous streams recordings](https://www.youtube.com/playlist?list=PLP0z1CQXyu5DjL_3-7lukQmKGYq2LhxKA) and [stuff planned for next streams](https://github.com/w23/OpenSource/projects/1).
