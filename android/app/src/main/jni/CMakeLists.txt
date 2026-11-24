# Moon Dancer - Android native build (libmain.so).
#
# SDLActivity loads libSDL2.so and then libmain.so, and calls SDL_main from
# libmain.so on the SDL thread.  src/main_launcher.c provides that SDL_main
# (SDL_main.h redefines main() on Android); src/main_sdl.c provides
# mw_game_main(), which the launcher calls in-process.  Both entries live in
# this one shared library on purpose.
#
# SDL2 is built from source: run `python tools/fetch_sdl2_source.py` once to
# populate third_party/SDL2 (gitignored).

cmake_minimum_required(VERSION 3.22.1)

project(moonwalker_android C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# libmain.so links our static libraries; they must be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Native optimization is separate from APK debuggability.  app/build.gradle
# pins -DCMAKE_BUILD_TYPE=Release for that reason; this guard is the safety net
# for a direct NDK configure (the documented -DSDL2_SOURCE=<path> escape hatch),
# where a Debug build type would otherwise produce an -O0 emulator core that
# cannot hold 60 fps on ARM.  add_compile_options() appends after the toolchain
# flags, so -O3 wins over the toolchain's -O0.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build type: forcing -O3 for the emulator core")
    add_compile_options(-O3)
endif()

get_filename_component(MW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
# CACHE so the FATAL_ERROR hint below actually works: a plain set() would
# shadow any -DSDL2_SOURCE=<path> passed on the configure command line.
set(SDL2_SOURCE "${MW_ROOT}/third_party/SDL2" CACHE PATH
    "SDL2 source tree (override with -DSDL2_SOURCE=<path>)")

if(NOT EXISTS "${SDL2_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL2 source tree not found at ${SDL2_SOURCE}.\n"
        "Run:  python tools/fetch_sdl2_source.py\n"
        "(or pass -DSDL2_SOURCE=<path> when configuring)")
endif()

add_subdirectory("${SDL2_SOURCE}" SDL2)

# ---------------------------------------------------------------------------
# musashi (68000 core)
# ---------------------------------------------------------------------------
add_library(musashi STATIC
    "${MW_ROOT}/third_party/musashi/m68kcpu.c"
    "${MW_ROOT}/third_party/musashi/m68kops.c"
    "${MW_ROOT}/third_party/musashi/softfloat/softfloat.c"
)
target_include_directories(musashi PUBLIC
    "${MW_ROOT}/src"
    "${MW_ROOT}/third_party/musashi"
)

# ---------------------------------------------------------------------------
# mwsystem18 (System 18 emulation + recompiled game)
# KEEP IN SYNC with add_library(mwsystem18 STATIC ...) in the root
# CMakeLists.txt; this list is maintained by hand for the Android build.
# ---------------------------------------------------------------------------
if(EXISTS "${MW_ROOT}/src/generated_game.c")
    set(MW_RECOMP_SOURCE "${MW_ROOT}/src/generated_game.c")
else()
    set(MW_RECOMP_SOURCE "${MW_ROOT}/src/generated_game_empty.c")
endif()

add_library(mwsystem18 STATIC
    "${MW_ROOT}/src/s18.c"
    "${MW_ROOT}/src/display.c"
    "${MW_ROOT}/src/fd1094.c"
    "${MW_ROOT}/src/m68k_glue.c"
    "${MW_ROOT}/src/rom_loader.c"
    "${MW_ROOT}/src/video.c"
    "${MW_ROOT}/src/recomp.c"
    "${MW_RECOMP_SOURCE}"
    "${MW_ROOT}/src/sound.cpp"
    "${MW_ROOT}/src/mcu.c"
    "${MW_ROOT}/third_party/emu8051/core.c"
    "${MW_ROOT}/third_party/emu8051/opcodes.c"
    "${MW_ROOT}/third_party/emu8051/disasm.c"
    "${MW_ROOT}/third_party/z80.c"
    "${MW_ROOT}/third_party/ymfm/ymfm_opn.cpp"
    "${MW_ROOT}/third_party/ymfm/ymfm_adpcm.cpp"
    "${MW_ROOT}/third_party/ymfm/ymfm_ssg.cpp"
)
target_include_directories(mwsystem18 PUBLIC
    "${MW_ROOT}/src"
    "${MW_ROOT}/third_party"
    "${MW_ROOT}/third_party/ymfm"
    "${MW_ROOT}/third_party/emu8051"
)
target_link_libraries(mwsystem18 PUBLIC musashi)

if(NOT MSVC)
    target_compile_options(mwsystem18 PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# imgui + mwui (shared launcher / game overlay UI)
# KEEP IN SYNC with the imgui block in the root CMakeLists.txt.
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    "${MW_ROOT}/third_party/imgui/imgui.cpp"
    "${MW_ROOT}/third_party/imgui/imgui_draw.cpp"
    "${MW_ROOT}/third_party/imgui/imgui_tables.cpp"
    "${MW_ROOT}/third_party/imgui/imgui_widgets.cpp"
    "${MW_ROOT}/third_party/imgui/backends/imgui_impl_sdl2.cpp"
    "${MW_ROOT}/third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp"
)
target_include_directories(imgui PUBLIC
    "${MW_ROOT}/third_party/imgui"
    "${MW_ROOT}/third_party/imgui/backends"
)
target_compile_features(imgui PUBLIC cxx_std_17)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

add_library(mwui STATIC
    "${MW_ROOT}/src/settings.c"
    "${MW_ROOT}/src/ui.cpp"
)
target_include_directories(mwui PUBLIC "${MW_ROOT}/src" PRIVATE "${MW_ROOT}/third_party/stb")
target_link_libraries(mwui PUBLIC imgui)
target_compile_features(mwui PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# libmain.so: launcher (SDL_main) + game (mw_game_main) in one library.
# ---------------------------------------------------------------------------
add_library(main SHARED
    "${MW_ROOT}/src/main_launcher.c"
    "${MW_ROOT}/src/main_sdl.c"
    "${MW_ROOT}/src/input.c"
    "${MW_ROOT}/src/touch_controls.c"
    "${MW_ROOT}/src/host_port.c"
    "${MW_ROOT}/src/launcher_music.c"
    "${MW_ROOT}/src/zip_loader.c"
    # JNI bridge: SAF ROM picker (JNI_OnLoad + nativeRomImported).  Lives here
    # rather than in src/ because it is Android-only and needs <jni.h>.
    "${CMAKE_CURRENT_SOURCE_DIR}/jni_bridge.c"
    "${MW_ROOT}/third_party/miniz/miniz.c"
    "${MW_ROOT}/third_party/miniz/miniz_tdef.c"
    "${MW_ROOT}/third_party/miniz/miniz_tinfl.c"
    "${MW_ROOT}/third_party/miniz/miniz_zip.c"
)
target_include_directories(main PRIVATE
    "${MW_ROOT}/src"
    "${MW_ROOT}/third_party/miniz"
)
target_link_libraries(main PRIVATE mwsystem18 mwui SDL2::SDL2)
set_target_properties(main PROPERTIES LINKER_LANGUAGE CXX)
