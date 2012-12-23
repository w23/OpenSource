CXX=g++
LD=g++
CXXFLAGS=-Wall -Werror -fno-exceptions -fno-rtti `pkg-config --cflags sdl` -I. -std=c++0x -I3p/kapusha -march=native
LDFLAGS=`pkg-config --libs sdl` -lGL -lm

ifeq ($(DEBUG),1)
CXXFLAGS += -g -DDEBUG=1
else
CXXFLAGS += -Os -fomit-frame-pointer
endif

.SUFFIXES: .cpp .o

.cpp.o: $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
