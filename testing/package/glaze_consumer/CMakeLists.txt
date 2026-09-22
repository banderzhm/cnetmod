cmake_minimum_required(VERSION 4.0)
project(cnetmod_glaze_consumer LANGUAGES CXX)

find_package(cnetmod 2 CONFIG REQUIRED)

add_executable(cnetmod_glaze_consumer main.cpp)
target_compile_features(cnetmod_glaze_consumer PRIVATE cxx_std_23)
target_link_libraries(cnetmod_glaze_consumer PRIVATE cnetmod::cnetmod_core)
set_property(TARGET cnetmod_glaze_consumer PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")
