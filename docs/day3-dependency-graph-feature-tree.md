# Day 3 — 依赖图 DAG + Feature 树 + 增量重建（你的算法主场）

> 目标：实现 ForgeCAD 最值钱的模块——**参数化历史树**。这是面试官最爱深挖、也最能体现"算法功底 + 工程能力"结合的地方。你不是只懂算法吗？今天就是你把算法变成工程能力的一天。
>
> 验收标准（今天结束前应全部做到）：
> ✓ 讲清为什么 CAD 需要历史树　✓ 讲清 DAG/拓扑排序/增量重建　✓ **自己实现 DependencyGraph**（拓扑排序 + 环检测 + 下游级联）　✓ 敲完 FeatureTree　✓ 全部单测绿（DependencyGraph 8 个 + FeatureTree 9 个）　✓ 不看资料回答末尾 12 题

---

## 1. 为什么 CAD 要"历史树"？（先把动机吃透）

想象你在 SolidWorks 里建了一个盒子，然后在它上面打孔：

```
Body
├── Sketch001   （画一个矩形）
├── Extrude001  （拉伸成实体，依赖 Sketch001）
└── Hole001     （打孔，依赖 Extrude001）
```

现在你回去**把矩形的宽度从 50 改成 60**。你希望发生什么？

- 孔要**自动跟着**变宽——因为孔的位置是"拉伸后的实体"上计算的。
- 你**不希望**手动重做 Extrude 和 Hole 两步。

CAD 的做法：**把每一步操作（Feature）记下来，形成一棵历史树**。改参数 = 从被改的特征开始，按依赖顺序**重放**后续操作。这就是"参数化"。

**核心问题来了**：重放的顺序不能乱——Extrude 必须在 Sketch 之后、Hole 必须在 Extrude 之后。**怎么保证顺序正确？** 这就是依赖图 + 拓扑排序。**你的算法知识在这里直接变成钱。**

---

## 2. 依赖图 DAG：概念

- 每个 Feature 是**一个节点**。
- "Extrude 依赖 Sketch" 是一条**有向边**：`Extrude → Sketch`（意思是：重建时 Sketch 必须在前）。
- 依赖关系**必须无环**：如果 A 依赖 B、B 依赖 A，那重建时谁先？无解。所以是 **DAG（有向无环图）**。

**面试一句话**：*"参数化历史是一个 DAG：节点是 Feature，边是依赖，重建顺序由拓扑排序决定，任何成环操作在 API 层直接拒绝。"*

---

## 3. 你的主场：实现 DependencyGraph 🎯

文件：`src/domain/DependencyGraph.h`（接口，已给）+ `src/domain/DependencyGraph.cpp`（**你自己实现**）

> **教学原则**：这一节我故意不直接给你实现。接口 + 测试 = 你的"需求规格书"。算法你是会的——把思路写出来就行。**卡住 20 分钟再看文末参考答案。**

### 3.1 接口（DependencyGraph.h，直接抄）

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace forge::domain {

// 有向无环图（DAG）：记录 Feature 之间的"依赖"关系。
// 语义：addDependency(featureId, dependsOnId) 表示 featureId 依赖 dependsOnId，
//       即重建时 dependsOnId 必须先于 featureId。
class DependencyGraph {
public:
    void addNode(const std::string& id);

    // 添加依赖：featureId 依赖 dependsOnId。会成环时返回 false 且不添加。
    bool addDependency(const std::string& featureId, const std::string& dependsOnId);

    // 拓扑排序（Kahn 算法）：返回"依赖在前"的一个顺序；含环返回空 vector。
    std::vector<std::string> topoOrder() const;

    // featureId 的直接依赖（必须先重建的）
    std::vector<std::string> dependenciesOf(const std::string& id) const;

    // 直接依赖 featureId 的节点（在等它的）
    std::vector<std::string> dependentsOf(const std::string& id) const;

    // 从 featureId 出发的所有下游（含间接等待者）——增量重建就靠它
    std::vector<std::string> downstreamOf(const std::string& id) const;

    std::size_t size() const { return deps_.size(); }
    bool empty() const { return deps_.empty(); }

private:
    // 邻接表：id -> 它依赖的节点
    std::unordered_map<std::string, std::vector<std::string>> deps_;
    // 反邻接表：id -> 依赖它的节点
    std::unordered_map<std::string, std::vector<std::string>> dependents_;

    // 辅助：从 start 沿"依赖"方向能否到达 target（环检测用）
    bool reachable(const std::string& start, const std::string& target) const;
};

} // namespace forge::domain
```

### 3.2 三条提示（先自己想，再看答案）

1. **拓扑排序（Kahn）**：先把"没有依赖"的节点全入队；每次弹出一个节点加入结果，然后把它从所有"等待者"的依赖清单里划掉；谁划到 0 谁入队。最后如果结果数量 < 节点总数 → 有环。
2. **环检测（加边时）**：加边 `featureId → dependsOnId` 前，检查 **dependsOnId 沿依赖方向能不能到达 featureId**——能到达就说明这条边会形成一个环，拒绝。
3. **downstreamOf**：从 id 出发，沿**反邻接表**（等待者方向）BFS，收集所有能到达的节点。

### 3.3 测试 = 规格书（tests/day3_dependency_test.cpp，直接抄）

```cpp
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "domain/DependencyGraph.h"

