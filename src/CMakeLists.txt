# ForgeCAD 核心库
# AUTOMOC：自动调用 Qt 的 moc 编译器处理带 Q_OBJECT 的类
# （Viewport3D 用了 Q_OBJECT 信号槽宏，不开这个会链接报错 metaObject 未解析）
set(CMAKE_AUTOMOC ON)

# forgecore 汇集领域、应用、AI、几何和可复用 Viewport；MainWindow 留在应用目标中。
add_library(forgecore
    # core：版本等最底层公共能力。
    core/Version.cpp
    # application：UI/AI 共用的建模用例和模型所有权。
    application/ModelDocument.cpp
    application/ModelingService.cpp
    # assistant：DeepSeek 网络适配、Agent 循环和工具白名单。
    assistant/AgentController.cpp
    assistant/DeepSeekClient.cpp
    assistant/ToolRegistry.cpp
    # domain/geometry：参数化 Feature 及 OCCT 构造。
    domain/BoxFeature.cpp
    domain/FeatureCatalog.cpp
    geometry/ShapeFactory.cpp
    # Viewport3D 可被不同窗口复用，因此放进核心库；MainWindow 不放这里。
    ui/Viewport3D.cpp
    domain/CylinderFeature.cpp
    domain/FeatureFactory.cpp
    domain/SphereFeature.cpp

)

# 包含 src 根，便于 #include<core/...>
target_include_directories(forgecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接共享依赖
target_link_libraries(forgecore PUBLIC
    Qt6::Core               # QObject、QString、JSON、事件循环。
    Qt6::Network            # DeepSeek HTTPS 请求。
    Qt6::Widgets            # Viewport3D 继承 QWidget。
    ${OCCT_LIBRARIES}       # 精确几何、拓扑和 AIS 显示。
    spdlog::spdlog          # 统一日志接口。
)

# 需要 RTTI/异常，关闭的编译选项不需要
target_compile_definitions(forgecore PRIVATE FORGECORE_BUILDING_LIB)
target_compile_definitions(forgecore PRIVATE
    FORGECAD_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FORGECAD_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FORGECAD_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    FORGECAD_VERSION_STRING="${PROJECT_VERSION}"
)
