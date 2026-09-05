#pragma once                              // 防止本文件被重复包含（每个头文件都写，编译器只处理一次）
#include <string>                         // std::string：字符串类型（存 id/name 用）
#include <vector>                         // std::vector：动态数组（参数列表用）
#include <utility>                        // std::move：移动语义（构造时把值"搬"进成员，不复制）
#include "domain/Parameter.h"             // 我们刚写的 Parameter（参数）类——Feature 的成员就是它
#include <TopoDS_Shape.hxx>               // OCCT 的形状类型：rebuild() 的返回值从文本升级成真形状，编译器必须认识它

namespace forge::domain {                 // 门牌号：forge 项目 / domain 领域层（避免和别的模块撞名）

// ============================================================
// Feature：参数化特征基类（抽象类）
// ------------------------------------------------------------
// 大白话：Feature = "一个可参数化的模型对象"。
//   - Box、Cylinder、Extrude……在 ForgeCAD 里全都是 Feature 的子类
//   - 每个特征都有：唯一 ID（"Box001"）、类型名（"Box"）、参数（长宽高…）
//   - 它管不了一切的细节，只定义"每个特征必须能做什么"（规矩）
// 为什么这么设计：让系统能统一对待所有特征（多态），
//   这正是 Day1 学的"同一个命令，不同对象做出来不一样"。
// ============================================================
class Feature {
public:
    // 构造函数：创建特征时必须给它"身份证号"和"名字"
    //   id_ / name_ 是成员变量；std::move 表示"把值搬进来，不复制"（省性能）
    Feature(std::string id, std::string name)
        : id_(std::move(id)), name_(std::move(name)) {}

    // 虚析构：多态基类必须有（Day1 学的！）
    //   否则通过基类指针 delete 时，只调用基类析构，子类资源会泄漏
    virtual ~Feature() = default;

    // 方法①：报出身份证号
    //   const std::string& = 返回"原件的只读视图"，不复制（省内存）
    //   末尾 const = 承诺此方法不改内部数据
    const std::string& id() const { return id_; }     // ← 你填的！返回成员变量 id_

    // 方法②：报出类型名（如 "Box"）
    const std::string& name() const { return name_; } // ← 你填的！返回成员变量 name_

    // ↓↓↓ 下面是"纯虚函数"（= 0）= 规矩：每个子类必须自己实现 ↓↓↓

    // 规矩①：报出自己的参数列表（Box 返回 [length, width, height]）
    virtual const std::vector<Parameter>& parameters() const = 0;

    // 规矩②：按名字改参数（比如把 length 改成 200）
    virtual void setParameter(const std::string& name, ParameterValue value) = 0;

    // 规矩③：检查自己参数合不合法（长宽高必须 > 0）
    //   合法返回空串 ""，不合法返回原因（如 "length 必须大于 0"）
    virtual std::string validate() const = 0;

    // 规矩④：重建自己——按当前参数重新算出自己的几何形状（重建 = 重算，不是"删了再建"）
    virtual TopoDS_Shape rebuild() const = 0;

private:
    std::string id_;      // 数据①：唯一身份证号，如 "Box001"（整个工程里不能重复）
    std::string name_;    // 数据②：类型名，如 "Box"（用户看到的名字）
};

} // namespace forge::domain
