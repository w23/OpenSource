include common.mk

SOURCES := \
	OpenSource/BSP.cpp \
	OpenSource_mini/main.cpp \
	OpenSource/OpenSource.cpp \

#MODULES=$(addprefix build/, $(patsubst %.cpp, %.o, $(SOURCES)))
MODULES=$(patsubst %.cpp, %.o, $(SOURCES))
DEPS=Makefile common.mk

.PHONY: clean

OpenSource_: $(DEPS) $(MODULES) Kapusha/libkapusha.a
	$(LD) $(LDFLAGS) $(MODULES) -LKapusha -lkapusha -o OpenSource_

Kapusha/libkapusha.a:
	make -C Kapusha

clean:
	@rm -rf $(MODULES) OpenSource_
