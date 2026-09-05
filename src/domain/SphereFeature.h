#pragma once                              // 防止重复包含
#include "domain/Feature.h"               // 继承 Feature（抽象爸爸）——多态！

namespace forge::domain {                 // 门牌号：领域层

// ============================================================
// SphereFeature：球体特征（Feature 的第三个"具体儿子"）
// ------------------------------------------------------------
// 大白话：这就是"一个带半径参数的球体"（球心在原点）。
//   它是 Feature 的子类，必须实现爸爸定的 4 条规矩：
//     ① 报参数列表  ② 改参数  ③ 校验  ④ 重建
// 对比 Box/Cylinder：它们有 2~3 个参数，球体只有 1 个（radius）——
//   每个具体类只写"自己的参数 + 重建方式"，公共的规矩全在爸爸那里。
// ============================================================
class SphereFeature : public Feature {
public:
    // 构造函数：造一个球体。参数 = 身份证号 + 半径
    //   （注意：这里没有默认值——你必须明确给出尺寸）
    SphereFeature(std::string id, double radius);

    // ↓↓ 4 条规矩的实现（override = 明确告诉编译器"我在实现爸爸的规矩"）↓↓

    // 规矩①：报出参数列表（返回 radius 一个参数）
    const std::vector<Parameter>& parameters() const override;

    // 规矩②：按名字改参数（找不到就抛异常）
    void setParameter(const std::string& name, ParameterValue value) override;

    // 规矩③：校验（半径必须 > 0；合法返回空串，否则返回原因）
    std::string validate() const override;

    // 规矩④：重建——按当前参数算出自己的真实 3D 形状
    TopoDS_Shape rebuild() const override;

private:
    std::vector<Parameter> params_;   // 数据：这个球体自己的参数（radius）
};

} // namespace forge::domain
