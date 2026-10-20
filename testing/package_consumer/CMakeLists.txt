cmake_minimum_required(VERSION 3.28)

project(cnetmod_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

find_package(cnetmod 2.1 CONFIG REQUIRED COMPONENTS
    core application http mysql redis orm openai)

add_executable(cnetmod_package_consumer main.cpp)
target_link_libraries(cnetmod_package_consumer PRIVATE
    cnetmod::core
    cnetmod::application
    cnetmod::http
    cnetmod::mysql
    cnetmod::redis
    cnetmod::orm
    cnetmod::openai)
