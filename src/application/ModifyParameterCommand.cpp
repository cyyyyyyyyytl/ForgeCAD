#include "application/ModifyParameterCommand.h"

#include "domain/Feature.h"
#include "domain/FeatureCatalog.h"

#include <stdexcept>
#include <utility>

namespace forge::application {

ModifyParameterCommand::ModifyParameterCommand(
    domain::Feature& feature,
    std::string parameterName,
    double newValue)
    : feature_(feature),
      parameterName_(std::move(parameterName)),
      newValue_(newValue)
{
    // Catalog 是参数规则表：它告诉我们参数是否存在，以及允许的最小/最大值。
    const domain::ParameterDescriptor* descriptor =
        domain::FeatureCatalog::findParameter(feature_.name(), parameterName_);

    // 找不到规则，说明类型或参数名不受系统支持。
    if (!descriptor) {
        throw std::invalid_argument(
            feature_.name() + " 没有参数: " + parameterName_);
    }

    // 新值超出规则范围时，在修改 Feature 之前直接拒绝。
    if (newValue_ < descriptor->minimum ||
        newValue_ > descriptor->maximum) {
        throw std::invalid_argument(
            "参数 " + parameterName_ + " 超出允许范围");
    }

    // 在 Feature 的参数列表中寻找目标参数。
    for (const auto& parameter : feature_.parameters()) {
        if (parameter.name() == parameterName_) {
            // 创建命令时立刻拍下旧值，供未来 Undo 使用。
            oldValue_ = parameter.asDouble();
            return;
        }
    }

    // 参数不存在时不能创建命令，否则以后无法可靠执行和撤销。
    throw std::invalid_argument(
        feature_.name() + " 没有参数: " + parameterName_);
}

void ModifyParameterCommand::execute()
{
    // 第一次执行和 Redo 都写入新值。
    feature_.setParameter(parameterName_, newValue_);
}

void ModifyParameterCommand::undo()
{
    // Undo 恢复创建命令时保存的旧值。
    feature_.setParameter(parameterName_, oldValue_);
}

} // namespace forge::application
