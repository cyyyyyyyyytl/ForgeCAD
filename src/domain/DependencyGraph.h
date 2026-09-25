#pragma once

#include <cstddef>       // std::size_t：容器数量使用的整数类型。
#include <string>        // std::string：保存 Box001 这样的特征编号。
#include <unordered_map> // unordered_map：按编号查找它的依赖名单。
#include <vector>        // vector：保存一个特征依赖的多个编号。

namespace forge::domain {

// 这张图可以想成一本“谁需要谁”的登记簿，只保存 Feature 的编号。
// 例如 addDependency("Cut001", "Box001") 表示 Cut001 需要 Box001。
// 登记簿里会出现：Cut001 -> [Box001]。
// 先有 Box001，才能计算 Cut001；修改 Box001 时，Cut001 会受影响。
// 这个类只管理编号之间的关系，不创建几何体，也不修改 ModelDocument。
class DependencyGraph {
public:
    // addNode("Box001")：登记一个编号，初始依赖名单为空。
    // 再次登记同一个编号不会增加一行。
    void addNode(const std::string& id);
    // 返回已登记的不同编号数量，不是依赖关系的数量。
    std::size_t size() const;

    // 第一个参数是“谁需要别人”，第二个参数是“它需要谁”。
    // 例如 addDependency("Cut001", "Box001") 会把 Box001 放进 Cut001 的名单。
    // 两端未登记、自己依赖自己、关系重复或形成循环时返回 false，且不改图。
    bool addDependency(const std::string& featureId, const std::string& dependsOnId);

    // dependenciesOf("Cut001")：Cut001 直接需要谁？例如 [Box001]。
    std::vector<std::string> dependenciesOf(const std::string& id) const;
    // dependentsOf("Box001")：谁直接需要 Box001？例如 [Cut001]。
    std::vector<std::string> dependentsOf(const std::string& id) const;
    // downstreamOf("Box001")：谁直接或间接受 Box001 影响？
    // 例如 Cut001 依赖 Box001，Hole001 依赖 Cut001，则返回 Cut001 和 Hole001。
    // 查询未知编号返回空列表；返回顺序不作为接口保证。
    std::vector<std::string> downstreamOf(const std::string& id) const;
    std::vector<std::string> deletionOrder(const std::string& id) const;
    std::vector<std::string> removeNodeAndDependents(const std::string& id);
    // 返回整张图的计算顺序：每个编号都排在依赖它的编号之前。
    // 例如 B 依赖 A、C 依赖 B，结果为 [A, B, C]。
    // 互不依赖的编号按字母顺序选择，便于重复运行得到相同结果。
    std::vector<std::string> topologicalOrder() const;

private:
    // 左边 string 是某个特征的编号；右边 vector<string> 是它需要的编号名单。
    // 例如："Cut001" -> {"Box001", "Cylinder001"}。
    // 一个特征可依赖零个、一个或多个特征，所以右边是列表而非单个 string。
    std::unordered_map<std::string, std::vector<std::string>> deps_;

    // 从 start 出发，沿“它依赖谁”往下找，能不能找到 target？
    // 添加 A -> B 前先问“B 现在能走到 A 吗”；若能，新边就会围成圈。
    bool reachable(const std::string& start, const std::string& target) const;
};

} // namespace forge::domain
