#pragma once                              // 防止重复包含
#include "domain/Feature.h"               // 继承 Feature（抽象爸爸）——多态！Day1 学的

namespace forge::domain {                 // 门牌号：领域层

// ============================================================
// BoxFeature：长方体特征（Feature 的第一个"具体儿子"）
// ------------------------------------------------------------
// 大白话：这就是"一个带着长宽高参数的长方体"。
//   它是 Feature 的子类，必须实现爸爸定的 4 条规矩：
//     ① 报参数列表  ② 改参数  ③ 校验  ④ 重建
// 对比 Feature.h：那里全是"= 0 的规矩"，这里全是"实打实的实现"——
//   所以具体类通常比抽象类更好读。
// ============================================================
class BoxFeature : public Feature {
public:
    // 构造函数：造一个长方体。参数 = 身份证号 + 长宽高
    //   （注意：这里没有默认值——你必须明确给出尺寸）
    BoxFeature(std::string id, double length, double width, double height);

    // ↓↓ 4 条规矩的实现（override = 明确告诉编译器"我在实现爸爸的规矩"）↓↓

    // 规矩①：报出参数列表（返回 length/width/height 三个参数）
    const std::vector<Parameter>& parameters() const override;

    // 规矩②：按名字改参数（找不到就抛异常）
    void setParameter(const std::string& name, ParameterValue value) override;

    // 规矩③：校验（长宽高必须 > 0；合法返回空串，否则返回原因）
    std::string validate() const override;

    // 规矩④：重建（W3 前返回文本描述；W3 接 OCCT 生成真 3D 形状）
    std::string rebuild() const override;

private:
    std::vector<Parameter> params_;   // 数据：这个盒子自己的参数（length/width/height）
};

} // namespace forge::domain
