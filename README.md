[![Github build status](https://github.com/w23/OpenSource/actions/workflows/build.yml/badge.svg)](https://github.com/w23/OpenSource/actions/workflows/build.yml) [![Build Status](https://travis-ci.org/w23/OpenSource.png)](https://travis-ci.org/w23/OpenSource) [![Build status](https://ci.appveyor.com/api/projects/status/rgu44jqi1kt2jpw9?svg=true)](https://ci.appveyor.com/project/w23/opensource)

OpenSource
==========
A utility for loading and rendering many Source VBSP maps together as a single giant mesh. It can be used to see how big the game world is, just for amusement.

## Current status
It is not production quality and is not ready for any professional and/or unsupervised use. It still has a lot of visual and other glitches. See issues.
However, it should be generally stable. It does run on Raspberry Pi. The entire Half-Life 2 fits into < 512MiB video memory and renders ~1.5 million triangles at about 10fps.

If you wish, you could check out the *old* branch for a version from 2012 that was used for this video of entire Half-Life 1 world: https://www.youtube.com/watch?v=-SaRdQdW-Ik

## What works
- It builds and runs on Windows, Linux/X11 and Raspberry Pi (bare VideoCore libs, w/o X11); No macOS support yet, stay tuned.
- VBSP format version 19 and 20, most of the maps from these games:
  - Half-Life: Source
  - Half-Life 2
  - Half-Life 2: Episode One
  - Half-Life 2: Episode Two
  - Portal
  - Portal 2. Well, somewhat. Its levels are not positioned correctly, requiring a lot of manual config patching, which has not been done.
- Basic support for the following map features:
  - Face geometry
  - Displacements
  - Base[0] textures
  - DXT1/3/5 textures
  - Reading VPK2 files
  - Reading materials from pakfile lumps
  - Packing textures with ETC1 on Raspberry Pi (packer is very naive and probably broken)

## Building

Requires CMake. Something like this:
```
cmake -E make_directory build
cmake . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Getting binaries
If you don't want to build it yourself, you can find some pre-built Windows binaries in [Releases](https://github.com/w23/OpenSource/releases)

## Running
Basically you point OpenSource binary to a cfg file. It should automatically load game resources from default Steam install directory.
There is a couple of pre-made cfg files for a few games. You can find them in pre-built archives, or in misc/ directory of this repo.

Preconfigured games are:
- `hl1.cfg`: Half-Life 1. For this you need to install "Half-Life: Source" game from Steam. Regular Half-Life won't work.
- `hl2.cfg`: Half-Life 2.
- `hl2eps.cfg`: Half-Life 2, including Episode One, and Episode Two. You need them to be installed from Steam.

Additional options:
- `-s` -- specify Steam install directory if it's different from the default one.
- `-m` -- add additional map to load at origin
- `-p` -- add a custom VPK file to load resources from
- `-d` -- add a custom directory to load resources from
- `-n` -- specify a limit to number of maps to load

Notes:
- Arguments order matters: options only apply to what follows them. E.g. `./OpenSource hl1.cfg -s <custom_steam_path>` will not use `<custom_steam_path>` for loading resources for `hl1.cfg`, but `./OpenSource -s <custom_steam_path> hl1.cfg` will.
- cfg files are not strictly necessary, it is possible to load maps only using arguments. However, landmark patching functionality is only supported via cfg files.

## Streaming (ON HOLD)
Development was done almost entirely live.

Stream links:
- [Twitch/ProvodGL](https://twitch.tv/provodgl)
- [YouTube](https://www.youtube.com/c/IvanAvdeev/live)

You can also check out [previous streams recordings](https://www.youtube.com/playlist?list=PLP0z1CQXyu5DjL_3-7lukQmKGYq2LhxKA) and [stuff planned for next streams](https://github.com/w23/OpenSource/projects/1).
