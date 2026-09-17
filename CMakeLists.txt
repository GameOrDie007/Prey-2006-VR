# PreyVR -> PCVR, Windows build.
#
# This is new work rather than a port. neo/CMakeLists.txt is stock dhewm3: it
# mentions Prey zero times, its options are CORE/BASE/D3XP, and D3XP no longer
# exists in this tree. The real build spec has always been neo/mobile/Android.mk
# and Android_game.mk, and cmake/sources.cmake is generated from those two by
# tools/gen_cmake_sources.py so the file lists cannot silently drift.
#
# Two targets, mirroring their two Android modules:
#
#   PreyVR.exe        libdoom3 - engine, renderer, VR layer, GL loader
#   gamex86_64.dll    libgame  - the Prey game code and its own copy of idlib
#
# The DLL name is not a choice: idSysLocal::DLL_GetFileName builds
# "%s" CPUSTRING ".dll" and CPUSTRING is "x86_64" on this target, so
# idFileSystemLocal::FindDLL will look for exactly gamex86_64.dll beside the
# executable.
#
# Toolchain: VS2019 Build Tools (MSVC 14.29). CMake 4.x hard-errors on
# neo/CMakeLists.txt's `cmake_minimum_required(VERSION 2.6)`; this file does not
# include that one, but 2019's bundled CMake 3.20 is the tested combination.

cmake_minimum_required(VERSION 3.16)
project(PreyVR CXX C)

