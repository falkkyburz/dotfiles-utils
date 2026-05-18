cmake_minimum_required(VERSION 3.25)

project(qt-lock-menu VERSION 0.1.0 LANGUAGES CXX)

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

qt_standard_project_setup()

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
