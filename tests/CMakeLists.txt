# ForgeCAD 单元测试
enable_testing()

# CONFIGURE_DEPENDS 让新增 test_*.cpp 后自动触发 CMake 重新配置，免去手动登记。
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    *.cpp
)
# 所有测试合并为一个可执行文件，GoogleTest 再按 TEST 宏拆分用例。
add_executable(forgecad_tests ${TEST_SOURCES})
# forgecore 提供被测生产代码；gtest_main 提供统一测试入口。
target_link_libraries(forgecad_tests PRIVATE
    forgecore
    GTest::gtest_main
)
# 构建后扫描测试程序，把每个 TEST 注册为独立 CTest 条目。
include(GoogleTest)
gtest_discover_tests(forgecad_tests)
