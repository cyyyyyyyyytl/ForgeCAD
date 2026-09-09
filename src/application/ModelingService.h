#pragma once

#include "domain/FeatureCatalog.h"

#include <string>
#include <string_view>

namespace forge::domain { class Feature; }

namespace forge::application {

class ModelDocument;

// ============================================================
// ModelingService：应用层用例入口
// ------------------------------------------------------------
// 它不拥有 Feature，也不负责画界面；职责是把一次业务操作串起来：
//   创建：生成 ID -> Factory 构造 -> 领域校验 -> 加入 Document
//   修改：查找 Feature -> Schema 校验 -> 保存旧值 -> 修改 -> 失败回滚
//
// Qt 菜单和 AI ToolRegistry 都调用这里，避免出现两套创建/校验逻辑。
// 以后加入 CommandManager 时，也只需要在这里把直接操作替换为 Command。
// ============================================================
class ModelingService {
public:
    explicit ModelingService(ModelDocument& document);

    domain::Feature& createFeature(const std::string& type,
                                   const domain::NumericParameters& parameters);
    void setParameter(std::string_view featureId,
                      std::string_view parameterName,
                      double value);

private:
    ModelDocument& document_;
};

} // namespace forge::application
