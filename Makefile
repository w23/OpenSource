.SUFFIXES:
.DEFAULT:

CC ?= cc
CFLAGS += -Wall -Wextra -pedantic -Iatto -O0 -g -D_GNU_SOURCE -Isrc/atto -fPIE -pie
LIBS = -lX11 -lXfixes -lGL -lm -pthread
OBJDIR ?= .obj
MAP ?= d1_trainstation_01

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) -std=gnu99 $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

all: run

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

TOOL_EXE = $(OBJDIR)/OpenSource
TOOL_SRCS = \
	src/atto/src/app_linux.c \
	src/atto/src/app_x11.c \
	src/OpenSource.c \
	src/bsp.c \
	src/atlas.c \
	src/filemap.c \
	src/collection.c \
	src/vmfparser.c \
	src/material.c \
	src/texture.c \
	src/cache.c \
	src/dxt.c \
	src/render.c
TOOL_OBJS = $(TOOL_SRCS:%=$(OBJDIR)/%.o)
TOOL_DEPS = $(TOOL_OBJS:%=%.d)

-include $(TOOL_DEPS)

$(TOOL_EXE): $(TOOL_OBJS)
	$(CXX) $(LIBS) $^ -o $@

clean:
	rm -f $(TOOL_OBJS) $(TOOL_DEPS) $(TOOL_EXE)

run: $(TOOL_EXE)
	$(TOOL_EXE) -d mnt/hl2_misc -d mnt/hl2_pak -d mnt/hl2_textures -d ~/.local/share/Steam/steamapps/common/Half-Life\ 2/hl2 $(MAP)

debug: $(TOOL_EXE)
	gdb --args $(TOOL_EXE) -d mnt/hl2_misc -d mnt/hl2_pak -d mnt/hl2_textures -d ~/.local/share/Steam/steamapps/common/Half-Life\ 2/hl2 $(MAP)

.PHONY: all clean run_tool debug_tool
