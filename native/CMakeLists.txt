cmake_minimum_required(VERSION 3.18.1)
project(g3dnative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(G3D_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

# Assimp is vendored in third_party/. Only the two importers the editor needs are compiled;
# FORCE keeps the configuration independent of whatever the developer cached locally.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
# Assimp sets CMAKE_DEBUG_POSTFIX "d", which leaks to every target in the same build tree and
# would rename libg3dnative.so to libg3dnatived.so, breaking System.loadLibrary("g3dnative").
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
add_subdirectory(${G3D_THIRD_PARTY}/assimp ${CMAKE_BINARY_DIR}/_assimp EXCLUDE_FROM_ALL)

set(G3D_XATLAS ${G3D_THIRD_PARTY}/xatlas/xatlas.cpp)

set(G3D_CORE_SOURCES
    core/mesh.cpp
    core/material.cpp
    core/primitives.cpp
    core/scene.cpp
    core/document.cpp
    core/uv_islands.cpp
    importers/assimp_importer.cpp
    uv/uv_service.cpp
    uv/uv_edit.cpp
    uv/uv_editor.cpp
    ${G3D_XATLAS}
)

set(G3D_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${G3D_THIRD_PARTY}
    ${G3D_THIRD_PARTY}/assimp/include
    ${G3D_THIRD_PARTY}/glm
    ${G3D_THIRD_PARTY}/xatlas
)

set_source_files_properties(${G3D_XATLAS} PROPERTIES COMPILE_OPTIONS "-w")

if (ANDROID)
    # The APK target: core + OpenGL ES renderer + JNI bridge.
    add_library(g3dnative SHARED
        ${G3D_CORE_SOURCES}
        renderer/g3d_renderer.cpp
        bridge/g3d_jni.cpp
    )
    target_include_directories(g3dnative PRIVATE ${G3D_INCLUDES})
    set_target_properties(g3dnative PROPERTIES OUTPUT_NAME g3dnative DEBUG_POSTFIX "")
    target_compile_options(g3dnative PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(g3dnative PRIVATE assimp log android EGL GLESv3)
else()
    # Host build: the same core as a static library plus the tests, so the native code can be
    # exercised without a device. See tests/run_host_test.sh.
    enable_testing()

    find_package(Threads REQUIRED)

    add_library(g3dcore STATIC ${G3D_CORE_SOURCES})
    target_include_directories(g3dcore PUBLIC ${G3D_INCLUDES})
    target_compile_options(g3dcore PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(g3dcore PUBLIC assimp Threads::Threads)

    add_executable(host_smoke_test tests/host_smoke_test.cpp)
    target_link_libraries(host_smoke_test PRIVATE g3dcore)
    add_test(NAME uv_smoke_test COMMAND host_smoke_test)

    add_executable(host_import_test tests/host_import_test.cpp)
    target_link_libraries(host_import_test PRIVATE g3dcore)
    target_compile_definitions(host_import_test
        PRIVATE G3D_TEST_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_test(NAME import_test COMMAND host_import_test)

    add_executable(host_uv_test tests/host_uv_test.cpp)
    target_link_libraries(host_uv_test PRIVATE g3dcore)
    target_compile_definitions(host_uv_test
        PRIVATE G3D_TEST_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_test(NAME uv_islands_test COMMAND host_uv_test)

    add_executable(host_uv_edit_test tests/host_uv_edit_test.cpp)
    target_link_libraries(host_uv_edit_test PRIVATE g3dcore)
    target_compile_definitions(host_uv_edit_test
        PRIVATE G3D_TEST_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    add_test(NAME uv_edit_test COMMAND host_uv_edit_test)

    # Headless GL test of the renderer. Only built when the host has EGL and OpenGL ES 3.
    find_library(G3D_EGL_LIBRARY EGL)
    find_library(G3D_GLES_LIBRARY GLESv2)
    if (G3D_EGL_LIBRARY AND G3D_GLES_LIBRARY)
        add_executable(host_render_test tests/host_render_test.cpp renderer/g3d_renderer.cpp)
        target_link_libraries(host_render_test PRIVATE g3dcore ${G3D_EGL_LIBRARY} ${G3D_GLES_LIBRARY})
        target_compile_options(host_render_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_compile_definitions(host_render_test
            PRIVATE G3D_TEST_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
        add_test(NAME render_test COMMAND host_render_test)
    else()
        message(STATUS "renderer test skipped: EGL or GLESv2 not installed on this host")
    endif()
endif()
