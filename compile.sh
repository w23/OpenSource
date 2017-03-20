#!/bin/sh
set -euo pipefail
#IFS=$'\n\t'

CFLAGS=${CFLAGS:-}
LDFLAGS=${LDFLAGS:-}

ASAN=
DEBUG=
RPI=
RPI_ROOT=${RPI_ROOT:-'/opt/raspberry-pi'}

SOURCES='OpenSource.c bsp.c atlas.c filemap.c'
WERROR='-Werror'
CFLAGS="-D_POSIX_C_SOURCE=200809L $CFLAGS"

while [ $# -gt 0 ]
do
	case "$1" in
		-D) DEBUG=1;;
		-P) RPI=1;;
		-S) ASAN=1;;
		-W) WERROR='';;
	esac
	shift
done

if [ $ASAN ]
then
	CFLAGS="-fsanitize=address $CFLAGS"
fi

if [ $DEBUG ]
then
	CFLAGS="-O0 -g $CFLAGS"
else
	CFLAGS="-O3 $CFLAGS"
fi

if [ $RPI ]
then
	RPI_TOOLCHAIN=${RPI_TOOLCHAIN:-"gcc-linaro-arm-linux-gnueabihf-raspbian-x64"}
	RPI_TOOLCHAINDIR=${RPI_TOOLCHAINDIR:-"$RPI_ROOT/raspberry-tools/arm-bcm2708/$RPI_TOOLCHAIN"}
	RPI_VCDIR=${RPI_VCDIR:-"$RPI_ROOT/raspberry-firmware/hardfp/opt/vc"}

	CC=${CC:-"$RPI_TOOLCHAINDIR/bin/arm-linux-gnueabihf-gcc"}

	CFLAGS="-std=gnu99 -Wall -Wextra $WERROR $CFLAGS"
	CFLAGS="-I$RPI_VCDIR/include -I$RPI_VCDIR/include/interface/vcos/pthreads $CFLAGS"
	CFLAGS="-I$RPI_VCDIR/include/interface/vmcs_host/linux -DATTO_PLATFORM_RPI $CFLAGS"
	LDFLAGS="-lGLESv2 -lEGL -lbcm_host -lvcos -lvchiq_arm -L$RPI_VCDIR/lib -lrt -lm $LDFLAGS"
	SOURCES+=' 3p/atto/src/app_linux.c 3p/atto/src/app_rpi.c'
else
	CC=${CC:-clang}
	CFLAGS="-std=c99 -Wall -Wextra -pedantic $WERROR $CFLAGS"
	LDFLAGS="-lm -lGL -lX11 -lXfixes $LDFLAGS"
	SOURCES+=' 3p/atto/src/app_linux.c 3p/atto/src/app_x11.c'
fi

CFLAGS="-I3p/atto -fPIE -pie $CFLAGS"

echo $CC $CFLAGS $SOURCES $LDFLAGS -o OpenSource
$CC $CFLAGS $SOURCES $LDFLAGS -o OpenSource
