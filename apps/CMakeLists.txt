# ForgeCAD 桌面应用
# AUTOMOC：自动处理 Q_OBJECT（信号槽宏）
# AUTOUIC：自动把 .ui 文件（Qt Designer 拖出来的界面）转成 C++ 类
#   → 比如 mainwindow.ui 自动生成 ui_mainwindow.h
# 注意：目前 mainwindow 相关还没注册（等你在 Designer 里拖好再接入）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

add_executable(forgecad WIN32
    forgecad/main.cpp
)
target_link_libraries(forgecad PRIVATE
    forgecore
    Qt6::Widgets
)

# 告诉 AUTOUIC：.ui 文件在 ../resources/ui 下（以后 .ui 生成的 ui_*.h 从这里来）
set_target_properties(forgecad PROPERTIES
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../resources/ui
)

# 让 main.cpp 能 #include "mainwindow.h"（mainwindow.h 在 ../resources/ui 下）
target_include_directories(forgecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../resources/ui
)

# ---- 运行时 DLL 自动部署 ----
# 直接双击 forgecad.exe 时,Windows 只在 exe 同目录找 DLL。
# 把 Qt 的 DLL(以及 OCCT/spdlog/fmt 的 DLL)在构建后自动拷到 exe 旁边,
# 否则会报 "找不到 Qt6Core.dll"。
if(WIN32)
    # Qt 平台插件(styles/windows 等)必须放在 exe 旁的 platforms/ 子目录
    # TARGET_FILE_DIR:Qt6::Core 指向 lib 目录,Qt 的 DLL 在上一级 bin/
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "拷贝 Qt DLL 到 exe 目录"
    )
    # 平台插件在独立子目录 platforms/,单独拷贝(否则 GUI 启动报
    # "could not find or load the Qt platform plugin windows")
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms"
            "$<TARGET_FILE_DIR:forgecad>/platforms"
        COMMENT "拷贝 Qt platforms 插件"
    )
    # OCCT / spdlog / fmt 的 DLL(来自 vcpkg)
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VCPKG_INSTALLED_DIR}/x64-windows/bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "拷贝 vcpkg DLL 到 exe 目录"
    )
endif()
