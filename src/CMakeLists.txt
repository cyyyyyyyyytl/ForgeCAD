# ForgeCAD 核心库
# AUTOMOC：自动调用 Qt 的 moc 编译器处理带 Q_OBJECT 的类
# （Viewport3D 用了 Q_OBJECT 信号槽宏，不开这个会链接报错 metaObject 未解析）
set(CMAKE_AUTOMOC ON)

add_library(forgecore
    core/Version.cpp
    domain/BoxFeature.cpp
    geometry/ShapeFactory.cpp
    ui/Viewport3D.cpp
    domain/CylinderFeature.cpp
    domain/FeatureFactory.cpp
    domain/SphereFeature.cpp

)

# 包含 src 根，便于 #include<core/...>
target_include_directories(forgecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接共享依赖
target_link_libraries(forgecore PUBLIC
    Qt6::Core
    Qt6::Widgets
    ${OCCT_LIBRARIES}
    spdlog::spdlog
)

# 需要 RTTI/异常，关闭的编译选项不需要
target_compile_definitions(forgecore PRIVATE FORGECORE_BUILDING_LIB)
target_compile_definitions(forgecore PRIVATE
    FORGECAD_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FORGECAD_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FORGECAD_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    FORGECAD_VERSION_STRING="${PROJECT_VERSION}"
)
