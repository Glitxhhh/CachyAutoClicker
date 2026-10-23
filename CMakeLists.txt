cmake_minimum_required(VERSION 3.16)
project(CachyAutoClicker VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON) # Enables Qt Resource Compilation

# DBus: the desktop-portal global hotkey. Network: the local socket behind --toggle/--status.
find_package(Qt6 COMPONENTS Widgets DBus Network REQUIRED)

# Included resources.qrc into the executable build
add_executable(CachyAutoClicker main.cpp portal_shortcut.h command_channel.h resources.qrc)
target_link_libraries(CachyAutoClicker PRIVATE Qt6::Widgets Qt6::DBus Qt6::Network)
