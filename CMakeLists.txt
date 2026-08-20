cmake_minimum_required(VERSION 3.20)
project(ForgeCAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 是否启用测试（默认开）
option(FORGECAD_BUILD_TESTS "构建单元测试" ON)

# ---- 依赖包：Qt / OCCT / spdlog / gtest ----
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)
find_package(OpenCASCADE CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
if(FORGECAD_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

# ---- 由 vcpkg 注入的 OCCT 模块变量 ----
set(OCCT_LIBRARIES
    TKPrim
    TKBRep
    TKTopAlgo
    TKGeomBase
    TKGeomAlgo
    TKG3d
    TKG2d
    TKMesh
    TKMath
    TKernel
    TKDESTEP
    TKXSBase
)

# ---- 子目录/库 ----
add_subdirectory(src)
add_subdirectory(apps)

if(FORGECAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
