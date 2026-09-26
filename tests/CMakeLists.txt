# ForgeCAD 单元测试
enable_testing()

# CONFIGURE_DEPENDS 让新增 test_*.cpp 后自动触发 CMake 重新配置，免去手动登记。
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    *.cpp
)
list(REMOVE_ITEM TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_boolean_ui.cpp)
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

# UI 测试需要系统允许加载 OCCT 渲染 DLL；与不依赖显示环境的核心测试分开。
option(FORGECAD_BUILD_UI_TESTS "构建菜单和对话框的 Qt UI 测试" OFF)
if(FORGECAD_BUILD_UI_TESTS)
    add_executable(forgecad_ui_tests
        test_boolean_ui.cpp
        ../src/ui/mainwindow.cpp
        ../src/ui/mainwindow.ui
        ../src/ui/assistantdialog.cpp
        ../src/ui/assistantdialog.ui
    )
    target_link_libraries(forgecad_ui_tests PRIVATE forgecore GTest::gtest_main)
    add_dependencies(forgecad_ui_tests forgecad) # 复用应用构建后的 Qt DLL 和插件部署。
    set_target_properties(forgecad_ui_tests PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
    )
    gtest_discover_tests(forgecad_ui_tests DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    forgecad_deploy_freetype_runtime(forgecad_ui_tests)
endif()
