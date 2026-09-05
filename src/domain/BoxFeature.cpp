#include "domain/BoxFeature.h"            // 自己对应的头文件（.cpp 第一行永远是它）
#include <stdexcept>                      // std::invalid_argument：参数不存在时抛的异常
#include <utility>                        // std::move
#include "geometry/ShapeFactory.h"            // 几何层工厂：真正会造盒子的地方，rebuild 请它干活

namespace forge::domain {

// ------------------------------------------------------------
// 构造函数：造一个 BoxFeature
// ------------------------------------------------------------
// ① 先把 id 交给爸爸 Feature 的构造函数（名字固定填 "Box"）
// ② 然后往 params_ 里按顺序装进 3 个参数（顺序很重要！后面按下标取）
BoxFeature::BoxFeature(std::string id, double length, double width, double height)
    : Feature(std::move(id), "Box") {            // 调用爸爸的构造函数（继承链第一步）
    params_.emplace_back("length", length);      // emplace_back = 直接在这造一个参数塞进数组
    params_.emplace_back("width",  width);       // （比 push_back 少一次拷贝）
    params_.emplace_back("height", height);
}

// 规矩①：报参数列表。直接返回成员变量（const 引用 = 只读视图，不复制）
const std::vector<Parameter>& BoxFeature::parameters() const { return params_; }

// ------------------------------------------------------------
// 规矩②：按名字改参数
// ------------------------------------------------------------
// 遍历自己的 3 个参数，找到名字匹配的 → 改值 → 返回
// 一个都找不到 → 抛异常（比如有人想改 "radius"，Box 没有这参数）
void BoxFeature::setParameter(const std::string& name, ParameterValue value) {
    for (auto& p : params_) {                    // 遍历参数列表
        if (p.name() == name) {                  // 名字对上了
            p.setValue(std::move(value));        // 改值（move = 搬进去）
            return;                              // 改完立刻返回
        }
    }
    throw std::invalid_argument("Box 没有参数: " + name);   // 没找到 → 抛异常
}

// ------------------------------------------------------------
// 规矩③：校验参数
// ------------------------------------------------------------
// 长宽高任何一个 <= 0 都不合法，返回原因；全合法返回空串 ""
std::string BoxFeature::validate() const {
    for (const auto& p : params_) {
        if (p.asDouble() <= 0.0) {               // asDouble() 是我们 Parameter 写的方法！
            return "参数 " + p.name() + " 必须大于 0，当前值 = "
                 + std::to_string(p.asDouble()); // 把错误原因拼成字符串返回
        }
    }
    return {};                                   // {} = 空串 = 合法
}

// ------------------------------------------------------------
// 规矩④：重建
// ------------------------------------------------------------
// 重建 = 重算：拿 params_ 里现在的三个数，请几何层工厂造一个新形状返回。
// 这里没有"删除"逻辑——旧形状由引用计数自动释放（OCCT handle 管理）。
// params_[0]/[1]/[2] 就是 length/width/height（构造时按这个顺序装的）
TopoDS_Shape BoxFeature::rebuild() const {
    return forge::geometry::ShapeFactory::makeBox(
        params_[0].asDouble(),   // length：第 0 个参数，取成 double
        params_[1].asDouble(),   // width
        params_[2].asDouble());  // height
}

} // namespace forge::domain
