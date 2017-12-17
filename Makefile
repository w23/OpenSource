.SUFFIXES:
MAKEOPTS+=-r

MAX_MAPS ?= 99
ARGS ?= -n $(MAX_MAPS) -c hl2.cfg

CFLAGS += -Wall -Wextra -D_GNU_SOURCE -Isrc/atto -fPIE
BUILDDIR ?= build

ifeq ($(DEBUG), 1)
	CONFIG = dbg
	CFLAGS += -O0 -g
else
	CONFIG = rel
	CFLAGS += -O3
endif

ifeq ($(NOWERROR), 1)
	CONFIG := $(CONFIG)nowerror
else
	CFLAGS += -Werror
endif

ifeq ($(RASPBERRY), 1)
	PLATFORM = pi
	RPI_ROOT ?= /opt/raspberry-pi

	ifeq ($(CROSS), 1)
		RPI_TOOLCHAIN ?= gcc-linaro-arm-linux-gnueabihf-raspbian-x64
		RPI_TOOLCHAINDIR ?= $(RPI_ROOT)/raspberry-tools/arm-bcm2708/$(RPI_TOOLCHAIN)
		RPI_VCDIR ?= $(RPI_ROOT)/raspberry-firmware/hardfp/opt/vc
		CC = $(RPI_TOOLCHAINDIR)/bin/arm-linux-gnueabihf-gcc
		COMPILER = gcc
	else
		RPI_VCDIR ?= /opt/vc
		CC ?= cc
	endif

	CFLAGS += -I$(RPI_VCDIR)/include -I$(RPI_VCDIR)/include/interface/vcos/pthreads
	CFLAGS += -I$(RPI_VCDIR)/include/interface/vmcs_host/linux -DATTO_PLATFORM_RPI
	LIBS += -lGLESv2 -lEGL -lbcm_host -lvcos -lvchiq_arm -L$(RPI_VCDIR)/lib -lrt -lm

	SOURCES += \
		src/atto/src/app_linux.c \
		src/atto/src/app_rpi.c

else
	PLATFORM = desktop
	CC ?= cc
	LIBS += -lX11 -lXfixes -lGL -lm -pthread
	SOURCES += \
		src/atto/src/app_linux.c \
		src/atto/src/app_x11.c
endif

COMPILER ?= $(CC)
OBJDIR ?= $(BUILDDIR)/$(PLATFORM)-$(CONFIG)-$(COMPILER)

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) -std=gnu99 $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

EXE = $(OBJDIR)/OpenSource
all: $(EXE)
SOURCES += \
	src/OpenSource.c \
	src/bsp.c \
	src/atlas.c \
	src/filemap.c \
	src/camera.c \
	src/collection.c \
	src/vmfparser.c \
	src/material.c \
	src/texture.c \
	src/cache.c \
	src/dxt.c \
	src/render.c \
	src/profiler.c \

OBJECTS = $(SOURCES:%=$(OBJDIR)/%.o)
DEPS = $(OBJECTS:%=%.d)

-include $(DEPS)

$(EXE): $(OBJECTS)
	$(CC) $^ $(LIBS) -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(EXE)

run: $(EXE)
	$(EXE) $(ARGS)

debug: $(EXE)
	gdb --args $(EXE) $(ARGS)

.PHONY: all clean run_tool debug_tool
