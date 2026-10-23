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
add_executable(CachyAutoClicker main.cpp x11_keys.cpp x11_keys.h portal_shortcut.h command_channel.h switcher_watcher.h resources.qrc)
target_link_libraries(CachyAutoClicker PRIVATE Qt6::Widgets Qt6::DBus Qt6::Network)

# Optional: X11 sessions grab the hotkey and read Alt through libX11, which also sees keys that Deskflow & co
# inject with XTest. Without libX11 the app still builds and the Wayland / evdev paths work as before.
find_package(X11)
if(X11_FOUND)
    target_compile_definitions(CachyAutoClicker PRIVATE CAC_HAVE_X11)
    target_link_libraries(CachyAutoClicker PRIVATE X11::X11)
endif()
