.SUFFIXES:
.DEFAULT:

CC ?= cc
CFLAGS += -Wall -Wextra -pedantic -Iatto -O0 -g -D_GNU_SOURCE -Isrc/atto -fPIE -pie
LIBS = -lX11 -lXfixes -lGL -lm -pthread
OBJDIR ?= .obj
MAP ?= d1_trainstation_01
VPKDIR ?= ~/.local/share/Steam/steamapps/common/Half-Life\ 2/hl2
ARGS ?= -p $(VPKDIR)/hl2_textures_dir.vpk -p $(VPKDIR)/hl2_misc_dir.vpk -p $(VPKDIR)/hl2_pak_dir.vpk -d $(VPKDIR)

DEPFLAGS = -MMD -MP
COMPILE.c = $(CC) -std=gnu99 $(CFLAGS) $(DEPFLAGS) -MT $@ -MF $@.d

$(OBJDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(COMPILE.c) -c $< -o $@

TOOL_EXE = $(OBJDIR)/OpenSource
all: $(TOOL_EXE)
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
	$(TOOL_EXE) $(ARGS) $(MAP)

debug: $(TOOL_EXE)
	gdb --args $(TOOL_EXE) $(ARGS) $(MAP)

.PHONY: all clean run_tool debug_tool