using namespace forge::domain;

namespace {
// 返回 s 在 v 中的位置；不存在返回 -1
int pos(const std::vector<std::string>& v, const std::string& s) {
    for (std::size_t i = 0; i < v.size(); ++i) if (v[i] == s) return static_cast<int>(i);
    return -1;
}
}

TEST(DependencyGraphTest, LinearChainOrder) {
    DependencyGraph g;
    g.addNode("a"); g.addNode("b"); g.addNode("c");
    g.addDependency("b", "a");          // b 依赖 a
    g.addDependency("c", "b");          // c 依赖 b
    auto order = g.topoOrder();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_LT(pos(order, "a"), pos(order, "b"));
    EXPECT_LT(pos(order, "b"), pos(order, "c"));
}

TEST(DependencyGraphTest, DiamondOrder) {
    DependencyGraph g;
    g.addNode("a"); g.addNode("b"); g.addNode("c"); g.addNode("d");
    g.addDependency("b", "a");
    g.addDependency("c", "a");
    g.addDependency("d", "b");
    g.addDependency("d", "c");
    auto order = g.topoOrder();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order.front(), "a");      // a 无依赖，一定最先
    EXPECT_EQ(order.back(), "d");       // d 依赖 b/c，一定最后
}

TEST(DependencyGraphTest, IndependentNodesAllPresent) {
    DependencyGraph g;
    g.addNode("x"); g.addNode("y");
    auto order = g.topoOrder();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_NE(pos(order, "x"), -1);
    EXPECT_NE(pos(order, "y"), -1);
}

TEST(DependencyGraphTest, CycleRejected) {
    DependencyGraph g;
    g.addNode("a"); g.addNode("b");
    EXPECT_TRUE(g.addDependency("a", "b"));
    EXPECT_FALSE(g.addDependency("b", "a"));   // 会成环 → 拒绝
    // 图里仍然只有一条边：a 的依赖只有 b
    EXPECT_EQ(g.dependenciesOf("a").size(), 1u);
    EXPECT_EQ(g.dependenciesOf("b").size(), 0u);
}

TEST(DependencyGraphTest, EmptyGraphTopo) {
    DependencyGraph g;
    EXPECT_TRUE(g.topoOrder().empty());
}

TEST(DependencyGraphTest, DownstreamCascade) {
    DependencyGraph g;
    g.addNode("a"); g.addNode("b"); g.addNode("c");
    g.addDependency("b", "a");
    g.addDependency("c", "b");
    auto down = g.downstreamOf("a");
    ASSERT_EQ(down.size(), 2u);                 // b 和 c 都受 a 影响
    EXPECT_NE(pos(down, "b"), -1);
    EXPECT_NE(pos(down, "c"), -1);
    EXPECT_TRUE(g.downstreamOf("c").empty());   // 叶子没有下游
}

