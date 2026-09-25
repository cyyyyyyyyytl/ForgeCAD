#include "domain/DependencyGraph.h"

#include <map>           // 按编号保存“还有几项依赖没完成”。
#include <set>           // 保存当前可计算的编号，按字母顺序取出。
#include <stdexcept>     // 图若意外成环，报告逻辑错误。
#include <unordered_set> // seen 保存已经找到的编号，防止同一编号重复进入结果。
#include <algorithm> // std::reverse：把计算顺序倒过来。
namespace forge::domain {

void DependencyGraph::addNode(const std::string& id)
{
    // deps_ 可以看成“编号 -> 依赖名单”的表。
    // [] 找到 id 时使用已有的那一行；找不到时自动新建一行。
    // 新行右侧的 vector 默认为空，例如 Box001 -> []。
    deps_[id];
}

std::size_t DependencyGraph::size() const
{
    // deps_.size() 数的是表中有几行；每个不同的 ID 只占一行。
    return deps_.size();
}

bool DependencyGraph::addDependency(
    const std::string& featureId, const std::string& dependsOnId)
{
    // contains(id) 问“表中有没有这个编号”，不会新建一行。
    // 两个编号都要先登记；A 依赖 A 则永远无法先算出 A。
    if (!deps_.contains(featureId) || !deps_.contains(dependsOnId) ||
        featureId == dependsOnId) {
        return false;
    }

    // [] 找到 featureId 对应的名单；& 表示直接使用表中的原名单，
    // 后面 push_back 时修改的就是表中的数据，不是一份临时拷贝。
    auto& dependencies = deps_[featureId];
    // 逐个检查名单中的旧编号，拒绝重复登记同一条关系。
    for (const std::string& existing : dependencies) {
        if (existing == dependsOnId) {
            return false;
        }
    }

    // 准备添加 A -> B 时，先从 B 沿已有依赖向下找 A。
    // 如果找得到，说明已有 B -> ... -> A；再添 A -> B 就会绕成圈。
    // 检查在 push_back 前执行，所以拒绝时原名单保持不变。
    if (reachable(dependsOnId, featureId)) {
        return false;
    }

    // 所有检查通过，才真正把“它需要谁”写入 featureId 的名单。
    dependencies.push_back(dependsOnId);
    return true;
}

bool DependencyGraph::reachable(
    const std::string& start, const std::string& target) const
{
    // 递归的终点：当前走到的编号正好是要找的编号。
    if (start == target) {
        return true;
    }

    // find(start) 找表中的一行；找不到时返回 end()，不是新建一行。
    const auto it = deps_.find(start);
    if (it == deps_.end()) {
        return false; // 没有这行，也就没有可继续走的依赖。
    }

    // it->second 是这一行右边的依赖名单。
    // 例如 C -> [B]、B -> [A]，从 C 找 A：先问 B，再由 B 问 A。
    // 函数在这里调用自己，这叫递归；图无环，最终会走到目标或空名单。
    for (const std::string& next : it->second) {
        if (reachable(next, target)) {
            return true;
        }
    }
    return false; // 所有路线都走过了，仍没找到目标。
}

std::vector<std::string> DependencyGraph::dependenciesOf(
    const std::string& id) const
{
    // find 不会改变登记簿；it 指向找到的那一行。
    const auto it = deps_.find(id);
    if (it == deps_.end()) {
        return {}; // 未登记的编号没有依赖名单。
    }
    // second 是该行右边的 vector；返回的是它的拷贝，调用者改它不会改图。
    return it->second;
}

std::vector<std::string> DependencyGraph::dependentsOf(
    const std::string& id) const
{
    std::vector<std::string> result;
    if (!deps_.contains(id)) {
        return result; // 未登记的编号不可能有合法的依赖者。
    }

    // 图存的是“每个特征需要谁”，没有单独存“谁需要这个特征”。
    // 因此逐行扫描：featureId 是左边编号，dependencies 是右边名单。
    for (const auto& [featureId, dependencies] : deps_) {
        // 查看该行名单中有没有我们要找的 id。
        for (const std::string& required : dependencies) {
            if (required == id) {
                // 找到了：featureId 直接依赖 id，把它加入答案。
                result.push_back(featureId);
                break; // 这一行已找到；继续检查下一行，不重复添加。
            }
        }
    }
    return result;
}

std::vector<std::string> DependencyGraph::downstreamOf(
    const std::string& id) const
{
    // result 收集答案。起点不算自己的下游，所以先保持空列表。
    std::vector<std::string> result;
    if (!deps_.contains(id)) {
        return result; // 不认识这个起点，就没有可以继续查找的关系。
    }

    // pending 像一叠“接下来要检查”的卡片；先放起点。
    // seen 像一本“已经见过”的名单；先登记起点，避免把它加进结果。
    std::vector<std::string> pending{id};
    std::unordered_set<std::string> seen{id};

    // 只要卡片堆里还有编号，就继续沿“谁依赖它”向外查找。
    while (!pending.empty()) {
        // back() 看最末尾的卡片；pop_back() 把这张卡片从待检查堆拿走。
        const std::string current = pending.back();
        pending.pop_back();

        // 查出直接依赖 current 的特征。例如 B、C 都依赖 A，这里会得到 B、C。
        for (const std::string& dependent : dependentsOf(current)) {
            // insert 会尝试把 dependent 放入 seen，并返回两个结果：
            // first 是位置，second 是“本次是否首次插入成功”的 bool。
            // 若 B、C 都依赖 A，而 D 同时依赖 B、C，D 会被找到两次。
            // 第一次插入返回 true，第二次已在名单中，返回 false。
            const bool firstVisit = seen.insert(dependent).second;
            if (firstVisit) {
                // 第一次遇见：它是答案，也要检查它后面还有谁依赖它。
                result.push_back(dependent);
                pending.push_back(dependent);
            }
        }
    }
    return result; // 每个受影响的特征最多出现一次。
}

std::vector<std::string> DependencyGraph::deletionOrder(const std::string &id) const {
    if (!deps_.contains(id)) {
        return {};
    }

    std::vector<std::string> affected = downstreamOf(id);
    affected.push_back(id);
    std::unordered_set<std::string> affectedSet(
    affected.begin(), affected.end());

    std::vector<std::string> order = topologicalOrder();
    std::reverse(order.begin(), order.end());

    std::vector<std::string> result;
    for (const std::string& current : order) {
        if (affectedSet.contains(current)) {
            result.push_back(current);
        }
    }
    return result;
}

std::vector<std::string> DependencyGraph::removeNodeAndDependents(const std::string &id) {
    const std::vector<std::string> removed = deletionOrder(id);

    for (const std::string& featureId : removed) {
        deps_.erase(featureId);
    }

    return removed;
}

std::vector<std::string> DependencyGraph::topologicalOrder() const
{
    // remaining["B"] = 1 表示 B 还有一个必须先算的特征。
    // 例如 B 依赖 A，C 依赖 B：A、B、C 的初始数量分别是 0、1、1。
    std::map<std::string, std::size_t> remaining;
    std::set<std::string> ready;
    for (const auto& [id, dependencies] : deps_) {
        remaining[id] = dependencies.size();
        if (dependencies.empty()) {
            ready.insert(id); // 不需要等别人，可以先计算。
        }
    }

    std::vector<std::string> order;
    while (!ready.empty()) {
        // set 按字母排序；begin() 是当前可计算的最小编号。
        const std::string current = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(current);

        // current 已经计算完：直接依赖它的特征都少等一个对象。
        for (const std::string& dependent : dependentsOf(current)) {
            auto& count = remaining.at(dependent);
            --count;
            if (count == 0) {
                ready.insert(dependent); // 它需要的对象现已全部算完。
            }
        }
    }

    // addDependency() 本来就会拒绝成环。若未来代码破坏了这个约束，
    // 有些节点会一直等不到 count == 0，此处明确报告问题。
    if (order.size() != deps_.size()) {
        throw std::logic_error("依赖图包含循环，无法确定计算顺序");
    }
    return order;
}
} // namespace forge::domain
