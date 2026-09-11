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
