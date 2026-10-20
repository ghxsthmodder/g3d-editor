cmake_minimum_required(VERSION 3.18.1)
project(g3dnative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(G3D_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

add_library(g3dnative SHARED
    core/mesh.cpp
    core/primitives.cpp
    uv/uv_service.cpp
    bridge/g3d_jni.cpp
    ${G3D_THIRD_PARTY}/xatlas/xatlas.cpp
)

target_include_directories(g3dnative PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${G3D_THIRD_PARTY}
    ${G3D_THIRD_PARTY}/glm
    ${G3D_THIRD_PARTY}/xatlas
)

set_source_files_properties(${G3D_THIRD_PARTY}/xatlas/xatlas.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

target_compile_options(g3dnative PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(g3dnative PRIVATE log android)
