cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)

# -----------------------------------------------------------------------------
# The Playdate port - a standalone CMake project in its own right, same shape
# as src/sdl3/ and src/sdl2/'s own CMakeLists.txt but modeled directly on the
# sibling Tinyjoypad_SDL project's own already-working
# src/playdate/CMakeLists.txt (see that file's own header comment for the
# full "why" - the official SDK's own playdate_game.cmake expects specific
# variable names, not our own three-port convention), copied here nearly
# verbatim with this project's own game name/paths substituted.
#
# Two build outputs, matching that sibling project exactly:
#  - Simulator (default - "cd src/playdate && cmake -B build -G Ninja &&
#    cmake --build build" builds a pdex.dll straight into Source/, loadable
#    by PlaydateSimulator.exe): add_library(SHARED).
#  - Device (`-DTOOLCHAIN=armgcc`, needs the SDK's own arm-none-eabi-gcc
#    toolchain): add_executable(), cross-compiled to pdex.elf.
# Either way, playdate_game.cmake's own POST_BUILD step invokes `pdc` to pack
# Source/ into GamebuinoClassic.pdx right there in this directory.
# -----------------------------------------------------------------------------

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})

if (NOT ${ENVSDK} STREQUAL "")
	file(TO_CMAKE_PATH ${ENVSDK} SDK)
else()
	# No PLAYDATE_SDK_PATH set - fall back to this machine's own known SDK
	# install location, matching the sibling Tinyjoypad_SDL project's own
	# identical fallback exactly (confirmed present on this machine before
	# relying on it).
	set(SDK "C:/playdate/PlaydateSDK")
endif()

if (NOT EXISTS ${SDK})
	message(FATAL_ERROR "SDK Path not found; set ENV value PLAYDATE_SDK_PATH")
	return()
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)

set(PLAYDATE_GAME_NAME GamebuinoClassic)
set(PLAYDATE_GAME_DEVICE GamebuinoClassic_DEVICE)

project(${PLAYDATE_GAME_NAME} C ASM)

# Every shim/menu/games/*.c file is its own separate translation unit - globbed
# directly here alongside this port's own main.c, exactly the way src/sdl3/
# and src/sdl2/'s own CMakeLists.txt already do. ../gameworld is untouched by
# this port - main.c writes a brand-new Playdate-native menu instead of
# reusing gameworld/menu.c's own BIOS-font one, but still links every one of
# its other files (all 99 games, both shims, menuGameList.c's addGames())
# completely unmodified.
file(GLOB GameworldSources CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/../gameworld/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/*.h"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.c"
        "${PROJECT_SOURCE_DIR}/../gameworld/games/*.h"
)

if (TOOLCHAIN STREQUAL "armgcc")
	add_executable(${PLAYDATE_GAME_DEVICE} ${PROJECT_SOURCE_DIR}/main.c ${GameworldSources})
	target_include_directories(${PLAYDATE_GAME_DEVICE} PRIVATE ${PROJECT_SOURCE_DIR}/../gameworld)
	set(PLAYDATE_GAME_TARGET ${PLAYDATE_GAME_DEVICE})
else()
	add_library(${PLAYDATE_GAME_NAME} SHARED ${PROJECT_SOURCE_DIR}/main.c ${GameworldSources})
	target_include_directories(${PLAYDATE_GAME_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../gameworld)
	set(PLAYDATE_GAME_TARGET ${PLAYDATE_GAME_NAME})

	# playdate.cmake's own MSVC branch defines _WINDLL=1 automatically (main.c's
	# `#ifdef _WINDLL __declspec(dllexport) int eventHandler(...)` needs this to
	# export the symbol the Simulator loads by name) but its non-MSVC branch
	# does not - this repo's own toolchain is MinGW-w64/GCC (matching the
	# sibling Tinyjoypad_SDL project's own identical Playdate build), not MSVC,
	# so this needs doing by hand here, same as that project's own fix.
	if (MINGW)
		target_compile_definitions(${PLAYDATE_GAME_NAME} PRIVATE _WINDLL)
	endif()
endif ()

# ../gameworld/menu.h uses `bool` without its own #include <stdbool.h> (it
# relies on getting pulled in transitively by whichever TU includes menu.h
# first - true today only by accident: machineDependent.h's own real
# #include <stdbool.h> happens to run before menu.h needs it in every game
# file's own include order, but menu.c's OWN include order is
# avrCompat.h/menu.h/machineDependent.h - menu.h first). This never actually
# broke the SDL2/SDL3 ports because neither of THEIR OWN CMakeLists.txt pins
# a C standard at all, and the GCC installed on this machine defaults new
# enough (C23) for `bool` to already be a real language keyword needing no
# header - masking the real ordering bug rather than fixing it. This port's
# own CMAKE_C_STANDARD 11 pin (matching the sibling Tinyjoypad_SDL project's
# own identical pin) exposes it for real: under C11, `bool` is only ever the
# <stdbool.h> macro, and menu.h's own use of it fails to compile in
# menu.c's own translation unit specifically. Fixed here, NOT by editing
# ../gameworld/menu.h (this port must not modify shared game-world code) -
# GCC's own `-include` flag force-includes a header before the very first
# line of every compiled source file in this target, the same effect as
# menu.h having its own #include <stdbool.h> would have had, without
# touching that file at all. Harmless on every other TU here too (a
# standard, idempotent, include-guarded header).
target_compile_options(${PLAYDATE_GAME_TARGET} PRIVATE -include stdbool.h)

include(${SDK}/C_API/buildsupport/playdate_game.cmake)
