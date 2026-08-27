# ForgeCAD 单元测试
enable_testing()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    *.cpp
)
add_executable(forgecad_tests ${TEST_SOURCES})
target_link_libraries(forgecad_tests PRIVATE
    forgecore
    GTest::gtest_main
    GTest::gmock
)
include(GoogleTest)
gtest_discover_tests(forgecad_tests)
