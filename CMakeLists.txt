cmake_minimum_required(VERSION 3.16)
project(CachyAutoClicker VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON) # Enables Qt Resource Compilation

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Included resources.qrc into the executable build
add_executable(CachyAutoClicker main.cpp resources.qrc)
target_link_libraries(CachyAutoClicker PRIVATE Qt6::Widgets)