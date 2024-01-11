cmake_minimum_required(VERSION 3.10)
project(OpenSource)

if(WIN32)
	# required to set SUBSYSTEM linker flag to WINDOWS
	# because we don't want it to be CONSOLE
	set(EXE_SUBSYSTEM WIN32)
endif()

add_subdirectory(src/atto)

set(SOURCES
	src/OpenSource.c
	src/atlas.c
	src/bsp.c
	src/cache.c
	src/camera.c
	src/collection.c
	src/dxt.c
	src/filemap.c
	src/log.c
	src/material.c
	src/profiler.c
	src/render.c
	src/texture.c
	src/vmfparser.c
)

set(HEADERS
	src/ahash.h
	src/atlas.h
	src/bsp.h
	src/cache.h
	src/camera.h
	src/collection.h
	src/common.h
	src/dxt.h
	src/etcpack.h
	src/filemap.h
	src/libc.h
	src/log.h
	src/material.h
	src/mempools.h
	src/profiler.h
	src/render.h
	src/texture.h
	src/vbsp.h
	src/vmfparser.h
	src/vpk.h
	src/vtf.h
	src/zip.h
)

add_executable(OpenSource ${EXE_SUBSYSTEM} ${SOURCES} ${HEADERS})

target_link_libraries(OpenSource atto)
set_target_properties(OpenSource PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED TRUE
	C_EXTENSIONS ON)
if(MSVC)
    add_compile_definitions(OpenSource _CRT_SECURE_NO_WARNINGS)
endif()
target_include_directories(OpenSource PRIVATE src/khronos)
target_compile_options(OpenSource PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic -Werror>
)
