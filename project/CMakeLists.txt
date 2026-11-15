# LoWRecomp - WWE Legends of WrestleMania (Xbox 360) as a native PC program
#
# Built on ReXGlue (static recompilation). The recompiled game code is NOT part
# of this repository: it is generated on your machine from your own copy of the
# game (rexglue codegen -> generated/default). SDK boilerplate lives in
# generated/rexglue.cmake.
#
# The normal way to build is the installer (install.bat in the repository root).

cmake_minimum_required(VERSION 3.25)
project(lowrecomp VERSION 1.0.1 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(LOWRECOMP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/lowrecomp.ico" CACHE FILEPATH
    "Icon for LoWLauncher.exe and lowrecomp.exe (.ico)")
set(LOWRECOMP_ASSET_DIR "" CACHE PATH
    "Optional: folder with generated assets (glyphs, kbhud, ps3hud from tools/low_assets.py) to copy next to the executable")

# Windows resources (icon + version information), shared by both programs
if(WIN32)
    # rc.exe wants a native path with escaped backslashes
    file(TO_NATIVE_PATH "${LOWRECOMP_ICON}" LOWRECOMP_ICON_PATH)
    string(REPLACE "\\" "\\\\" LOWRECOMP_ICON_PATH "${LOWRECOMP_ICON_PATH}")
    foreach(kind game launcher)
        if(kind STREQUAL "game")
            set(LOWRECOMP_RC_DESCRIPTION "WWE Legends of WrestleMania (LoWRecomp)")
            set(LOWRECOMP_RC_FILENAME "lowrecomp.exe")
        else()
            set(LOWRECOMP_RC_DESCRIPTION "LoWRecomp Launcher")
            set(LOWRECOMP_RC_FILENAME "LoWLauncher.exe")
        endif()
        configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${kind}.rc" @ONLY)
    endforeach()
endif()

# Sources
set(LOWRECOMP_SOURCES
    src/main.cpp
    src/low_input.cpp
    src/low_system.cpp
)

if(WIN32)
    add_executable(lowrecomp WIN32 ${LOWRECOMP_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/game.rc")
else()
    add_executable(lowrecomp ${LOWRECOMP_SOURCES})
endif()

# Graphics: for now the game draws through ReXGlue's Xenos GPU plugin
# (rexgpu-xenos.dll, selected with gpu_plugin = "xenos"). A native renderer is
# a possible future step.
option(LOWRECOMP_XENOS "Ship the Xenos GPU plugin (required for graphics at the moment)" ON)

if(LOWRECOMP_XENOS)
    rexglue_setup_target(lowrecomp GPU_PLUGINS xenos)
else()
    rexglue_setup_target(lowrecomp)
endif()

# Player configuration next to the .exe: only created on the first build, after
# that it belongs to the launcher (and the player). Reset = delete the file.
add_custom_command(TARGET lowrecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/config/lowrecomp.toml"
            "-DDST=$<TARGET_FILE_DIR:lowrecomp>/lowrecomp.toml"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake"
    # Controller mappings for third-party pads (SDL_GameControllerDB, zlib licence)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/gamecontrollerdb.txt"
            "$<TARGET_FILE_DIR:lowrecomp>/gamecontrollerdb.txt"
    COMMENT "Providing configuration and gamecontrollerdb.txt")

# Generated assets (button symbols, keyboard / PS3 HUD) - only for builds outside
# the installer, which writes them straight into the installation folder.
if(LOWRECOMP_ASSET_DIR AND EXISTS "${LOWRECOMP_ASSET_DIR}")
    set(_asset_cmds)
    foreach(sub glyphs kbhud ps3hud)
        if(EXISTS "${LOWRECOMP_ASSET_DIR}/${sub}")
            list(APPEND _asset_cmds COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                 "${LOWRECOMP_ASSET_DIR}/${sub}" "$<TARGET_FILE_DIR:lowrecomp>/${sub}")
        endif()
    endforeach()
    add_custom_target(lowrecomp_assets ALL ${_asset_cmds}
        COMMENT "Copying generated assets")
    add_dependencies(lowrecomp_assets lowrecomp)
endif()

# ---------------------------------------------------------------------------
# Launcher (LoWLauncher.exe): start window with language, display, graphics,
# controls and save-game import. Small separate program (Dear ImGui +
# Direct3D 11), independent of the game's renderer.
# ---------------------------------------------------------------------------
if(WIN32)
    set(LOW_IMGUI_DIR "" CACHE PATH "Dear ImGui sources for the launcher (rexglue-sdk/thirdparty/imgui)")
    if(NOT LOW_IMGUI_DIR AND REXSDK_DIR)
        set(LOW_IMGUI_DIR "${REXSDK_DIR}/thirdparty/imgui")
    endif()
    if(EXISTS "${LOW_IMGUI_DIR}/imgui.cpp")
        add_executable(LoWLauncher WIN32
            launcher/launcher.cpp
            "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc"
            ${LOW_IMGUI_DIR}/imgui.cpp
            ${LOW_IMGUI_DIR}/imgui_draw.cpp
            ${LOW_IMGUI_DIR}/imgui_tables.cpp
            ${LOW_IMGUI_DIR}/imgui_widgets.cpp
            ${LOW_IMGUI_DIR}/backends/imgui_impl_win32.cpp
            ${LOW_IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
        target_include_directories(LoWLauncher PRIVATE
            ${LOW_IMGUI_DIR} ${LOW_IMGUI_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR}/launcher)
        target_link_libraries(LoWLauncher PRIVATE d3d11 dxgi d3dcompiler dwmapi shell32 ole32 uuid)
        target_compile_definitions(LoWLauncher PRIVATE UNICODE _UNICODE
            LOWRECOMP_VERSION="${PROJECT_VERSION}")
        # The launcher belongs to the game: always build it along
        add_dependencies(lowrecomp LoWLauncher)
    else()
        message(WARNING "Dear ImGui not found (LOW_IMGUI_DIR='${LOW_IMGUI_DIR}') - launcher is not built")
    endif()
endif()
