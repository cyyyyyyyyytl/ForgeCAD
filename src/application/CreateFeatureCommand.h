#pragma once

#include "application/Command.h"
#include "domain/Feature.h"

#include <memory>
#include <string>

namespace forge::application {

class ModelDocument;

// CreateFeatureCommand 把“创建 Feature”封装成可以撤销和重做的操作。
// Feature 的 unique_ptr 会在命令和 Document 之间移动，全程只有一个所有者。
class CreateFeatureCommand final : public Command {
public:
    CreateFeatureCommand(
        ModelDocument& document,
        std::unique_ptr<domain::Feature> feature);

    // 把 Feature 所有权交给 Document。
    void execute() override;

    // 从 Document 取回 Feature 所有权，留待下一次 Redo。
    void undo() override;

private:
    ModelDocument& document_;                    // 被修改的文档，不参与所有权管理。
    std::unique_ptr<domain::Feature> feature_;   // 执行前或撤销后暂时持有 Feature。
    std::string featureId_;                      // feature_ 移走后仍用于按 ID 取回对象。
    bool executed_ = false;                      // 记录 Feature 当前是否位于 Document 中。
};

} // namespace forge::application
