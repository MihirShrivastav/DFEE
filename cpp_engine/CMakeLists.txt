cmake_minimum_required(VERSION 3.26)

project(DFEEEngine
    VERSION 0.1.0
    DESCRIPTION "Native C++ engine for the Deterministic Film Emulation Engine"
    LANGUAGES CXX
)

option(DFEE_BUILD_TESTS "Build native DFEE tests" ON)
option(DFEE_BUILD_PYTHON "Build Python extension module for FastAPI integration" ON)
option(DFEE_ENABLE_CUDA "Build optional CUDA acceleration target when CUDA is available" OFF)
option(DFEE_ENABLE_LIBRAW "Enable LibRaw integration when the library is discoverable" ON)
option(DFEE_REQUIRE_LIBRAW "Fail configure if LibRaw is not discoverable" OFF)
option(DFEE_ENABLE_OPENCV "Enable OpenCV integration when the library is discoverable" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dfee_core STATIC
    src/analyzer.cpp
    src/bias.cpp
    src/bridge_utils.cpp
    src/color_spaces.cpp
    src/cuda_runtime.cpp
    src/image.cpp
    src/profile.cpp
    src/raw_decode.cpp
    src/raw_metadata.cpp
    src/renderer.cpp
    src/session.cpp
    src/solver.cpp
)

if(DFEE_ENABLE_LIBRAW)
    find_path(DFEE_LIBRAW_INCLUDE_DIR
        NAMES libraw/libraw.h libraw.h
        DOC "LibRaw include directory")
    find_library(DFEE_LIBRAW_LIBRARY
        NAMES raw_r libraw_r raw libraw
        DOC "LibRaw library")

    if(DFEE_LIBRAW_INCLUDE_DIR AND DFEE_LIBRAW_LIBRARY)
        add_library(dfee_libraw INTERFACE)
        target_include_directories(dfee_libraw INTERFACE ${DFEE_LIBRAW_INCLUDE_DIR})
        target_link_libraries(dfee_libraw INTERFACE ${DFEE_LIBRAW_LIBRARY})
        target_compile_definitions(dfee_core PUBLIC DFEE_HAS_LIBRAW=1)
        target_link_libraries(dfee_core PUBLIC dfee_libraw)
        message(STATUS "DFEE: LibRaw enabled (${DFEE_LIBRAW_LIBRARY})")
    elseif(DFEE_REQUIRE_LIBRAW)
        message(FATAL_ERROR
            "DFEE: LibRaw is required but was not found.\n"
            "Install dependencies with vcpkg and configure with the vcpkg toolchain.\n"
            "Expected steps on Windows:\n"
            "  1. Set VCPKG_ROOT to your vcpkg checkout.\n"
            "  2. Run: cmake --preset windows-msvc-vcpkg\n"
            "Or disable the requirement with -DDFEE_REQUIRE_LIBRAW=OFF for scaffold-only builds.")
    else()
        target_compile_definitions(dfee_core PUBLIC DFEE_HAS_LIBRAW=0)
        message(WARNING
            "DFEE: LibRaw was not found. Native RAW decode remains disabled.\n"
            "To enable it, install dependencies through vcpkg and configure with the vcpkg toolchain.\n"
            "Suggested preset after setting VCPKG_ROOT: cmake --preset windows-msvc-vcpkg")
    endif()
else()
    target_compile_definitions(dfee_core PUBLIC DFEE_HAS_LIBRAW=0)
endif()

if(DFEE_ENABLE_OPENCV)
    find_package(OpenCV CONFIG QUIET COMPONENTS core imgcodecs imgproc)
    if(OpenCV_FOUND)
        target_link_libraries(dfee_core PUBLIC opencv_core opencv_imgcodecs opencv_imgproc)
        target_compile_definitions(dfee_core PUBLIC DFEE_HAS_OPENCV=1)
        message(STATUS "DFEE: OpenCV enabled")
    else()
        target_compile_definitions(dfee_core PUBLIC DFEE_HAS_OPENCV=0)
        message(WARNING
            "DFEE: OpenCV was not found. Native JPEG preview encoding remains disabled.\n"
            "To enable it, install dependencies through vcpkg and configure with the vcpkg toolchain.\n"
            "Suggested preset after setting VCPKG_ROOT: cmake --preset windows-msvc-vcpkg")
    endif()
else()
    target_compile_definitions(dfee_core PUBLIC DFEE_HAS_OPENCV=0)
endif()

find_package(yaml-cpp CONFIG QUIET)
if(yaml-cpp_FOUND)
    target_link_libraries(dfee_core PUBLIC yaml-cpp::yaml-cpp)
    message(STATUS "DFEE: yaml-cpp enabled")
else()
    message(FATAL_ERROR
        "DFEE: yaml-cpp is required for native profile loading.\n"
        "Install dependencies with vcpkg and configure with the vcpkg toolchain.\n"
        "Expected steps on Windows:\n"
        "  1. Set VCPKG_ROOT to your vcpkg checkout.\n"
        "  2. Run: cmake --preset windows-msvc-vcpkg")
endif()

target_include_directories(dfee_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(dfee_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(dfee_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(dfee_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(dfee_cli tools/dfee_cli.cpp)
target_link_libraries(dfee_cli PRIVATE dfee_core)
target_compile_definitions(dfee_cli PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

if(DFEE_BUILD_TESTS)
    enable_testing()
    add_executable(dfee_tests tests/test_core.cpp)
    target_link_libraries(dfee_tests PRIVATE dfee_core)
    target_compile_definitions(dfee_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME dfee_tests COMMAND dfee_tests)
endif()

if(DFEE_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development.Module QUIET)
    if(Python3_Development.Module_FOUND)
        add_library(dfee_native MODULE bindings/python/dfee_native_module.cpp)
        target_link_libraries(dfee_native PRIVATE dfee_core Python3::Module)
        target_include_directories(dfee_native PRIVATE ${Python3_INCLUDE_DIRS})
        set_target_properties(dfee_native PROPERTIES PREFIX "")
        if(WIN32)
            set_target_properties(dfee_native PROPERTIES SUFFIX ".pyd")
        endif()
    else()
        message(WARNING "Python3 development module was not found; dfee_native will not be built.")
    endif()
endif()

if(DFEE_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        add_library(dfee_cuda STATIC cuda/cuda_stub.cu)
        target_link_libraries(dfee_cuda PUBLIC dfee_core)
        target_compile_features(dfee_cuda PUBLIC cxx_std_20)
        target_compile_definitions(dfee_core PUBLIC DFEE_CUDA_COMPILED=1)
    else()
        message(WARNING "DFEE_ENABLE_CUDA is ON, but no CUDA compiler was found. Building CPU engine only.")
    endif()
endif()
