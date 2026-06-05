cmake_minimum_required(VERSION 3.25)

project(dotfiles-utils VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    get_filename_component(BUILD_DIR_NAME "${CMAKE_BINARY_DIR}" NAME)
    string(TOLOWER "${BUILD_DIR_NAME}" BUILD_DIR_NAME_LOWER)
    if(BUILD_DIR_NAME_LOWER MATCHES "release")
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    else()
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
if(BUILD_TYPE_LOWER STREQUAL "debug")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for clangd" FORCE)
else()
    set(CMAKE_EXPORT_COMPILE_COMMANDS OFF CACHE BOOL "Export compile commands for clangd" FORCE)
    file(REMOVE "${CMAKE_BINARY_DIR}/compile_commands.json")
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(
        clangd_symlink ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Creating debug compile_commands.json symlink for clangd"
    )
endif()

find_package(PkgConfig REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)

set(WLR_PROTO "/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml" CACHE FILEPATH "wlr layer shell protocol XML")
set(XDG_PROTO "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" CACHE FILEPATH "xdg shell protocol XML")

set(WLR_HEADER "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
set(WLR_CODE "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c")
set(XDG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
set(XDG_CODE "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

add_custom_command(
    OUTPUT ${WLR_HEADER}
    COMMAND ${WAYLAND_SCANNER} client-header ${WLR_PROTO} ${WLR_HEADER}
    DEPENDS ${WLR_PROTO}
    VERBATIM
)

add_custom_command(
    OUTPUT ${WLR_CODE}
    COMMAND ${WAYLAND_SCANNER} private-code ${WLR_PROTO} ${WLR_CODE}
    DEPENDS ${WLR_PROTO}
    VERBATIM
)

add_custom_command(
    OUTPUT ${XDG_HEADER}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_PROTO} ${XDG_HEADER}
    DEPENDS ${XDG_PROTO}
    VERBATIM
)

add_custom_command(
    OUTPUT ${XDG_CODE}
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_PROTO} ${XDG_CODE}
    DEPENDS ${XDG_PROTO}
    VERBATIM
)

add_custom_target(minibar_protocol_bindings DEPENDS ${WLR_HEADER} ${WLR_CODE} ${XDG_HEADER} ${XDG_CODE})

add_library(imgui_app STATIC
    imgui_app.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
)

target_include_directories(imgui_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        third_party/imgui
        third_party/imgui/backends
)

target_link_libraries(imgui_app
    PUBLIC
        PkgConfig::SDL2
        PkgConfig::EGL
        PkgConfig::GLESV2
)

function(add_imgui_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE imgui_app)
endfunction()

add_imgui_executable(lock-menu lock-menu.cpp)
add_imgui_executable(power-menu power-menu.cpp)
add_imgui_executable(screenshot-menu screenshot-menu.cpp)
add_imgui_executable(screenrecord-menu screenrecord-menu.cpp)
add_imgui_executable(idle-menu idle-menu.cpp)
add_imgui_executable(clocks clocks.cpp)

target_compile_definitions(clocks
    PRIVATE
        CLOCKS_SOUND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sound"
)

add_custom_command(TARGET clocks POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/sound
            $<TARGET_FILE_DIR:clocks>/sound
)

add_executable(minibar minibar.cpp ${WLR_CODE} ${XDG_CODE})
add_dependencies(minibar minibar_protocol_bindings)
add_executable(screenfreeze screenfreeze.cpp ${WLR_CODE} ${XDG_CODE})
add_dependencies(screenfreeze minibar_protocol_bindings)
add_executable(battery-daemon battery.cpp)

set_source_files_properties(${WLR_CODE} ${XDG_CODE} PROPERTIES LANGUAGE C)
target_compile_options(minibar PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(screenfreeze PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(battery-daemon PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(minibar PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(screenfreeze PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(minibar
    PRIVATE
        PkgConfig::WAYLAND
        PkgConfig::CAIRO
        PkgConfig::PANGOCAIRO
)

target_link_libraries(screenfreeze
    PRIVATE
        PkgConfig::WAYLAND
)

install(TARGETS lock-menu power-menu screenshot-menu screenrecord-menu idle-menu clocks minibar screenfreeze battery-daemon
    RUNTIME DESTINATION bin
)

install(FILES clocks.desktop
    DESTINATION share/applications
)

install(DIRECTORY sound
    DESTINATION share/clocks
)
