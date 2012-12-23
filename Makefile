include common.mk

SOURCES := \
	src/BSP.cpp \
	src/CloudAtlas.cpp \
	src/Entity.cpp \
	src/main_sdl.cpp \
	src/Materializer.cpp \
	src/ResRes.cpp \
	src/OpenSource.cpp

#MODULES=$(addprefix build/, $(patsubst %.cpp, %.o, $(SOURCES)))
MODULES=$(patsubst %.cpp, %.o, $(SOURCES))
DEPS=Makefile common.mk

.PHONY: clean

OpenSource: $(DEPS) $(MODULES) 3p/kapusha/libkapusha.a
	$(LD) $(LDFLAGS) $(MODULES) -L3p/kapusha -lkapusha -o OpenSource

3p/kapusha/libkapusha.a:
	make -C 3p/kapusha

clean:
	@rm -rf $(MODULES) OpenSource

deepclean: clean
	@make -C 3p/kapusha clean
