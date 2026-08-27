# Day 4 — Command 模式 + Undo/Redo（Ctrl+Z/Y 的实现）

> 目标：给 ForgeCAD 装上"后悔药"——撤销/重做。今天学完，你的 W2 核心（Feature 系统 + 依赖图 + Undo/Redo）就齐了，这是面试时**最能撑起 30 分钟深挖**的一块。
>
> 验收标准（今天结束前应全部做到）：
> ✓ 讲清双栈原理　✓ 讲清 Command 为什么能撤销　✓ 敲完 CommandManager + 3 个具体命令　✓ 讲清命令里的所有权流转　✓ 9 个单测全绿　✓ 不看资料回答末尾 12 题

---

## 1. 先想一个问题：怎么给"操作"装后悔药？

用户点了"删除 Box001"，程序把 Box001 从 Feature 树里删掉了。现在用户按 **Ctrl+Z**——怎么把 Box001 变回来？

三个候选方案，你感受一下差别：

| 方案 | 做法 | 问题 |
|---|---|---|
| 快照 | 每次操作前把整棵树序列化存一份 | 内存爆炸；大模型卡死 |
| 逆操作 | 删除前记住"它原来在哪"，撤销=加回去 | 每个操作都要写"反向逻辑"，散落在业务代码里 |
| **Command（命令）** | **把"这次删除"本身封装成一个对象**，对象里自带 `execute()` 和 `undo()` | 业务代码只管执行；撤销逻辑跟着命令走 |

**Command 的本质**：把"一段操作"变成"一个对象"。对象可以**存进栈、拿出来执行、再存回去**——这就是可撤销的前提。

> **面试一句话**：*"Undo/Redo 用 Command 模式实现：每个用户操作封装成命令对象（execute/undo 成对出现），执行后压入 undo 栈；Ctrl+Z 弹栈调用 undo() 再压入 redo 栈；Ctrl+Y 反之。新命令执行时清空 redo 栈。"*

---

## 2. 双栈原理（先画图再写码）

```
执行 C1:                [undo 栈: C1        ]  [redo 栈:        ]
执行 C2:                [undo 栈: C1 C2     ]  [redo 栈:        ]
Ctrl+Z (撤销 C2):       [undo 栈: C1        ]  [redo 栈: C2     ]
Ctrl+Z (撤销 C1):       [undo 栈:            ]  [redo 栈: C2 C1  ]
Ctrl+Y (重做 C1):       [undo 栈: C1        ]  [redo 栈: C2     ]
执行 C3 (新操作):       [undo 栈: C1 C3     ]  [redo 栈:        ]  ← redo 被清空！
```

**关键规则：新命令执行时清空 redo 栈。** 为什么？历史分叉了——撤销到一半，用户做了个新操作，那"被撤销的未来"（C2）就永远不该回来了。这是和"单栈+游标"方案的本质区别。

---

## 3. 代码：CommandManager（双栈核心）

文件：`src/application/Command.h` + `src/application/CommandManager.h/.cpp`

### Command.h
```cpp
#pragma once
#include <string>

namespace forge::application {

// 一次"用户操作"封装成对象：可执行、可撤销。
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string name() const = 0;
    virtual bool failed() const { return false; }   // 执行失败返回 true（失败命令不入栈）
};

}
```

### CommandManager.h
```cpp
#pragma once
#include <memory>
#include <vector>
#include "application/Command.h"

namespace forge::application {

// Undo/Redo 核心：双栈。
class CommandManager {
public:
    // 执行命令：成功返回 true 并入 undo 栈、清空 redo 栈；失败返回 false 且不入栈。
    bool execute(std::unique_ptr<Command> cmd);
    bool undo();                                  // 栈空返回 false
    bool redo();
    std::size_t undoCount() const { return undoStack_.size(); }
    std::size_t redoCount() const { return redoStack_.size(); }
    void clear();

private:
    std::vector<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
};

}
```

### CommandManager.cpp
```cpp
#include "application/CommandManager.h"

namespace forge::application {

bool CommandManager::execute(std::unique_ptr<Command> cmd) {
    if (!cmd) return false;
    cmd->execute();
    if (cmd->failed()) return false;             // 失败的命令不入栈（否则 undo 会撤销没生效的操作）
    undoStack_.push_back(std::move(cmd));        // 所有权：调用方 → 管理器
    redoStack_.clear();                          // 新命令使 redo 失效（历史分叉）
    return true;
}

bool CommandManager::undo() {
    if (undoStack_.empty()) return false;
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));      // 撤下来的命令进 redo 栈，等 Ctrl+Y
    return true;
}

bool CommandManager::redo() {
    if (redoStack_.empty()) return false;
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();                            // 重放（不是 execute() 里的"入栈"逻辑）
    undoStack_.push_back(std::move(cmd));
    return true;
}

void CommandManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

}
```

