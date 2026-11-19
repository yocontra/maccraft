cmake_minimum_required(VERSION 3.20)

# Project name can be configured from command line with -DAPP_NAME=YourAppName
if(NOT DEFINED APP_NAME)
    set(APP_NAME "App" CACHE STRING "Name of the application")
endif()

project(${APP_NAME} LANGUAGES C CXX)

# maccraft targets the stock 68000: no 68020-only instructions, no assembly.
# Do not add -mcpu=68020 here.
add_compile_options(-ffunction-sections -fdata-sections)

# Version string compiled into the About box; supplied by the build (git describe).
if(NOT DEFINED APP_VERSION)
    if(DEFINED ENV{APP_VERSION_STR} AND NOT "$ENV{APP_VERSION_STR}" STREQUAL "")
        set(APP_VERSION "$ENV{APP_VERSION_STR}")
    else()
        set(APP_VERSION "dev")
    endif()
endif()

# Set source and resource files
set(SRC_C
    src/main.c
    src/selftest.c
    src/ui/menu.c
    src/ui/event.c
    src/error.c
    src/ui/splash_window.c
    src/ui/game_window.c
    src/ui/about_window.c
    src/ui/window_manager.c
    src/ui/utils.c
    src/ui/hud.c
    src/ui/world_view.c
    src/ui/avatar.c
    src/ui/sprite_sheet.c
    src/game/world.c
    src/game/iso.c
    src/game/pick.c
    src/game/player.c
    src/game/save.c
    src/sound/sfx.c
    src/game/texture.c
    src/game/dither.c
    src/game/light.c
    src/game/blocks.c
    src/game/art.c
    src/game/bake.c
    src/game/bake_quickdraw.c
    src/game/scene.c
    src/game/sky.c
)
set(HEADER_FILES
    src/ui/menu.h
    src/ui/event.h
    src/error.h
    src/selftest.h
    src/constants.h
    src/ui/splash_window.h
    src/ui/game_window.h
    src/ui/about_window.h
    src/ui/window_manager.h
    src/ui/utils.h
    src/ui/hud.h
    src/ui/world_view.h
    src/ui/avatar.h
    src/ui/sprite_sheet.h
    src/game/world.h
    src/game/iso.h
    src/game/pick.h
    src/game/player.h
    src/game/save.h
    src/sound/sfx.h
    src/game/texture.h
    src/game/dither.h
    src/game/light.h
    src/game/blocks.h
    src/game/art.h
    src/game/bake.h
    src/game/scene.h
    src/game/sky.h
)
set(RESOURCE_FILES
    src/main.r
)

# Configuration options
option(ENABLE_CLANG_FORMAT "Enable clang-format formatting" ON)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set path to LaunchAPPL if provided
if(NOT DEFINED LAUNCH_APPL)
    set(LAUNCH_APPL "LaunchAPPL" CACHE STRING "Path to the LaunchAPPL executable")
endif()

# Find clang-format if enabled
if(ENABLE_CLANG_FORMAT)
    find_program(CLANG_FORMAT clang-format)
    if(CLANG_FORMAT)
        message(STATUS "Found clang-format: ${CLANG_FORMAT}")

        set(FORMAT_SOURCES ${SRC_C} ${HEADER_FILES})

        add_custom_target(format
            COMMENT "Running clang-format to format the code..."
            COMMAND ${CLANG_FORMAT} -i -style=file ${FORMAT_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
    else()
        message(WARNING "clang-format not found. Code formatting will not be available.")
    endif()
endif()

# Define build target
add_application(${APP_NAME}
    ${SRC_C}
    ${RESOURCE_FILES}
    CREATOR "MCft"
)

target_compile_definitions(${APP_NAME} PRIVATE APP_VERSION_STR="${APP_VERSION}")

# Save 200KB+ of code by removing unused stuff; requires the -ffunction-sections
# -fdata-sections compile options set above.
set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS "-Wl,-gc-sections")

# Add custom targets
add_custom_target(build
    DEPENDS ${APP_NAME}
    COMMENT "Building application"
)

add_custom_target(run
    DEPENDS ${APP_NAME}
    COMMAND ${LAUNCH_APPL} -e minivmac ${CMAKE_CURRENT_BINARY_DIR}/${APP_NAME}.bin
    COMMENT "Running in Mini vMac"
)