TEST(DependencyGraphTest, DependentsAndDependencies) {
    DependencyGraph g;
    g.addNode("a"); g.addNode("b");
    g.addDependency("b", "a");
    EXPECT_EQ(g.dependenciesOf("b"), std::vector<std::string>({"a"}));
    EXPECT_EQ(g.dependentsOf("a"), std::vector<std::string>({"b"}));
}

TEST(DependencyGraphTest, UnknownNodeDependencyRejected) {
    DependencyGraph g;
    g.addNode("a");
    EXPECT_FALSE(g.addDependency("a", "ghost"));   // ghost 不存在
}
```

> ⚠️ 注意 `DependentsAndDependencies` 用 `EXPECT_EQ` 比较 vector，要求顺序严格一致——你的实现里 push 顺序要和依赖声明顺序一致（这是接口契约的一部分）。

### 3.4 写之前再提醒（Kahn 的常见翻车点）

- 忘记把 `dependents_` 的反向关系也维护好 → `topoOrder` 里 `--indeg[y]` 找不到 y。
- `downstreamOf` 忘记去重（一个节点可能通过两条路径都被访问到）→ 用 `seen` 集合。
- 环检测只查"目标节点的直接依赖"，没查"间接依赖"→ 用 BFS/DFS 的 `reachable`。

---

## 4. FeatureTree：管理特征生命周期 + 重建调度（代码给全，敲一遍）

依赖图是"骨架"，FeatureTree 是"管理者"：它持有所有 Feature 的所有权（`unique_ptr`）、维护依赖、调度重建。**这段代码是 Day 2 的 `Feature` + Day 3 的 `DependencyGraph` 的合体，敲的时候体会所有权怎么流转。**

### 4.1 FeatureTree.h

```cpp
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "domain/DependencyGraph.h"
#include "domain/Feature.h"

namespace forge::domain {

// 一次重建的结果：featureId + 错误信息（空 = 成功）
struct RebuildResult {
    std::string featureId;
    std::string error;
};

class FeatureTree {
public:
    // 添加特征；dependsOn 声明它依赖的既有特征 id。id 重复返回 false。
    bool addFeature(std::unique_ptr<Feature> feature,
                    const std::vector<std::string>& dependsOn = {});

    // 删除特征及其所有下游；返回被删的顶层特征（下游特征被销毁）。
    std::unique_ptr<Feature> removeFeature(const std::string& id);

    Feature* find(const std::string& id);
    const Feature* find(const std::string& id) const;

    // 全量重建：按拓扑序重建每个未抑制特征；参数非法（validate() 非空）记失败，
    // 依赖了失败节点的特征记为"上游失败"（级联）。
    std::vector<RebuildResult> rebuildAll();

    // 增量重建：只重算 id 及它的所有下游（按拓扑序）——修改参数后调用这个。
    std::vector<RebuildResult> rebuildFrom(const std::string& id);

    bool suppress(const std::string& id);      // 抑制：不参与重建
    bool unsuppress(const std::string& id);
    bool isSuppressed(const std::string& id) const;

    std::size_t size() const { return features_.size(); }

private:
    std::vector<RebuildResult> rebuildOrdered(const std::vector<std::string>& order);

    DependencyGraph graph_;
    std::unordered_map<std::string, std::unique_ptr<Feature>> features_;
    std::unordered_set<std::string> suppressed_;
};

} // namespace forge::domain
```

### 4.2 FeatureTree.cpp

```cpp
#include "domain/FeatureTree.h"
#include <algorithm>

