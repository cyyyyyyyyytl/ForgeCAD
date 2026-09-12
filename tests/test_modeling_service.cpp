#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "application/ModelingService.h"
#include "domain/Feature.h"

using forge::application::ModelDocument;
using forge::application::ModelingService;
using forge::domain::NumericParameters;

// ============================================================
// ModelingServiceTest：验证 UI/AI 共用的应用层入口
// ------------------------------------------------------------
// Service 测试关注完整用例，而不是单个 Feature 内部算法：创建是否进入文档、
// 修改是否按 ID 生效、非法请求是否被拒绝且不会污染原值。
// ============================================================

// 使用具名参数创建 Cylinder，验证参数 Map 顺序不影响最终对象和几何重建。
TEST(ModelingServiceTest, CreatesFeatureThroughNamedApplicationInterface)
{
    // Document 先构造，Service 持有它的非拥有引用。
    ModelDocument document;
    ModelingService service(document);

    // 故意先写 height 后写 radius，验证具名参数路径。
    auto& feature = service.createFeature("Cylinder", {
        {"height", 60.0},
        {"radius", 20.0},
    });

    // Service 应负责分配 ID、加入 Document，并留下可成功重建的 Feature。
    EXPECT_EQ(feature.id(), "Cylinder001");
    EXPECT_EQ(document.findFeature("Cylinder001"), &feature);
    EXPECT_FALSE(feature.rebuild().IsNull());
}

// 创建操作也应进入统一命令历史，并在 Undo/Redo 之间保持同一个 Feature 对象。
TEST(ModelingServiceTest, CreatedFeatureCanBeUndoneAndRedone)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Box", {
        {"length", 50.0},
        {"width", 40.0},
        {"height", 30.0},
    });
    const auto* originalAddress = &feature;

    EXPECT_EQ(document.findFeature("Box001"), originalAddress);
    EXPECT_TRUE(service.canUndo());

    service.undo();
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_TRUE(service.canRedo());

    service.redo();
    EXPECT_EQ(document.findFeature("Box001"), originalAddress);
    EXPECT_TRUE(service.canUndo());
    EXPECT_FALSE(service.canRedo());
}

// 连续的“创建 + 修改”必须严格按后进先出顺序撤销，再按原顺序重做。
TEST(ModelingServiceTest, CreateAndModifyFollowLifoHistoryOrder)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    service.setParameter(feature.id(), "radius", 35.0);
    service.undo();
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 20.0);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);

    service.undo();
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);

    service.redo();
    EXPECT_EQ(document.findFeature("Sphere001"), &feature);
    service.redo();
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 35.0);
}

// 通过字符串 ID 修改参数，模拟属性面板或 AI set_parameter 工具的真实调用。
TEST(ModelingServiceTest, ModifiesParameterByStableFeatureId)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    // 修改后直接读取领域对象，确认最终持久状态而不只相信返回提示。
    service.setParameter(feature.id(), "radius", 35.0);

    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 35.0);
}

// 验证 UI 和 AI 共用的 Service 入口已经真正接入命令历史。
TEST(ModelingServiceTest, ParameterModificationCanBeUndoneAndRedone)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    // 修改成功后，命令位于 Undo 栈；Sphere 半径变成新值 35。
    service.setParameter(feature.id(), "radius", 35.0);
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 35.0);
    EXPECT_TRUE(service.canUndo());
    EXPECT_FALSE(service.canRedo());

    // Service 将 Undo 转发给内部 CommandManager，命令恢复旧值 20。
    service.undo();
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 20.0);
    // 修改命令已撤销，但更早的“创建 Sphere”命令仍可继续撤销。
    EXPECT_TRUE(service.canUndo());
    EXPECT_TRUE(service.canRedo());

    // Redo 再次执行同一命令，半径重新变成 35。
    service.redo();
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 35.0);
    EXPECT_TRUE(service.canUndo());
    EXPECT_FALSE(service.canRedo());
}

// 验证三类错误：越界值、错误参数名、错误 Feature ID。
TEST(ModelingServiceTest, RejectsInvalidFeatureParameterOrId)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    // 越界修改抛异常，并保持原 radius=20 不变，证明回滚/前置校验有效。
    EXPECT_THROW(service.setParameter(feature.id(), "radius", -5.0),
                 std::invalid_argument);
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 20.0);

    // Sphere 不存在 height 参数；不存在的对象也不能被静默忽略。
    EXPECT_THROW(service.setParameter(feature.id(), "height", 10.0),
                 std::invalid_argument);
    EXPECT_THROW(service.setParameter("missing", "radius", 10.0),
                 std::invalid_argument);
}
