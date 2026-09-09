# ForgeCAD 桌面应用
# AUTOMOC：自动处理 Q_OBJECT（信号槽宏）
# AUTOUIC：自动把 .ui 文件（Qt Designer 拖出来的界面）转成 C++ 类
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

add_executable(forgecad WIN32
    forgecad/main.cpp
    ../src/ui/mainwindow.cpp
    ../src/ui/mainwindow.ui
)
target_link_libraries(forgecad PRIVATE
    forgecore
    Qt6::Widgets
)

set_target_properties(forgecad PROPERTIES
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
)
target_include_directories(forgecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui
)

if(WIN32)
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "copy Qt DLL"
    )
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms"
            "$<TARGET_FILE_DIR:forgecad>/platforms"
        COMMENT "copy Qt platforms"
    )
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Qt6::Core>/../plugins/tls"
            "$<TARGET_FILE_DIR:forgecad>/tls"
        COMMENT "copy Qt TLS backends"
    )
    add_custom_command(TARGET forgecad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VCPKG_INSTALLED_DIR}/x64-windows/bin"
            "$<TARGET_FILE_DIR:forgecad>"
        COMMENT "copy vcpkg DLL"
    )
endif()
