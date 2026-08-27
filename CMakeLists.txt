cmake_minimum_required(VERSION 3.20)
project(ForgeCAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 下强制以 UTF-8 解析源码：否则中文注释会被按 GBK 读取，触发 C4819 并导致语法错乱
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
    # ---- W3/W4 几何与 3D 视图所需（vcpkg 已验证存在）----
    TKBO        # 布尔运算 BRepAlgoAPI_Fuse/Cut/Common
    TKFillet    # Fillet / Chamfer
    TKOffset    # 偏移 / 管道等构造
    TKXCAF      # XCAF 文档（STEP 元数据）
    TKV3d       # 3D 视图核心（AIS）
    TKService   # 图形服务（视图上下文）
    TKOpenGl    # OpenGL 图形驱动
)

# ---- 子目录/库 ----
add_subdirectory(src)
add_subdirectory(apps)

if(FORGECAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- 练习代码目标（practice/ 目录，不进 Git）----
add_executable(memory_lesson practice/day1/memory_lesson.cpp)

add_executable(polymorphism practice/day1/polymorphism.cpp)

add_executable(parameter_demo practice/day1/parameter_demo.cpp)
target_include_directories(parameter_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
