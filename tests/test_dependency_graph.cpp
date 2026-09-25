#include "domain/DependencyGraph.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace {

// 图使用 unordered_map，遍历时先遇到 B 还是 C 不固定。
// 测试只关心“有哪些编号”，所以先把 vector 转成 set 再比较。
std::set<std::string> asSet(const std::vector<std::string>& ids)
{
    // begin()/end() 分别指向列表的开始和末尾；set 会用这些元素建集合。
    return {ids.begin(), ids.end()};
}

} // namespace

TEST(DependencyGraphTest, RejectsInvalidAndCyclicDependencies)
{
    forge::domain::DependencyGraph graph;
    // 先登记三个编号。只登记名字时，它们彼此还没有依赖关系。
    graph.addNode("A");
    graph.addNode("B");
    graph.addNode("C");
    graph.addNode("A"); // 重复登记不新增节点。

    EXPECT_EQ(graph.size(), 3); // A、B、C 共三行。
    EXPECT_FALSE(graph.addDependency("A", "A")); // A 不能等待自己。
    EXPECT_FALSE(graph.addDependency("A", "missing")); // 目标必须先登记。
    EXPECT_TRUE(graph.addDependency("B", "A")); // B 依赖 A。
    EXPECT_TRUE(graph.addDependency("C", "B")); // C 依赖 B。
    EXPECT_FALSE(graph.addDependency("B", "A")); // 重复关系。
    EXPECT_FALSE(graph.addDependency("A", "C")); // A 等 C，C 等 B，B 等 A，形成圈。
    EXPECT_TRUE(graph.dependenciesOf("A").empty()); // 拒绝后原图不变。
}

TEST(DependencyGraphTest, FindsDirectAndIndirectDependentsOnce)
{
    forge::domain::DependencyGraph graph;
    for (const std::string& id : {"A", "B", "C", "D"}) {
        graph.addNode(id);
    }

    // 菱形：B 和 C 都依赖 A；D 同时依赖 B、C。
    // 从 A 向“谁需要它”查找，会经过 B 和 C 两条路线，两条都通向 D。
    ASSERT_TRUE(graph.addDependency("B", "A"));
    ASSERT_TRUE(graph.addDependency("C", "A"));
    ASSERT_TRUE(graph.addDependency("D", "B"));
    ASSERT_TRUE(graph.addDependency("D", "C"));

    // 问“D 需要谁”，答案是 B、C。
    EXPECT_EQ(asSet(graph.dependenciesOf("D")),
              (std::set<std::string>{"B", "C"}));
    // 问“谁直接需要 A”，答案也是 B、C；D 隔了一层，所以不在这里。
    EXPECT_EQ(asSet(graph.dependentsOf("A")),
              (std::set<std::string>{"B", "C"}));

    // 问“谁直接或间接受 A 影响”，D 也要算进来。
    const auto downstream = graph.downstreamOf("A");
    EXPECT_EQ(asSet(downstream),
              (std::set<std::string>{"B", "C", "D"}));
    EXPECT_EQ(downstream.size(), 3); // D 虽有两条路径，也只出现一次。
    EXPECT_TRUE(graph.downstreamOf("missing").empty());
}

TEST(DependencyGraphTest, OrdersDependenciesBeforeTheirUsers)
{
    forge::domain::DependencyGraph graph;
    for (const std::string& id : {"D", "C", "B", "A"}) {
        graph.addNode(id);
    }

    // B、C 都要等 A；D 又要等 B 和 C。
    ASSERT_TRUE(graph.addDependency("B", "A"));
    ASSERT_TRUE(graph.addDependency("C", "A"));
    ASSERT_TRUE(graph.addDependency("D", "B"));
    ASSERT_TRUE(graph.addDependency("D", "C"));

    // A 最先可计算；B/C 都就绪时按字母取 B，再取 C；D 必须最后。
    EXPECT_EQ(graph.topologicalOrder(),
              (std::vector<std::string>{"A", "B", "C", "D"}));
}

TEST(DependencyGraphTest, DeletesDependentsBeforeTheirDependencies)
{
    forge::domain::DependencyGraph graph;
    for (const std::string& id : {"A", "B", "C", "X"}) {
        graph.addNode(id);
    }

    ASSERT_TRUE(graph.addDependency("B", "A"));
    ASSERT_TRUE(graph.addDependency("C", "B"));

    EXPECT_EQ(graph.deletionOrder("A"),
              (std::vector<std::string>{"C", "B", "A"}));

    // 真正移除后，只剩与 A 无关的 X；返回值保留安全的删除顺序。
    const auto removed = graph.removeNodeAndDependents("A");
    EXPECT_EQ(removed, (std::vector<std::string>{"C", "B", "A"}));
    EXPECT_EQ(graph.size(), 1);
    EXPECT_EQ(graph.topologicalOrder(),
              (std::vector<std::string>{"X"}));
    EXPECT_TRUE(graph.removeNodeAndDependents("missing").empty());
}
