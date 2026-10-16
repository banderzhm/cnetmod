cmake_minimum_required(VERSION 3.28)
project(cnetmod_mimalloc_consumer LANGUAGES CXX)

find_package(cnetmod 2 CONFIG REQUIRED)

add_executable(cnetmod_mimalloc_consumer main.cpp)
target_compile_features(cnetmod_mimalloc_consumer PRIVATE cxx_std_23)
cnetmod_enable_mimalloc(cnetmod_mimalloc_consumer)
