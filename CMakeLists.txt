cmake_minimum_required(VERSION 3.25)

project(qt-lock-menu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(
        clangd_symlink ALL
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Creating compile_commands.json symlink for clangd"
    )
endif()

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml Quick QuickControls2)
find_package(PkgConfig REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)

qt_standard_project_setup()

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

qt_add_executable(lock-menu
    lock-menu.cpp
)

qt_add_executable(power-menu
    power-menu.cpp
)

qt_add_executable(screenshot-menu
    screenshot-menu.cpp
)

qt_add_executable(screenrecord-menu
    screenrecord-menu.cpp
)

qt_add_executable(idle-menu
    idle-menu.cpp
)

add_executable(minibar minibar.cpp ${WLR_CODE} ${XDG_CODE})
add_dependencies(minibar minibar_protocol_bindings)
set_source_files_properties(${WLR_CODE} ${XDG_CODE} PROPERTIES LANGUAGE C)
set_target_properties(minibar PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_options(minibar PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(minibar PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(lock-menu
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_link_libraries(power-menu
    PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_link_libraries(screenshot-menu
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_link_libraries(screenrecord-menu
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_link_libraries(idle-menu
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

target_link_libraries(minibar
    PRIVATE
        PkgConfig::WAYLAND
        PkgConfig::CAIRO
        PkgConfig::PANGOCAIRO
)

install(TARGETS lock-menu
    RUNTIME DESTINATION bin
)

install(TARGETS power-menu
    RUNTIME DESTINATION bin
)

install(TARGETS screenshot-menu
    RUNTIME DESTINATION bin
)

install(TARGETS screenrecord-menu
    RUNTIME DESTINATION bin
)

install(TARGETS idle-menu
    RUNTIME DESTINATION bin
)

install(TARGETS minibar
    RUNTIME DESTINATION bin
)
