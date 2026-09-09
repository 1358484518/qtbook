cmake_minimum_required(VERSION 3.16)
project(qt_env_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(qt_env_smoke main.cpp)
target_link_libraries(qt_env_smoke PRIVATE Qt6::Widgets)