> **注意**：`undo()`/`redo()` 里调的是**命令自己的** `execute()`，不是 `CommandManager::execute()`——否则会再清一次 redo 栈、再压一次栈，逻辑就乱了。这是一个常见的面试陷阱。

> **💡 真实教训（本教材第一版踩过，测试抓出来的）**：`execute()` 最初返回 `void`，测试想确认"失败的命令不入栈"，于是在 `std::move` 之后还拿着 `cmd.get()` 的裸指针去读 `failed()`——但失败的命令不被入栈，`execute()` 返回时参数里的 `unique_ptr` 就把命令对象**销毁**了，裸指针变成**悬垂指针**，一读就崩（SEH 0xc0000005）。修正：让 `execute()` 返回 `bool`，调用方立刻知道成败，不再需要悬垂指针。**这就是我们写测试的原因之一——测试能抓住"你以为对其实没对"的代码。**

---

## 4. 三个具体命令（所有权流转是重点！）

### 4.1 AddFeatureCommand：添加特征

```cpp
// AddFeatureCommand.h
#pragma once
#include <memory>
#include "application/Command.h"
#include "domain/Feature.h"
#include "domain/FeatureTree.h"

namespace forge::application {

// 添加特征：execute 把特征所有权交给 FeatureTree；undo 时取回。
class AddFeatureCommand : public Command {
public:
    AddFeatureCommand(forge::domain::FeatureTree& tree,
                      std::unique_ptr<forge::domain::Feature> feature);
    void execute() override;
    void undo() override;
    std::string name() const override { return "添加特征 " + id_; }

private:
    forge::domain::FeatureTree& tree_;
    std::string id_;
    std::unique_ptr<forge::domain::Feature> feature_;  // 未执行或已撤销时持有
    bool executed_ = false;
};

}
```

```cpp
// AddFeatureCommand.cpp
#include "application/AddFeatureCommand.h"

namespace forge::application {

AddFeatureCommand::AddFeatureCommand(forge::domain::FeatureTree& tree,
                                     std::unique_ptr<forge::domain::Feature> feature)
    : tree_(tree), feature_(std::move(feature)) {}

void AddFeatureCommand::execute() {
    if (executed_) return;
    id_ = feature_->id();
    tree_.addFeature(std::move(feature_));   // 所有权：命令 → 树
    executed_ = true;
}

void AddFeatureCommand::undo() {
    if (!executed_) return;
    feature_ = tree_.removeFeature(id_);     // 所有权：树 → 命令（取回，等 Ctrl+Y）
    executed_ = false;
}

}
```

> 🔑 **所有权流转图**：调用方 →(execute) 命令 →(execute) 树 →(undo) 命令 →(redo) 树。`unique_ptr` 全程独占，谁都不泄露、不双删。**这就是 Day 1 学的 move 语义在真实工程里的用法。**

### 4.2 DeleteFeatureCommand：删除特征（有下游就拒绝）

```cpp
// DeleteFeatureCommand.h
#pragma once
#include <memory>
#include "application/Command.h"
#include "domain/FeatureTree.h"

namespace forge::application {

// 删除特征：有下游依赖时拒绝执行（级联删除无法完美撤销）。
class DeleteFeatureCommand : public Command {
public:
    DeleteFeatureCommand(forge::domain::FeatureTree& tree, const std::string& id);
    void execute() override;
    void undo() override;
    std::string name() const override { return "删除特征 " + id_; }
    bool failed() const override { return failed_; }

private:
    forge::domain::FeatureTree& tree_;
    std::string id_;
    std::unique_ptr<forge::domain::Feature> feature_;
    bool executed_ = false;
    bool failed_ = false;
};

}
```

```cpp
// DeleteFeatureCommand.cpp
#include "application/DeleteFeatureCommand.h"

namespace forge::application {

DeleteFeatureCommand::DeleteFeatureCommand(forge::domain::FeatureTree& tree, const std::string& id)
    : tree_(tree), id_(id) {}

void DeleteFeatureCommand::execute() {
    if (executed_ || failed_) return;
    if (tree_.hasDependents(id_)) { failed_ = true; return; }   // 有下游，拒绝
    feature_ = tree_.removeFeature(id_);
    if (!feature_) { failed_ = true; return; }
    executed_ = true;
}

void DeleteFeatureCommand::undo() {
    if (!executed_) return;
    tree_.addFeature(std::move(feature_));   // 放回树
    executed_ = false;
}

}
```

