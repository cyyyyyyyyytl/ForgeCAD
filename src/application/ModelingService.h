#pragma once

#include "application/CommandManager.h"

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
    // Service 不拥有 Document，调用方必须保证 Document 生命周期更长。
    explicit ModelingService(ModelDocument& document);

    // 根据类型和具名参数创建特征，自动生成稳定 ID 并加入文档。
    domain::Feature& createFeature(const std::string& type,
                                   const domain::NumericParameters& parameters);
    // 按稳定 ID 修改单个参数；参数不存在或越界时抛 invalid_argument。
    void setParameter(std::string_view featureId,
                      std::string_view parameterName,
                      double value);
    // 撤销或重做最近一次通过本服务完成的建模操作。
    void undo();
    void redo();

    bool canUndo() const;
    bool canRedo() const;

private:
    ModelDocument& document_; // 当前操作目标，不参与所有权管理。
    CommandManager commandManager_;
};

} // namespace forge::application
