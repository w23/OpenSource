#!/bin/sh

CC=clang
#CFLAGS='-fsanitize=address -fPIE -pie -O0 -g -Wall -Werror -Wextra -pedantic --std=c99 -I3p/atto'
CFLAGS='-O0 -g -Wall -Werror -Wextra -pedantic --std=c99 -I3p/atto'
LDFLAGS='-lm -lGL -lX11'

SOURCES='OpenSource.c bsp.c atlas.c filemap.c '
SOURCES+='3p/atto/src/app_linux.c 3p/atto/src/app_x11.c'

$CC $CFLAGS $SOURCES $LDFLAGS -o OpenSource