> **先给 Day 3 的 FeatureTree 加一个方法**（在 `FeatureTree.h` 里加，删掉那行注释）：
> ```cpp
> bool hasDependents(const std::string& id) const { return !graph_.dependentsOf(id).empty(); }
> ```

### 4.3 ModifyParameterCommand：修改参数（记住旧值）

```cpp
// ModifyParameterCommand.h
#pragma once
#include <utility>
#include "application/Command.h"
#include "domain/Parameter.h"
#include "domain/FeatureTree.h"

namespace forge::application {

// 修改参数：execute 记住旧值并写入新值；undo 恢复旧值。
class ModifyParameterCommand : public Command {
public:
    ModifyParameterCommand(forge::domain::FeatureTree& tree,
                           const std::string& featureId,
                           const std::string& paramName,
                           forge::domain::ParameterValue newValue);
    void execute() override;
    void undo() override;
    std::string name() const override { return "修改参数 " + featureId_ + "." + paramName_; }
    bool failed() const override { return failed_; }

private:
    forge::domain::FeatureTree& tree_;
    std::string featureId_;
    std::string paramName_;
    forge::domain::ParameterValue oldValue_;
    forge::domain::ParameterValue newValue_;
    bool executed_ = false;
    bool failed_ = false;
};

}
```

```cpp
// ModifyParameterCommand.cpp
#include "application/ModifyParameterCommand.h"

namespace forge::application {

ModifyParameterCommand::ModifyParameterCommand(forge::domain::FeatureTree& tree,
                                               const std::string& featureId,
                                               const std::string& paramName,
                                               forge::domain::ParameterValue newValue)
    : tree_(tree), featureId_(featureId), paramName_(paramName), newValue_(std::move(newValue)) {}

void ModifyParameterCommand::execute() {
    if (executed_ || failed_) return;
    auto* f = tree_.find(featureId_);
    if (!f) { failed_ = true; return; }
    // 找到旧值并记下来（撤销时要用）
    bool found = false;
    for (const auto& p : f->parameters()) {
        if (p.name() == paramName_) { oldValue_ = p.value(); found = true; break; }
    }
    if (!found) { failed_ = true; return; }        // 参数不存在
    f->setParameter(paramName_, newValue_);
    executed_ = true;
}

void ModifyParameterCommand::undo() {
    if (!executed_) return;
    tree_.find(featureId_)->setParameter(paramName_, oldValue_);
    executed_ = false;
}

}
```

> **为什么记旧值而不是"重放参数"？** 修改参数是最轻量的命令——只存一个旧值，撤销就是"写回去"。反过来想：如果撤销靠"重新执行历史"，那参数历史会无限膨胀。**选择哪种取决于"逆操作的代价"**，这是设计权衡，面试官爱问。

---

## 5. 测试（tests/day4_command_test.cpp）—— 9 个用例

