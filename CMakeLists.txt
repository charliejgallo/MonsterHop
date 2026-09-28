cmake_minimum_required(VERSION 3.20)
project(monsterhop C)

# Monster Hop for macOS and Windows. The game is the watch's code, from the
# AmoledOS repository (the `amoledos` submodule, or any checkout with
# -DAMOLEDOS_DIR=...): apps/monsterhop/main and the art pack. What lives here
# is the host: the window, the controls, and the part of the watch's HAL the
# game calls.
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# SDL2 and LVGL come with FetchContent. A local LVGL (the simulator's, say)
# saves the download: -DFETCHCONTENT_SOURCE_DIR_LVGL=<path>.

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(AMOLEDOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/amoledos" CACHE PATH "an AmoledOS checkout")
set(MH_GAME_DIR "${AMOLEDOS_DIR}/apps/monsterhop")
if(NOT EXISTS "${MH_GAME_DIR}/main/monsterhop.c")
    message(FATAL_ERROR "no game in ${MH_GAME_DIR}: git submodule update --init, or -DAMOLEDOS_DIR=")
endif()
set(MH_VERSION "0.1.0" CACHE STRING "the version the builds carry")

include(FetchContent)

# ---- SDL2, static: nothing to install next to the game ----
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)

# ---- LVGL 9.5, the watch's version, with our lv_conf.h ----
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE INTERNAL "")
set(LV_CONF_BUILD_DISABLE_DEMOS 1 CACHE INTERNAL "")
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
    target_compile_options(lvgl PRIVATE -w)
endif()

# ---- the game and the pieces of AmoledOS it uses ----
file(GLOB MH_GAME_SRC CONFIGURE_DEPENDS "${MH_GAME_DIR}/main/*.c")
file(GLOB AOS_FONT_SRC CONFIGURE_DEPENDS "${AMOLEDOS_DIR}/components/aos_fonts/*.c")
set(AOS_UI_SRC
    "${AMOLEDOS_DIR}/components/aos_ui/aos_i18n.c"
    "${AMOLEDOS_DIR}/components/aos_ui/aos_lang_embedded.c"
    "${AMOLEDOS_DIR}/components/aos_ui/aos_theme.c")

set(MH_SRC
    src/main.c
    src/hal_desktop.c
    src/ui_desktop.c
    src/nav.c
    ${MH_GAME_SRC}
    ${AOS_UI_SRC}
    ${AOS_FONT_SRC})

set(MH_PAK "${MH_GAME_DIR}/assets/monsterhop.pak")

if(APPLE)
    set(MH_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/MonsterHop.icns")
    set(MH_BUNDLE_RES "${MH_PAK}")
    if(EXISTS "${MH_ICNS}")
        list(APPEND MH_BUNDLE_RES "${MH_ICNS}")
    endif()
    add_executable(monsterhop MACOSX_BUNDLE ${MH_SRC} ${MH_BUNDLE_RES})
    set_source_files_properties(${MH_BUNDLE_RES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(monsterhop PROPERTIES
        OUTPUT_NAME "Monster Hop"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Monster Hop"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.charliejgallo.monsterhop"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MH_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MH_VERSION}")
elseif(WIN32)
    set(MH_RC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/monsterhop.rc")
    if(EXISTS "${MH_RC}")
        list(APPEND MH_SRC "${MH_RC}")
    endif()
    add_executable(monsterhop WIN32 ${MH_SRC})
    set_target_properties(monsterhop PROPERTIES OUTPUT_NAME "MonsterHop")
    add_custom_command(TARGET monsterhop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MH_PAK}" "$<TARGET_FILE_DIR:monsterhop>/monsterhop.pak")
else()
    add_executable(monsterhop ${MH_SRC})
    add_custom_command(TARGET monsterhop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MH_PAK}" "$<TARGET_FILE_DIR:monsterhop>/monsterhop.pak")
endif()

target_include_directories(monsterhop PRIVATE
    src
    "${MH_GAME_DIR}/main"
    "${AMOLEDOS_DIR}/components/aos_hal/include"
    "${AMOLEDOS_DIR}/components/aos_ui/include"
    "${AMOLEDOS_DIR}/components/aos_ui"
    "${AMOLEDOS_DIR}/components/aos_fonts/include")

# AOS_SIM_BUILTIN: the game registers itself and reads the MH_* development
# switches (MH_LEVEL, MH_UNLOCK...), as in the watch's simulator.
# MH_VIEW_RUNTIME + the cache's size: the wider frame (src/desktop.h).
target_compile_definitions(monsterhop PRIVATE
    AOS_SIM AOS_SIM_BUILTIN MH_DESKTOP MH_VIEW_RUNTIME MH_CW=1024 MH_CH=640
    MH_VERSION="${MH_VERSION}")
if(NOT MSVC)
    target_compile_options(monsterhop PRIVATE -Wall -Wno-unused-parameter -Wno-unused-function)
endif()

target_link_libraries(monsterhop PRIVATE lvgl SDL2::SDL2main SDL2::SDL2-static)
if(NOT WIN32)
    target_link_libraries(monsterhop PRIVATE m)
endif()
if(WIN32 AND NOT MSVC)
    # one .exe, no MinGW runtime DLLs next to it
    target_link_options(monsterhop PRIVATE -static -static-libgcc)
endif()
