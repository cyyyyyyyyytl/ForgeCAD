# ForgeCAD 桌面应用
# AUTOMOC：自动处理 Q_OBJECT（信号槽宏）
# AUTOUIC：自动把 .ui 文件（Qt Designer 拖出来的界面）转成 C++ 类
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# WIN32 表示 Windows GUI 程序，不弹出额外控制台窗口。
add_executable(forgecad WIN32
    forgecad/main.cpp
    ../src/ui/mainwindow.cpp
    ../src/ui/mainwindow.ui
    ../src/ui/assistantdialog.cpp
    ../src/ui/assistantdialog.h
    ../src/ui/assistantdialog.ui
)
# 主程序只直接依赖 forgecore 和 Qt Widgets；其余依赖由 forgecore 传递。
target_link_libraries(forgecad PRIVATE
    forgecore
    Qt6::Widgets
)

# 告诉 AUTOUIC 去 src/ui 查找 mainwindow.ui，否则生成时找不到 Designer 文件。
set_target_properties(forgecad PROPERTIES
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
)
# main.cpp 以 #include "mainwindow.h" 引用头文件，因此加入 UI 目录。
target_include_directories(forgecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
)

if(WIN32)
    # 将 Qt 主 DLL 放到 exe 同目录，使程序脱离 IDE 的 PATH 也能启动。
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "copy Qt DLL"
    )
    # platforms 中包含 qwindows.dll，缺少它会报“无法初始化 Qt 平台插件”。
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms"
            "$<TARGET_FILE_DIR:forgecad>/platforms"
        COMMENT "copy Qt platforms"
    )
    # tls 中包含 Schannel/OpenSSL backend，缺少它会导致 DeepSeek HTTPS 返回 HTTP 0。
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../plugins/tls"
            "$<TARGET_FILE_DIR:forgecad>/tls"
        COMMENT "copy Qt TLS backends"
    )
    # 复制 OCCT/spdlog 等 vcpkg 运行库，避免启动时报依赖 DLL 缺失。
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VCPKG_INSTALLED_DIR}/x64-windows/$<$<CONFIG:Debug>:debug/>bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "copy vcpkg DLL"
    )
endif()
forgecad_deploy_freetype_runtime(forgecad)
