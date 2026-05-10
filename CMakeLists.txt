cmake_minimum_required(VERSION 3.15)
project(Reservoir VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Confluence REQUIRED)

## == Library Targets == ##
add_library(Reservoir
    src/blo/blo.cpp
    src/patch/patch.cpp
    src/mat1/mat1.cpp
    src/reservoir.cpp
)

target_include_directories(Reservoir
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_link_libraries(Reservoir
    PRIVATE
        Confluence::confluence
)

## == Installation == ##
include(GNUInstallDirs)

install(TARGETS Reservoir
    EXPORT ReservoirTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ReservoirTargets
    FILE ReservoirTargets.cmake
    NAMESPACE Reservoir::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Reservoir
)

add_library(Reservoir::Reservoir ALIAS Reservoir)