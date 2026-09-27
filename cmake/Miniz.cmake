# 使用上游 miniz 3.1.2 的静态库，ZIP/CRC/压缩均交给库处理。
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(forgecad_miniz
    URL https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.1.2
    URL_HASH SHA256=98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb
    SOURCE_SUBDIR forgecad-no-upstream-cmake)
FetchContent_MakeAvailable(forgecad_miniz)
file(WRITE "${forgecad_miniz_BINARY_DIR}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(forgecad_zip STATIC
    ${forgecad_miniz_SOURCE_DIR}/miniz.c
    ${forgecad_miniz_SOURCE_DIR}/miniz_zip.c
    ${forgecad_miniz_SOURCE_DIR}/miniz_tinfl.c
    ${forgecad_miniz_SOURCE_DIR}/miniz_tdef.c)
target_include_directories(forgecad_zip PUBLIC ${forgecad_miniz_SOURCE_DIR} ${forgecad_miniz_BINARY_DIR})
target_compile_definitions(forgecad_zip PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
