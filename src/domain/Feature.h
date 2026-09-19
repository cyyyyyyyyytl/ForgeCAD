#pragma once // Feature 是多个模块共同包含的抽象接口，只允许定义一次。

#include "domain/Parameter.h" // 子类接口需要 Parameter 列表和 ParameterValue。

#include <TopoDS_Shape.hxx> // 所有具体特征统一重建成 OCCT 通用形状类型。

#include <string>  // 保存稳定 ID、类型名和校验错误文字。
#include <utility> // 构造时使用 std::move 接管字符串参数。
#include <vector>  // parameters() 统一返回有序参数列表。

namespace forge::domain {

// ============================================================
// Feature：一个参数化模型对象的共同接口
// ------------------------------------------------------------
// BoxFeature、CylinderFeature、SphereFeature 都继承它。Document 只依赖
// 这个接口，因此可以把不同具体类型放在同一个容器中（运行时多态）。
//
// id 是对象身份，例如 Box001；type 是对象种类，例如 Box，两者不能混用。
// Feature 保存参数并能重建几何，但不知道 Qt 界面、AI JSON 或 Undo 栈。
// ============================================================
class Feature {
public:
    // 基类只保存所有 Feature 都有的 ID 和类型；具体参数交给子类保存。
    Feature(std::string id, std::string type)
        : id_(std::move(id))     // 例如 Box001，用于稳定定位一个实际对象。
        , type_(std::move(type)) // 例如 Box，用于识别对象所属种类。
    {
    }
    // 基类析构必须是 virtual，才能通过 unique_ptr<Feature> 正确销毁子类。
    virtual ~Feature() = default;

    // 两个访问器都返回只读引用：不复制字符串，也不允许外部破坏对象身份。
    const std::string& id() const { return id_; }
    const std::string& type() const { return type_; }

    // 返回对象当前参数的只读视图，不复制整份参数表。
    virtual const std::vector<Parameter>& parameters() const = 0;
    // 只负责写入指定参数；对外范围检查由 ModelDocument 统一完成。
    virtual void setParameter(const std::string& name, ParameterValue value) = 0;
    // 返回空字符串表示合法，否则返回领域错误原因。
    virtual std::string validate() const = 0;
    // 根据当前参数计算 OCCT 几何；Feature 本身不长期缓存显示对象。
    virtual TopoDS_Shape rebuild() const = 0;

private:
    std::string id_;   // 文档内的稳定实例身份，例如 Sphere001。
    std::string type_; // 登记表中的类型名称，例如 Sphere。
};

} // namespace forge::domain
