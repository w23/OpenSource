CXX=g++
LD=g++
CXXFLAGS=-Wall -Werror -fno-exceptions -fno-rtti -I. -std=c++0x -I3p/kapusha
LDFLAGS=-lm

ifeq ($(DEBUG),1)
	CXXFLAGS += -g -DDEBUG=1
else
	CXXFLAGS += -Os -fomit-frame-pointer
endif

ifeq ($(RPI),1)
	CXXFLAGS += -DKAPUSHA_RPI=1 -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
	LDFLAGS += -lGLESv2 -lEGL -lbcm_host -L/opt/vc/lib
else
	CXXFLAGS += `pkg-config --cflags sdl` -march=native
	LDFLAGS += `pkg-config --libs sdl` -lGL
endif

.SUFFIXES: .cpp .o

.cpp.o: $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
