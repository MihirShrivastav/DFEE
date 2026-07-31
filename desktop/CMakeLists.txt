cmake_minimum_required(VERSION 3.26)
project(DFEEDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Build the engine core, but not its Python module or its tests.
set(DFEE_BUILD_TESTS OFF)
set(DFEE_BUILD_PYTHON OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp_engine ${CMAKE_BINARY_DIR}/cpp_engine)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Gui Concurrent)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(DFEE
    src/main.cpp
    src/EngineController.cpp
    src/EngineController.h
    src/RenderWorker.cpp
    src/RenderWorker.h
    src/PreviewImageProvider.h
)

qt_add_qml_module(DFEE
    URI DFEE
    VERSION 1.0
    QML_FILES qml/Main.qml
)

target_compile_definitions(DFEE PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(DFEE PRIVATE Qt6::Quick Qt6::Gui Qt6::Concurrent dfee_core)
set_target_properties(DFEE PROPERTIES WIN32_EXECUTABLE ON)