namespace forge::domain {

bool FeatureTree::addFeature(std::unique_ptr<Feature> feature,
                             const std::vector<std::string>& dependsOn) {
    if (!feature) return false;
    const std::string id = feature->id();
    if (features_.count(id)) return false;            // id 唯一
    for (const auto& dep : dependsOn) {
        if (!features_.count(dep)) return false;      // 依赖必须已存在
    }
    graph_.addNode(id);
    for (const auto& dep : dependsOn) {
        // 新节点没有等待者，加依赖不可能成环（想想为什么）
        graph_.addDependency(id, dep);
    }
    features_[id] = std::move(feature);               // 所有权：命令层 → 树
    return true;
}

std::unique_ptr<Feature> FeatureTree::removeFeature(const std::string& id) {
    if (!features_.count(id)) return nullptr;
    auto downstream = graph_.downstreamOf(id);        // 先算下游
    for (const auto& d : downstream) {                // 级联删除下游
        features_.erase(d);
        suppressed_.erase(d);
    }
    auto removed = std::move(features_[id]);          // 再删自己，所有权移交出来
    features_.erase(id);
    suppressed_.erase(id);
    return removed;
}

Feature* FeatureTree::find(const std::string& id) {
    auto it = features_.find(id);
    return it == features_.end() ? nullptr : it->second.get();
}

const Feature* FeatureTree::find(const std::string& id) const {
    auto it = features_.find(id);
    return it == features_.end() ? nullptr : it->second.get();
}

std::vector<RebuildResult> FeatureTree::rebuildAll() {
    return rebuildOrdered(graph_.topoOrder());
}

std::vector<RebuildResult> FeatureTree::rebuildFrom(const std::string& id) {
    // 受影响集合 = {id} ∪ 下游；再按全图拓扑序过滤，保证顺序正确
    auto affected = graph_.downstreamOf(id);
    std::unordered_set<std::string> set;
    set.insert(id);
    for (const auto& d : affected) set.insert(d);

    std::vector<std::string> filtered;
    for (const auto& n : graph_.topoOrder()) {
        if (set.count(n)) filtered.push_back(n);
    }
    return rebuildOrdered(filtered);
}

std::vector<RebuildResult> FeatureTree::rebuildOrdered(const std::vector<std::string>& order) {
    std::vector<RebuildResult> results;
    std::unordered_set<std::string> failed;           // 本轮已失败的特征
    for (const auto& id : order) {
        auto it = features_.find(id);
        if (it == features_.end()) continue;          // 图里的"空洞"（被删节点），跳过
        if (suppressed_.count(id)) continue;          // 被抑制，跳过

        bool depFailed = false;
        for (const auto& dep : graph_.dependenciesOf(id)) {
            if (failed.count(dep)) { depFailed = true; break; }
        }
        if (depFailed) {
            failed.insert(id);
            results.push_back({id, "依赖的上游重建失败"});
            continue;
        }

        auto err = it->second->validate();            // 重建前先校验参数
        if (!err.empty()) {
            failed.insert(id);
            results.push_back({id, err});
            continue;
        }
        it->second->rebuild();                        // 真·重建（W3 起接 OCCT）
        results.push_back({id, {}});
    }
    return results;
}

bool FeatureTree::suppress(const std::string& id) {
    if (!features_.count(id)) return false;
    suppressed_.insert(id);
    return true;
}

bool FeatureTree::unsuppress(const std::string& id) {
    if (!features_.count(id)) return false;
    suppressed_.erase(id);
    return true;
}

bool FeatureTree::isSuppressed(const std::string& id) const {
    return suppressed_.count(id) > 0;
}

} // namespace forge::domain
```

> **为什么增量重建要先取"全图拓扑序再过滤"？** 因为 BFS 出来的下游顺序不保证拓扑序（想想钻石依赖），直接按 BFS 顺序重算会算错。先拓扑、再过滤，永远正确。这是一个很好的面试"工程细节"。

### 4.3 测试（tests/day3_feature_tree_test.cpp）

```cpp
#include <gtest/gtest.h>
#include <memory>
#include "domain/FeatureTree.h"
#include "domain/FeatureFactory.h"

using namespace forge::domain;

namespace {
// Sketch001 用 BoxFeature 占位（模拟"草图"），Extrude001 依赖它
std::unique_ptr<FeatureTree> makeTree() {
    auto tree = std::make_unique<FeatureTree>();
    tree->addFeature(FeatureFactory::create("box", "Sketch001"));
    tree->addFeature(FeatureFactory::create("box", "Extrude001"), {"Sketch001"});
    return tree;
}
}

TEST(FeatureTreeTest, AddAndFind) {
    auto tree = makeTree();
    ASSERT_NE(tree->find("Sketch001"), nullptr);
    ASSERT_NE(tree->find("Extrude001"), nullptr);
    EXPECT_EQ(tree->size(), 2u);
}

TEST(FeatureTreeTest, DuplicateIdRejected) {
    auto tree = makeTree();
    EXPECT_FALSE(tree->addFeature(FeatureFactory::create("box", "Sketch001")));
}

