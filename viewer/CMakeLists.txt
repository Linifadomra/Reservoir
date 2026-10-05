cmake_minimum_required(VERSION 3.15)
project(reservoir_view LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL
        GIT_TAG release-3.2.20
    )
    FetchContent_MakeAvailable(SDL3)
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG v1.91.9
)
FetchContent_MakeAvailable(imgui)

add_executable(reservoir_view
    viewer.cpp
    gx_texture.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(reservoir_view PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

if(APPLE)
    target_link_libraries(reservoir_view PRIVATE Reservoir confluence SDL3::SDL3)
else()
    target_link_libraries(reservoir_view PRIVATE
        -Wl,--start-group
        Reservoir
        confluence
        -Wl,--end-group
        SDL3::SDL3
    )
endif()
