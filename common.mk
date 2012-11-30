CXX=g++
LD=g++
CXXFLAGS=-Wall -Werror -fno-exceptions -fno-rtti `pkg-config --cflags sdl` -I. -std=c++0x -g -DDEBUG=1 -IKapusha
LDFLAGS=`pkg-config --libs sdl` -lGL -lm

.SUFFIXES: .cpp .o

.cpp.o: $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