TEST(FeatureTreeTest, UnknownDependencyRejected) {
    FeatureTree tree;
    EXPECT_TRUE(tree.addFeature(FeatureFactory::create("box", "A")));
    EXPECT_FALSE(tree.addFeature(FeatureFactory::create("box", "B"), {"NoSuch"}));
}

TEST(FeatureTreeTest, FullRebuildInTopoOrder) {
    auto tree = makeTree();
    auto results = tree->rebuildAll();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].featureId, "Sketch001");     // 依赖在前
    EXPECT_EQ(results[1].featureId, "Extrude001");
    EXPECT_TRUE(results[0].error.empty());
    EXPECT_TRUE(results[1].error.empty());
}

TEST(FeatureTreeTest, IncrementalRebuildAffectsDownstream) {
    auto tree = makeTree();
    auto results = tree->rebuildFrom("Sketch001");
    ASSERT_EQ(results.size(), 2u);                    // 自己 + 下游
    EXPECT_EQ(results[0].featureId, "Sketch001");
    EXPECT_EQ(results[1].featureId, "Extrude001");
}

TEST(FeatureTreeTest, RebuildFromLeafOnlySelf) {
    auto tree = makeTree();
    auto results = tree->rebuildFrom("Extrude001");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].featureId, "Extrude001");
}

TEST(FeatureTreeTest, InvalidFeatureCascadesError) {
    auto tree = makeTree();
    tree->find("Sketch001")->setParameter("width", 0.0);   // 非法参数
    auto results = tree->rebuildAll();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].error.empty());               // 自己失败
    EXPECT_FALSE(results[1].error.empty());               // 下游级联失败
}

TEST(FeatureTreeTest, SuppressSkipsRebuild) {
    auto tree = makeTree();
    tree->suppress("Extrude001");
    auto results = tree->rebuildAll();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].featureId, "Sketch001");
    EXPECT_TRUE(tree->isSuppressed("Extrude001"));
}

TEST(FeatureTreeTest, RemoveCascadesDownstream) {
    auto tree = makeTree();
    auto removed = tree->removeFeature("Sketch001");
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->id(), "Sketch001");
    EXPECT_EQ(tree->find("Sketch001"), nullptr);
    EXPECT_EQ(tree->find("Extrude001"), nullptr);         // 下游被级联删除
    EXPECT_EQ(tree->size(), 0u);
}
```

---

## 5. 接入工程（一步）

在 `src/CMakeLists.txt` 的 `forgecore` 里补上两个新 .cpp（其余都不用动，测试文件自动扫描）：

```cmake
add_library(forgecore
    core/Version.cpp
    geometry/ShapeFactory.cpp
    domain/BoxFeature.cpp
    domain/FeatureFactory.cpp
    domain/DependencyGraph.cpp      # ← 新增
    domain/FeatureTree.cpp          # ← 新增
)
```

然后：
```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**验收：DependencyGraph 8 个测试 + FeatureTree 9 个测试 = 17 个全绿。**

---

## 6. 今晚面试复盘 —— 12 个必答问题

1. **为什么参数化建模要记录历史（Feature 树）？**（改参数 → 按依赖顺序重放历史，不用手动重做）
2. **依赖图为什么必须无环？**（有环则重建顺序无解——A 依赖 B、B 依赖 A 谁先？）
3. **拓扑排序怎么实现？**（Kahn：入度 0 入队，弹出一个划掉它被依赖的边，减到 0 再入队；结果数 < 节点数说明有环）
4. **增量重建和全量重建的区别？**（只重算受影响子图 vs 全部；改一个参数只动它的下游）
5. **怎么找出"受影响的特征"？**（沿反邻接表 BFS 求下游传递闭包）
6. **为什么新增特征时加依赖不会产生环？**（新节点没有等待者，从新节点到老节点加边不可能闭合回路）
7. **删除特征为什么必须级联删下游？**（下游依赖它的输出，删了它下游就悬空）
8. **FeatureTree 里为什么用 unique_ptr？所有权怎么流转？**（独占所有权；addFeature 时 move 进树，removeFeature 时 move 出来）
9. **suppress 和 delete 的区别？**（抑制=暂时不参与重建、随时恢复；删除=彻底移除）
10. **重建失败怎么处理？下游呢？**（validate() 校验失败记错误；下游标记"上游失败"级联，UI 可标红）
11. **拓扑序里的"空洞"（被删节点）怎么处理？**（重建时按 features_ 过滤跳过；完整方案可给图加 removeNode，留作练习）
12. **两个特征互相依赖，你在哪一层阻止？**（API 层：addDependency 里 reachable 环检测，返回 false）

