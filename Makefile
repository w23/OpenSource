include common.mk

SOURCES := \
	OpenSource/BSP.cpp \
	OpenSource/CloudAtlas.cpp \
	OpenSource/Entity.cpp \
	OpenSource/main_sdl.cpp \
	OpenSource/Materializer.cpp \
	OpenSource/ResRes.cpp \
	OpenSource/VTF.cpp \
	OpenSource/OpenSource.cpp

#MODULES=$(addprefix build/, $(patsubst %.cpp, %.o, $(SOURCES)))
MODULES=$(patsubst %.cpp, %.o, $(SOURCES))
DEPS=Makefile common.mk

.PHONY: clean

OpenSource_: $(DEPS) $(MODULES) Kapusha/libkapusha.a
	$(LD) $(LDFLAGS) $(MODULES) -LKapusha -lkapusha -o OpenSource_sdl

Kapusha/libkapusha.a:
	make -C kapusha

clean:
	@rm -rf $(MODULES) OpenSource_sdl
