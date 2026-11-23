cmake_minimum_required(VERSION 3.20)
project(moonwalker_recomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(musashi STATIC
    third_party/musashi/m68kcpu.c
    third_party/musashi/m68kops.c
    third_party/musashi/softfloat/softfloat.c
)
target_include_directories(musashi PUBLIC src third_party/musashi)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/generated_game.c")
    set(MW_RECOMP_SOURCE src/generated_game.c)
else()
    set(MW_RECOMP_SOURCE src/generated_game_empty.c)
endif()

add_library(mwsystem18 STATIC
    src/s18.c
    src/display.c
    src/fd1094.c
    src/m68k_glue.c
    src/rom_loader.c
    src/video.c
    src/recomp.c
    ${MW_RECOMP_SOURCE}
    src/sound.cpp
    src/mcu.c
    third_party/emu8051/core.c
    third_party/emu8051/opcodes.c
    third_party/emu8051/disasm.c
    third_party/z80.c
    third_party/ymfm/ymfm_opn.cpp
    third_party/ymfm/ymfm_adpcm.cpp
    third_party/ymfm/ymfm_ssg.cpp
)
target_include_directories(mwsystem18 PUBLIC src third_party third_party/ymfm third_party/emu8051)
target_link_libraries(mwsystem18 PUBLIC musashi)
target_include_directories(mwsystem18 PUBLIC src)

add_executable(mwalk_headless src/main_headless.c)
target_link_libraries(mwalk_headless PRIVATE mwsystem18)
set_target_properties(mwalk_headless PROPERTIES LINKER_LANGUAGE CXX)

if(MSVC)
    target_compile_options(mwsystem18 PRIVATE /W3)
else()
    target_compile_options(mwsystem18 PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_options(mwalk_headless PRIVATE -Wall -Wextra)
endif()

option(MW_ENABLE_TRACE "Enable the 68000 instruction trace hook" OFF)
if(MW_ENABLE_TRACE)
    target_compile_definitions(musashi PRIVATE MW_TRACE)
    target_compile_definitions(mwsystem18 PRIVATE MW_TRACE)
endif()

option(MW_BUILD_SDL "Build the SDL2 front end" ON)
if(MW_BUILD_SDL)
    if(WIN32 AND NOT SDL2_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib/cmake/SDL2")
        set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib/cmake/SDL2")
    endif()
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 sdl2)
        endif()
    endif()

    if(SDL2_FOUND)
        add_library(imgui STATIC
            third_party/imgui/imgui.cpp
            third_party/imgui/imgui_draw.cpp
            third_party/imgui/imgui_tables.cpp
            third_party/imgui/imgui_widgets.cpp
            third_party/imgui/backends/imgui_impl_sdl2.cpp
            third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
        )
        target_include_directories(imgui PUBLIC
            third_party/imgui
            third_party/imgui/backends
        )
        target_compile_features(imgui PUBLIC cxx_std_17)
        if(TARGET SDL2::SDL2)
            target_link_libraries(imgui PUBLIC SDL2::SDL2)
            set(MW_SDL_LINK_LIBS SDL2::SDL2main SDL2::SDL2)
        else()
            target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
            target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES})
            set(MW_SDL_LINK_LIBS ${SDL2_LIBRARIES})
        endif()

        add_library(mwui STATIC
            src/settings.c
            src/ui.cpp
        )
        target_include_directories(mwui PUBLIC src PRIVATE third_party/stb)
        target_link_libraries(mwui PUBLIC imgui)
        target_compile_features(mwui PUBLIC cxx_std_17)

        add_executable(mwalk
            src/main_sdl.c
            src/input.c
            src/host_port.c
            src/zip_loader.c
            third_party/miniz/miniz.c
            third_party/miniz/miniz_tdef.c
            third_party/miniz/miniz_tinfl.c
            third_party/miniz/miniz_zip.c
        )
        target_include_directories(mwalk PRIVATE third_party/miniz)
        target_link_libraries(mwalk PRIVATE mwsystem18 mwui ${MW_SDL_LINK_LIBS})
        set_target_properties(mwalk PROPERTIES LINKER_LANGUAGE CXX)

        add_executable(mwalk_launcher src/main_launcher.c src/input.c src/host_port.c
            src/launcher_music.c)
        target_link_libraries(mwalk_launcher PRIVATE mwui ${MW_SDL_LINK_LIBS})
        set_target_properties(mwalk_launcher PROPERTIES LINKER_LANGUAGE CXX)
        if(WIN32)
            target_link_libraries(mwalk_launcher PRIVATE comdlg32 dwmapi)
            target_link_libraries(mwalk PRIVATE dwmapi)
        endif()

        if(WIN32)
            set_target_properties(mwalk mwalk_launcher PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()

        if(WIN32 AND SDL2_DIR)
            add_custom_command(TARGET mwalk POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DIR}/../../../bin/SDL2.dll" "$<TARGET_FILE_DIR:mwalk>")
            add_custom_command(TARGET mwalk_launcher POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${SDL2_DIR}/../../../bin/SDL2.dll" "$<TARGET_FILE_DIR:mwalk_launcher>")
        endif()

        add_custom_command(TARGET mwalk POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:mwalk>/assets")
        add_custom_command(TARGET mwalk_launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:mwalk_launcher>/assets")
    else()
        message(WARNING "SDL2 not found; the launcher and mwalk front end will not be built")
    endif()
endif()