---

## ✅ Day 3 是否达标自检表

- [ ] 能脱口讲"参数化历史 = DAG + 拓扑排序 + 重放"
- [ ] 独立实现 DependencyGraph：拓扑排序、环检测、downstream BFS 全对
- [ ] DependencyGraph 8 个测试全绿
- [ ] FeatureTree 敲完且**讲得清所有权流转**（unique_ptr 进出）
- [ ] FeatureTree 9 个测试全绿
- [ ] 12 个面试题不看资料讲完

> 全勾上，Day 3 成功！明天进入：**Command 模式 + Undo/Redo（Ctrl+Z/Y）+ suppress/reorder**——把历史树变成"可撤销的交互系统"，然后 W2 核心就齐了。

---

## 📎 参考答案（先自己写 20 分钟再看）

### DependencyGraph.cpp

```cpp
#include "domain/DependencyGraph.h"
#include <deque>
#include <unordered_set>

namespace forge::domain {

void DependencyGraph::addNode(const std::string& id) {
    deps_.emplace(id, std::vector<std::string>{});
    dependents_.emplace(id, std::vector<std::string>{});
}

bool DependencyGraph::addDependency(const std::string& featureId, const std::string& dependsOnId) {
    if (!deps_.count(featureId) || !deps_.count(dependsOnId)) return false;  // 节点必须都存在
    auto& ds = deps_[featureId];
    if (std::find(ds.begin(), ds.end(), dependsOnId) != ds.end()) return true; // 已有，幂等
    if (reachable(dependsOnId, featureId)) return false;   // 会成环 → 拒绝
    ds.push_back(dependsOnId);
    dependents_[dependsOnId].push_back(featureId);
    return true;
}

std::vector<std::string> DependencyGraph::topoOrder() const {
    std::unordered_map<std::string, int> indeg;
    for (const auto& [id, ds] : deps_) indeg[id] = static_cast<int>(ds.size());

    std::deque<std::string> q;
    for (const auto& [id, d] : indeg) if (d == 0) q.push_back(id);

    std::vector<std::string> order;
    while (!q.empty()) {
        auto cur = q.front(); q.pop_front();
        order.push_back(cur);
        for (const auto& y : dependents_.at(cur)) {
            if (--indeg[y] == 0) q.push_back(y);
        }
    }
    return order.size() == deps_.size() ? order : std::vector<std::string>{};
}

std::vector<std::string> DependencyGraph::dependenciesOf(const std::string& id) const {
    auto it = deps_.find(id);
    return it == deps_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> DependencyGraph::dependentsOf(const std::string& id) const {
    auto it = dependents_.find(id);
    return it == dependents_.end() ? std::vector<std::string>{} : it->second;
}

std::vector<std::string> DependencyGraph::downstreamOf(const std::string& id) const {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    std::deque<std::string> q;
    q.push_back(id);
    seen.insert(id);
    while (!q.empty()) {
        auto cur = q.front(); q.pop_front();
        for (const auto& y : dependentsOf(cur)) {
            if (!seen.count(y)) {
                seen.insert(y);
                q.push_back(y);
                result.push_back(y);
            }
        }
    }
    return result;
}

bool DependencyGraph::reachable(const std::string& start, const std::string& target) const {
    if (start == target) return true;
    std::unordered_set<std::string> seen;
    std::deque<std::string> q{start};
    seen.insert(start);
    while (!q.empty()) {
        auto cur = q.front(); q.pop_front();
        for (const auto& y : dependenciesOf(cur)) {
            if (y == target) return true;
            if (!seen.count(y)) { seen.insert(y); q.push_back(y); }
        }
    }
    return false;
}

} // namespace forge::domain
```
