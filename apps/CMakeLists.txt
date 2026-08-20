# ForgeCAD 桌面应用
set(CMAKE_AUTOMOC ON)

add_executable(forgecad WIN32
    forgecad/main.cpp
)
target_link_libraries(forgecad PRIVATE
    forgecore
    Qt6::Widgets
)