```cpp
#include <gtest/gtest.h>
#include <memory>
#include "domain/FeatureTree.h"
#include "domain/FeatureFactory.h"
#include "application/CommandManager.h"
#include "application/AddFeatureCommand.h"
#include "application/DeleteFeatureCommand.h"
#include "application/ModifyParameterCommand.h"

using namespace forge::domain;
using namespace forge::application;

TEST(CommandManagerTest, ExecutePushesUndoStack) {
    FeatureTree tree;
    CommandManager mgr;
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "A")));
    EXPECT_EQ(mgr.undoCount(), 1u);
    EXPECT_EQ(mgr.redoCount(), 0u);
    EXPECT_NE(tree.find("A"), nullptr);
}

TEST(CommandManagerTest, UndoRedoAddFeature) {
    FeatureTree tree;
    CommandManager mgr;
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "A")));
    EXPECT_TRUE(mgr.undo());
    EXPECT_EQ(tree.find("A"), nullptr);          // 撤销：特征被移除
    EXPECT_EQ(mgr.undoCount(), 0u);
    EXPECT_EQ(mgr.redoCount(), 1u);
    EXPECT_TRUE(mgr.redo());
    EXPECT_NE(tree.find("A"), nullptr);          // 重做：特征回来
    EXPECT_EQ(mgr.undoCount(), 1u);
}

TEST(CommandManagerTest, UndoEmptyReturnsFalse) {
    FeatureTree tree;
    CommandManager mgr;
    EXPECT_FALSE(mgr.undo());
    EXPECT_FALSE(mgr.redo());
}

TEST(CommandManagerTest, NewCommandClearsRedoStack) {
    FeatureTree tree;
    CommandManager mgr;
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "A")));
    mgr.undo();
    EXPECT_EQ(mgr.redoCount(), 1u);
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "B")));
    EXPECT_EQ(mgr.redoCount(), 0u);              // 新命令使 redo 失效
}

TEST(CommandManagerTest, ModifyParameterUndoRestoresOld) {
    FeatureTree tree;
    tree.addFeature(FeatureFactory::create("box", "Box001"));
    CommandManager mgr;
    mgr.execute(std::make_unique<ModifyParameterCommand>(tree, "Box001", "height", 60.0));
    EXPECT_DOUBLE_EQ(tree.find("Box001")->parameters()[2].asDouble(), 60.0);
    mgr.undo();
    EXPECT_DOUBLE_EQ(tree.find("Box001")->parameters()[2].asDouble(), 30.0);  // 旧值
    mgr.redo();
    EXPECT_DOUBLE_EQ(tree.find("Box001")->parameters()[2].asDouble(), 60.0);
}

TEST(CommandManagerTest, ModifyUnknownParameterFails) {
    FeatureTree tree;
    tree.addFeature(FeatureFactory::create("box", "Box001"));
    CommandManager mgr;
    // execute 返回 false = 失败；失败命令不入栈，参数未被改动
    EXPECT_FALSE(mgr.execute(std::make_unique<ModifyParameterCommand>(tree, "Box001", "radius", 10.0)));
    EXPECT_EQ(mgr.undoCount(), 0u);
    EXPECT_DOUBLE_EQ(tree.find("Box001")->parameters()[2].asDouble(), 30.0);  // height 仍是旧值
}

TEST(CommandManagerTest, DeleteRefusedWhenHasDependents) {
    FeatureTree tree;
    tree.addFeature(FeatureFactory::create("box", "Sketch001"));
    tree.addFeature(FeatureFactory::create("box", "Extrude001"), {"Sketch001"});
    CommandManager mgr;
    // 有下游 → 拒绝删除：execute 返回 false，特征还在，命令不入栈
    EXPECT_FALSE(mgr.execute(std::make_unique<DeleteFeatureCommand>(tree, "Sketch001")));
    EXPECT_NE(tree.find("Sketch001"), nullptr);
    EXPECT_EQ(mgr.undoCount(), 0u);
}

TEST(CommandManagerTest, DeleteUndoRestores) {
    FeatureTree tree;
    tree.addFeature(FeatureFactory::create("box", "Leaf001"));
    CommandManager mgr;
    mgr.execute(std::make_unique<DeleteFeatureCommand>(tree, "Leaf001"));
    EXPECT_EQ(tree.find("Leaf001"), nullptr);     // 已删除
    EXPECT_TRUE(mgr.undo());
    EXPECT_NE(tree.find("Leaf001"), nullptr);     // 撤销 → 回来
    EXPECT_TRUE(mgr.redo());
    EXPECT_EQ(tree.find("Leaf001"), nullptr);
}

TEST(CommandManagerTest, SequenceUndoRedo) {
    // 连续操作：加 A → 加 B → 撤销 ×2 → 重做 ×2
    FeatureTree tree;
    CommandManager mgr;
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "A")));
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "B")));
    EXPECT_TRUE(mgr.undo());
    EXPECT_TRUE(mgr.undo());
    EXPECT_EQ(tree.size(), 0u);
    EXPECT_TRUE(mgr.redo());
    EXPECT_TRUE(mgr.redo());
    EXPECT_EQ(tree.size(), 2u);
}
```

---

## 6. 接入工程（src/CMakeLists.txt 加 4 行）

```cmake
add_library(forgecore
    core/Version.cpp
    geometry/ShapeFactory.cpp
    domain/BoxFeature.cpp
    domain/FeatureFactory.cpp
    domain/DependencyGraph.cpp
    domain/FeatureTree.cpp
    application/CommandManager.cpp        # ← 新增
    application/AddFeatureCommand.cpp     # ← 新增
    application/DeleteFeatureCommand.cpp  # ← 新增
    application/ModifyParameterCommand.cpp# ← 新增
)
```