set(PREYVR_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(PREYVR_JNI ${PREYVR_ROOT}/app/src/main/jni)
set(PREYVR_NEO ${PREYVR_JNI}/d3es-multithread-master/neo)

if(NOT MSVC)
    message(FATAL_ERROR "This build is Windows/MSVC only. The Android build is unchanged; use gradle for that.")
endif()

# Prebuilt dependencies. dhewm3-libs is the bundle dhewm3 itself ships for
# Windows, so its SDL2/OpenAL/ogg/vorbis/jpeg/zlib are the versions this engine
# expects. Not vendored here - binaries are never committed - so point this at
# a local checkout.
set(PREYVR_DEPS "" CACHE PATH
    "dhewm3-libs x86_64-w64-mingw32 directory (containing include/, lib/, bin/)")

if(NOT PREYVR_DEPS OR NOT EXISTS "${PREYVR_DEPS}/include")
    message(FATAL_ERROR
        "Set PREYVR_DEPS to a dhewm3-libs x86_64-w64-mingw32 directory.\n"
        "  cmake -DPREYVR_DEPS=.../dhewm3-libs-master/x86_64-w64-mingw32 ...")
endif()

# The OpenXR loader import library. The VR layer calls xrCreateInstance and
# friends directly rather than through GetProcAddress, so it has to link.
set(PREYVR_OPENXR_LIB "" CACHE FILEPATH "openxr_loader.lib built from KhronosGroup/OpenXR-SDK")

if(NOT PREYVR_OPENXR_LIB OR NOT EXISTS "${PREYVR_OPENXR_LIB}")
    message(FATAL_ERROR
        "Set PREYVR_OPENXR_LIB to an openxr_loader.lib.\n"
        "  cmake -DPREYVR_OPENXR_LIB=.../openxr_loader.lib ...")
endif()

# Warning level. The previous three ports all had real bugs sitting in warnings
# nobody read, so the dangerous classes are promoted to errors-worth-reading
# rather than left at MSVC's defaults:
#
#   C4172  returning address of local variable      (-Wreturn-local-addr)
#   C4700  uninitialized local variable used        (-Wuninitialized)
#   C4701  potentially uninitialized local used
#   C4703  potentially uninitialized local pointer
#   C4715  not all control paths return a value
#   C4033  function must return a value
#
# The noisy conversion warnings stay off: this is 2004 code and C4244/C4267
# would bury everything else.
add_compile_options(/W3 /w14172 /w14700 /w14701 /w14703 /w14715 /w14033
                    /wd4244 /wd4267 /wd4305 /wd4996 /wd4018 /wd4312)

# Debug info in Release. There is no debugger installed on this machine, so the
# crash handler in sys/win32/win_pcvr.cpp walks the stack itself with dbghelp -
# which needs a PDB beside the binary to name anything. /Zi does not change code
# generation, and /OPT:REF is restated because /DEBUG otherwise turns off the
# optimisations Release would have had.
#
# /OPT:ICF is deliberately NOT set. It folds functions with identical bodies
# onto one address, which makes a crash stack name whichever of them the
# symbol table happened to keep - destructor thunks fold constantly. A stack
# that might be lying is worth less than the handful of kilobytes it saves.
add_compile_options($<$<CONFIG:Release>:/Zi>)
add_link_options($<$<CONFIG:Release>:/DEBUG>
                 $<$<CONFIG:Release>:/OPT:REF>
                 )   # deliberately no /OPT:ICF - see below

include(${PREYVR_ROOT}/cmake/sources.cmake)

# Their Android defines, minus the Android-only ones. ENGINE_NAME, NO_LIGHT and
# USE_GLES2 are all theirs and are carried across unchanged - see PROGRESS.md.
set(PREYVR_COMMON_DEFS
    _HUMANHEAD
    HUMANHEAD
    _PREY
    __DOOM_DLL__
    IOAPI_NO_64
    _K_CLANG                # glKarin's; his build defines it, so this one does
    KHRONOS_STATIC          # GL_APICALL must not be dllimport, see jni/GLES
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES       # M_PI, which MSVC hides by default
)

set(PREYVR_INCLUDES
    ${PREYVR_NEO}
    ${PREYVR_NEO}/mobile
    ${PREYVR_JNI}
    ${PREYVR_JNI}/OpenXR
    ${PREYVR_JNI}/GLES
    ${PREYVR_JNI}/Doom3Quest
    ${PREYVR_DEPS}/include
    ${PREYVR_DEPS}/include/SDL2
)

# ---------------------------------------------------------------- engine

add_executable(PreyVR ${PREYVR_ENGINE_SOURCES})

target_compile_definitions(PreyVR PRIVATE
    ${PREYVR_COMMON_DEFS}
    USE_GLES2
    D3ES
    ENGINE_NAME="d3es"
    NO_LIGHT
    # Turns the VR layer's own OXR()/GL() error checking on in a Release build.
    # Their guards are _DEBUG only, so on this port every OpenXR and GL call in
    # jni/Doom3Quest was unchecked and a silent failure was indistinguishable
    # from success. See the note in VrBase.h.
    PREYVR_XR_CHECKS
)
target_include_directories(PreyVR PRIVATE ${PREYVR_INCLUDES})

# The VR layer stays C, which is what it is on Android and what the rest of the
# engine assumes: sys/events.cpp defines Sys_AddKeyEvent and friends as
# `extern "C"`, sys/glimp.cpp declares Doom3Quest_GetScreenRes the same way, and
# VrCommon.h wraps its declarations in `extern "C"` under __cplusplus. That whole
# pattern exists because these are C translation units. Compiling them as C++
# breaks the contract in both directions and the link fails.
#
# lvonasek's untested _WIN32 branch in VrInput.c uses static_cast, which is what
# first suggested C++ - but that branch had never been linked. See PROGRESS.md.

target_link_libraries(PreyVR PRIVATE
    ${PREYVR_DEPS}/lib/SDL2.lib
    ${PREYVR_DEPS}/lib/SDL2main.lib
    ${PREYVR_DEPS}/lib/OpenAL32.lib
    ${PREYVR_DEPS}/lib/ogg.lib
    ${PREYVR_DEPS}/lib/vorbis.lib
    ${PREYVR_DEPS}/lib/vorbisfile.lib
    ${PREYVR_DEPS}/lib/jpeg.lib
    ${PREYVR_DEPS}/lib/zlib1.lib
    ${PREYVR_OPENXR_LIB}
    opengl32
    ws2_32
    winmm
    iphlpapi
    dbghelp
)

# Console subsystem: the engine prints a great deal before a window exists, and
# a GUI-subsystem binary has no stdout, which cost the Raze port real time.
set_target_properties(PreyVR PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)

# ------------------------------------------------------------------ game

add_library(game SHARED ${PREYVR_GAME_SOURCES})

target_compile_definitions(game PRIVATE
    ${PREYVR_COMMON_DEFS}
    GAME_DLL
)
target_include_directories(game PRIVATE
    ${PREYVR_INCLUDES}
    ${PREYVR_NEO}/game
)

# FindDLL looks for "game" CPUSTRING ".dll" beside the executable.
#
# /EXPORT:GetGameAPI is not optional. game/Game_local.cpp declares the one entry
# point the engine looks for as a bare `extern "C" gameExport_t *GetGameAPI(...)`
# and relies on `#pragma GCC visibility push(default)` around it to export it -
# which MSVC ignores, so the DLL built here had no exports at all and the engine
# loaded it and then reported "couldn't find game DLL API". Their platform.h does
# define ID_GAME_API as __declspec(dllexport) under _MSC_VER + GAME_DLL, but it
# was never applied to this function, because nobody had built the game module on
# Windows. Exporting from the link keeps their source untouched.
set_target_properties(game PROPERTIES
    OUTPUT_NAME "gamex86_64"
    PREFIX ""
    LINK_FLAGS "/EXPORT:GetGameAPI"
)

# ------------------------------------------------------------------ notes

message(STATUS "PreyVR: ${PREYVR_ENGINE_SOURCES_COUNT} engine sources, game -> gamex86_64.dll")
message(STATUS "PreyVR: deps   ${PREYVR_DEPS}")
message(STATUS "PreyVR: openxr ${PREYVR_OPENXR_LIB}")
message(STATUS "PreyVR: runtime DLLs (SDL2, OpenAL32, ogg/vorbis, jpeg, zlib, openxr_loader) must sit beside the exe; none are committed")
