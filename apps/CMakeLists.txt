# ForgeCAD 桌面应用
set(CMAKE_AUTOMOC ON)

add_executable(forgecad WIN32
    forgecad/main.cpp
)
target_link_libraries(forgecad PRIVATE
    forgecore
    Qt6::Widgets
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