然后构建 + 测试：
```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

**验收：到这一步，你手里是 Day2+3+4 的全部测试（6+17+9=32 个）全绿。**

---

## 7. 自己试试（控制台演示，感受一下"后悔药"）

```cpp
// 任意临时 main 里粘这段跑跑看
#include <iostream>
#include "domain/FeatureTree.h"
#include "domain/FeatureFactory.h"
#include "application/CommandManager.h"
#include "application/AddFeatureCommand.h"
#include "application/ModifyParameterCommand.h"

using namespace forge::domain;
using namespace forge::application;

int main() {
    FeatureTree tree;
    CommandManager mgr;
    mgr.execute(std::make_unique<AddFeatureCommand>(tree, FeatureFactory::create("box", "Box001")));
    std::cout << "添加后 size = " << tree.size() << "\n";          // 1
    mgr.execute(std::make_unique<ModifyParameterCommand>(tree, "Box001", "height", 60.0));
    std::cout << "改高后 height = " << tree.find("Box001")->parameters()[2].asDouble() << "\n";  // 60
    mgr.undo();
    std::cout << "Ctrl+Z 后 height = " << tree.find("Box001")->parameters()[2].asDouble() << "\n"; // 30
    mgr.undo();
    std::cout << "再 Ctrl+Z 后 size = " << tree.size() << "\n";     // 0
    mgr.redo();
    std::cout << "Ctrl+Y 后 size = " << tree.size() << "\n";        // 1
}
```

---

## 8. 今晚面试复盘 —— 12 个必答问题

1. **Undo/Redo 的实现方案有哪些？**（快照 / 逆操作 / Command 命令栈——讲清各自代价）
2. **双栈和"单栈+游标"的区别？**（双栈天然处理"撤销后新操作清空 redo"；游标方案还要额外维护截断）
3. **Command 把操作对象化的本质收益？**（可存储、可逆、可延迟执行、可记录日志、可序列化）
4. **AddFeatureCommand 的所有权怎么流转？**（调用方→命令→树，undo 时树→命令，redo 再→树；unique_ptr 独占）
5. **新命令为什么清空 redo 栈？**（历史分叉：新操作后"被撤销的未来"作废）
6. **失败的命令为什么不入栈？**（否则 undo 会去撤销一个根本没生效的操作，状态错乱）
7. **DeleteFeatureCommand 为什么拒绝删除有下游的特征？**（级联删除不可完美撤销；真实 CAD 也要求先删依赖）
8. **ModifyParameterCommand 为什么记旧值而不是重放？**（逆操作代价小；重放会膨胀且依赖历史完整）
9. **undo 栈无上限会怎样？**（内存膨胀；真实 CAD 设上限如 100 步，超出丢最旧的——vector 的 erase(begin)）
10. **redo 时命令再次失败怎么办？**（真实工程要处理：弹栈丢弃或提示；教学版可留作思考题）
11. **undo() 抛异常怎么办？**（异常安全：先取状态再修改；Day 5 错误模型会讲 Result/Error 方案）
12. **Qt 里 Ctrl+Z 怎么接？**（QAction + QKeySequence::Undo 快捷键 → 调 mgr.undo()；W4 实现）

---

## ✅ Day 4 是否达标自检表

- [ ] 不看图能画出双栈的 6 个状态
- [ ] CommandManager 敲完（execute/undo/redo 逻辑讲清）
- [ ] 3 个具体命令敲完，**所有权流转能画出来**
- [ ] 9 个测试全绿（累计 32 个）
- [ ] 12 个面试题不看资料讲完

> 全勾上，**W2 核心完成**！明天进入 W3：**3D 数学 + OCCT 几何内核**——`rebuild()` 从"文本占位"升级为"真生成 OCCT Shape"，然后做布尔、做分析，ForgeCAD 开始变硬核。

---

## 📎 挑战题（可选，做了面试加分）：Feature reorder 基础

给 `FeatureTree` 加"显示顺序"（插入序），实现 `moveUp(id)` / `moveDown(id)`：
- 显示顺序是用户看到的树顺序；**重建顺序永远由依赖图拓扑序决定**（两者可以不同）
- 移动前校验：换位后若破坏依赖关系（比如把 Extrude 移到它的 Sketch 前面）→ 返回 false 拒绝
- 提示：显示顺序用 `std::vector<std::string> order_` 维护；校验依赖 = 检查被移动节点与邻居的依赖关系

做不出来不影响 W2 验收，但这是"工程细节"级加分题，做出来面试可以主动讲。
