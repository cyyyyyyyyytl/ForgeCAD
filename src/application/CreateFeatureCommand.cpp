#include "application/CreateFeatureCommand.h"

#include "application/ModelDocument.h"

#include <stdexcept>
#include <utility>

namespace forge::application {

CreateFeatureCommand::CreateFeatureCommand(
    ModelDocument& document,
    std::unique_ptr<domain::Feature> feature)
    : document_(document),
      feature_(std::move(feature))
{
    // 空 Feature 无法创建，也没有可供 Undo 定位的稳定 ID。
    if (!feature_) {
        throw std::invalid_argument("创建命令不能持有空 Feature");
    }

    // execute() 会把 feature_ 移入 Document，因此必须在移动前单独保存 ID。
    featureId_ = feature_->id();
}

void CreateFeatureCommand::execute()
{
    // 正常状态下，只有“尚未执行”或“已经撤销”的命令才持有 feature_。
    if (executed_) {
        return;
    }
    if (!feature_) {
        throw std::logic_error("创建命令没有可执行的 Feature");
    }

    // 所有权从 CreateFeatureCommand 转移到 ModelDocument。
    document_.addFeature(std::move(feature_));
    executed_ = true;
}

void CreateFeatureCommand::undo()
{
    // 未执行的命令没有对 Document 产生影响，因此无需撤销。
    if (!executed_) {
        return;
    }

    // 所有权从 ModelDocument 取回命令；对象没有销毁，Redo 可以再次加入。
    feature_ = document_.removeFeature(featureId_);
    if (!feature_) {
        throw std::logic_error("撤销创建时找不到 Feature: " + featureId_);
    }
    executed_ = false;
}

} // namespace forge::application
