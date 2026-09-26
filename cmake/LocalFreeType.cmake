# 本机安全策略可能拦截 vcpkg FreeType 的可选依赖。使用同版本的精简运行库，
# 并在自动部署完成之后覆盖 DLL，避免下一次编译又恢复旧文件。
set(FORGECAD_FREETYPE_RUNTIME_DIR "" CACHE PATH "本地 FreeType 运行库目录（含 Release/Debug 子目录）")
if(NOT FORGECAD_FREETYPE_RUNTIME_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/build/local-freetype/runtime/Release/freetype.dll")
    set(FORGECAD_FREETYPE_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/build/local-freetype/runtime" CACHE PATH "本地 FreeType 运行库目录" FORCE)
endif()

function(forgecad_deploy_freetype_runtime target)
    if(WIN32 AND FORGECAD_FREETYPE_RUNTIME_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FORGECAD_FREETYPE_RUNTIME_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/freetype$<$<CONFIG:Debug>:d>.dll"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploy local FreeType runtime"
            VERBATIM)
    endif()
endfunction()
