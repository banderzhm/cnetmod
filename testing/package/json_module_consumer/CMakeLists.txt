cmake_minimum_required(VERSION 4.0)
project(cnetmod_json_module_consumer LANGUAGES CXX)

find_package(cnetmod 2 CONFIG REQUIRED)

add_executable(cnetmod_json_module_consumer main.cpp)
target_compile_features(cnetmod_json_module_consumer PRIVATE cxx_std_23)
target_link_libraries(cnetmod_json_module_consumer PRIVATE cnetmod::runtime)
