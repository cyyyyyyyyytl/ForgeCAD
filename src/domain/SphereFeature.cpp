#include "domain/SphereFeature.h"        // 自己对应的头文件（.cpp 第一行永远是它）
#include <stdexcept>                      // std::invalid_argument：参数不存在时抛的异常
#include <utility>                        // std::move
#include "geometry/ShapeFactory.h"        // 几何层工厂：真正会造球体的地方，rebuild 请它干活

namespace forge::domain {

// ------------------------------------------------------------
// 构造函数：造一个 SphereFeature
// ------------------------------------------------------------
// ① 先把 id 交给爸爸 Feature 的构造函数（名字固定填 "Sphere"）
// ② 然后往 params_ 里装进 1 个参数（只有 radius）
SphereFeature::SphereFeature(std::string id, double radius)
    : Feature(std::move(id), "Sphere") {   // 调用爸爸的构造函数（继承链第一步）
    params_.emplace_back("radius", radius);  // emplace_back = 直接在这造一个参数塞进数组
}

// 规矩①：报参数列表。直接返回成员变量（const 引用 = 只读视图，不复制）
const std::vector<Parameter>& SphereFeature::parameters() const { return params_; }

// ------------------------------------------------------------
// 规矩②：按名字改参数
// ------------------------------------------------------------
// 遍历自己的参数，找到名字匹配的 → 改值 → 返回
// 一个都找不到 → 抛异常（比如有人想改 "length"，Sphere 没有这参数）
void SphereFeature::setParameter(const std::string& name, ParameterValue value) {
    for (auto& p : params_) {                // 遍历参数列表
        if (p.name() == name) {              // 名字对上了
            p.setValue(std::move(value));    // 改值（move = 搬进去）
            return;                          // 改完立刻返回
        }
    }
    throw std::invalid_argument("Sphere 没有参数: " + name);  // 没找到 → 抛异常
}

// ------------------------------------------------------------
// 规矩③：校验参数
// ------------------------------------------------------------
// 半径 <= 0 不合法，返回原因；合法返回空串 ""
std::string SphereFeature::validate() const {
    for (const auto& p : params_) {
        if (p.asDouble() <= 0.0) {           // asDouble() 是我们 Parameter 写的方法！
            return "参数 " + p.name() + " 必须大于 0，当前值 = "
                 + std::to_string(p.asDouble());  // 把错误原因拼成字符串返回
        }
    }
    return {};                               // {} = 空串 = 合法
}

// ------------------------------------------------------------
// 规矩④：重建
// ------------------------------------------------------------
// 重建 = 重算：拿 params_ 里的半径，请几何层工厂造一个新球体返回。
// 这里没有"删除"逻辑——旧形状由引用计数自动释放（OCCT handle 管理）。
// params_[0] 就是 radius（构造时只装了这一个）
TopoDS_Shape SphereFeature::rebuild() const {
    return forge::geometry::ShapeFactory::makeSphere(
        params_[0].asDouble());   // radius：第 0 个（也是唯一一个）参数
}

} // namespace forge::domain
