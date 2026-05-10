cmake_minimum_required(VERSION 3.15)
project(reservoir_cli LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
    Confluence
    GIT_REPOSITORY https://github.com/Linifadomra/Confluence
    GIT_TAG main
)
FetchContent_MakeAvailable(Confluence)

add_executable(reservoir_cli cli.cpp)
target_include_directories(reservoir_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reservoir_cli PRIVATE
    -Wl,--start-group
    Reservoir
    confluence
    -Wl,--end-group
)