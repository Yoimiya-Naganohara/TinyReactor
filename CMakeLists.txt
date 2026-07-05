cmake_minimum_required(VERSION 3.28)
project(TinyReactor VERSION 1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(TinyReactor
    src/main.cpp
    src/Core/EventLoop.cpp
    src/Worker/Worker.cpp
    src/HttpParser/HttpParser.cpp
)
target_compile_features(TinyReactor PRIVATE cxx_std_23)
