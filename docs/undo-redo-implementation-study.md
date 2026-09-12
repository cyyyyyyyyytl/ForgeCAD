# ForgeCAD Undo/Redo 实现学习笔记

> 对应代码版本：`24d66cd`、`6a44357`  
> 建议学习时间：分 3 次阅读，每次 30～45 分钟。  
> 目标：不只会使用 Ctrl+Z/Ctrl+Y，还能讲清命令模式、双栈和 `unique_ptr` 所有权流转。

---

## 1. 这次实现了什么

本轮把 ForgeCAD 的两种真实操作变成了可撤销命令：

1. 创建 Box、Cylinder、Sphere；
2. 修改 Feature 的数值参数。

用户现在可以：

```text
创建 Box
→ 修改 length
→ Ctrl+Z：撤销参数修改
→ Ctrl+Z：撤销 Box 创建
→ Ctrl+Y：恢复 Box
→ Ctrl+Y：恢复参数修改
```

主要代码位置：

```text
src/application/Command.h
src/application/CommandManager.h/.cpp
src/application/ModifyParameterCommand.h/.cpp
src/application/CreateFeatureCommand.h/.cpp
src/application/ModelDocument.h/.cpp
src/application/ModelingService.h/.cpp
src/ui/mainwindow.h/.cpp/.ui
```

对应测试：

```text
tests/test_command_manager.cpp
tests/test_modify_parameter_command.cpp
tests/test_create_feature_command.cpp
tests/test_model_document.cpp
tests/test_modeling_service.cpp
```

---

## 2. 先用两个盒子理解 Undo/Redo

先不要想 C++。想象桌上有两个盒子：

```text
Undo 盒子：已经做过、现在可以反悔的操作
Redo 盒子：已经反悔、现在可以重做的操作
```

把 Box 的长度从 50 改为 100 时，程序创建一张命令卡：

```text
目标：Box001
参数：length
旧值：50
新值：100
```

执行后：

```text
模型：length = 100
Undo：[修改长度命令]
Redo：[]
```

按 Undo：

```text
从 Undo 拿出命令
→ 调用命令的 undo()，恢复旧值 50
→ 把命令放进 Redo
```

结果：

```text
模型：length = 50
Undo：[]
Redo：[修改长度命令]
```

按 Redo：

```text
从 Redo 拿出命令
→ 再次调用命令的 execute()，写入新值 100
→ 把命令放回 Undo
```

结果：

```text
模型：length = 100
Undo：[修改长度命令]
Redo：[]
```

记忆口诀：

```text
Undo：Undo 栈 → undo() → Redo 栈
Redo：Redo 栈 → execute() → Undo 栈
```

### 为什么新操作必须清空 Redo

假设执行了 A、B、C，然后撤销 C：

```text
当前历史：A → B
可以重做：C
```

此时用户没有重做 C，而是执行了新操作 D：

```text
新历史：A → B → D
```

C 属于已经放弃的旧路线，必须清空。否则程序会把两个不同未来混在一起。

---

## 3. Command：所有可撤销操作的共同规则

`Command.h` 中的核心接口：

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
};
```

### `execute()` 是什么

执行操作本身：

```text
修改参数命令：写入新值
创建命令：把 Feature 加入 Document
```

Redo 不需要单独的 `redo()` 虚函数，因为重做就是再次调用 `execute()`。

### `undo()` 是什么

执行相反操作：

```text
修改参数命令：恢复旧值
创建命令：从 Document 取回 Feature
```

### 为什么析构函数是 `virtual`

`CommandManager` 保存的是：

```cpp
std::unique_ptr<Command>
```

但实际对象可能是：

```cpp
ModifyParameterCommand
CreateFeatureCommand
```

通过基类指针销毁派生类对象时，虚析构保证派生类资源也被正确释放。

### 为什么 `execute()` 和 `undo()` 后面有 `= 0`

`= 0` 表示纯虚函数。`Command` 只规定共同接口，不知道具体应该修改哪个参数或创建哪个 Feature，因此不能直接实例化。

---

## 4. CommandManager：两个 vector 组成的栈

核心成员：

```cpp
std::vector<std::unique_ptr<Command>> undoStack_;
std::vector<std::unique_ptr<Command>> redoStack_;
```

虽然类型是 `vector`，但只使用末尾：

```cpp
push_back() // 压入栈顶
back()      // 访问栈顶
pop_back()  // 弹出栈顶
```

所以它表现的仍然是后进先出的栈。

### 为什么没有直接使用 `std::stack`

当前 Undo/Redo 用两者都能实现，但 ForgeCAD 以后可能需要：

- 展示命令历史；
- 限制最多保存 100 步；
- 删除最旧记录；
- 调试并查看历史顺序。

`vector` 可以遍历、`clear()`，也可以删除最旧元素，更方便扩展。当前 `push_back()` 和 `pop_back()` 仍是摊还 O(1)。

### 执行新命令

```cpp
void CommandManager::executeCommand(std::unique_ptr<Command> command)
{
    if (!command) {
        throw std::invalid_argument("不能执行空命令");
    }

    command->execute();
    undoStack_.push_back(std::move(command));
    redoStack_.clear();
}
```

顺序不能随便理解：

1. 先执行命令；
2. 成功后才进入 Undo 栈；
3. 新操作使旧 Redo 历史失效。

### Undo 中的所有权搬运

```cpp
auto command = std::move(undoStack_.back());
undoStack_.pop_back();
command->undo();
redoStack_.push_back(std::move(command));
```

可以把它理解成：

```text
move：从盒子里拿走命令卡
pop_back：删掉拿走后留下的空卡位
```

`std::move` 不会缩短 vector，也不会自动删除 vector 元素。移动后原来的 `unique_ptr` 变成空指针，但那个位置仍然存在，所以还要 `pop_back()`。

Redo 是完全相反的搬运过程。

---

## 5. ModifyParameterCommand：记住旧值和新值

核心数据：

```cpp
domain::Feature& feature_;
std::string parameterName_;
double oldValue_;
double newValue_;
```

命令卡相当于：

```text
目标对象：Box001
参数名字：length
旧值：50
新值：100
```

### 为什么 Feature 使用引用

Feature 真正由 `ModelDocument` 拥有。参数命令只是操作它，不应该和 Document 争夺所有权，因此保存：

```cpp
domain::Feature& feature_;
```

### 为什么必须保存 `oldValue_`

Undo 发生时，Feature 已经是新值。没有提前记录旧值，命令就不知道应该恢复成什么。

### 为什么也要保存 `newValue_`

第一次 execute 和 Redo 都需要写入新值：

```cpp
void ModifyParameterCommand::execute()
{
    feature_.setParameter(parameterName_, newValue_);
}

void ModifyParameterCommand::undo()
{
    feature_.setParameter(parameterName_, oldValue_);
}
```

### 非法值为什么在构造阶段拒绝

命令通过 `FeatureCatalog` 查询参数规则：

```text
Box.length：最小 1，最大 10000
```

如果用户尝试设置 `-5`：

```text
构造 ModifyParameterCommand
→ 发现 -5 越界
→ 抛出异常
→ executeCommand 尚未开始
→ Feature 保持原值
→ Undo 栈也不会增加失败命令
```

---

## 6. CreateFeatureCommand：所有权在命令和文档之间移动

参数修改命令只引用 Feature，而创建命令必须暂时拥有 Feature：

```cpp
ModelDocument& document_;
std::unique_ptr<domain::Feature> feature_;
std::string featureId_;
bool executed_ = false;
```

### 执行前

```text
CreateFeatureCommand 拥有 Feature
ModelDocument 中没有这个 Feature
```

### execute

```cpp
document_.addFeature(std::move(feature_));
```

```text
CreateFeatureCommand 的 feature_ 变成 nullptr
ModelDocument 获得 Feature 所有权
```

### undo

```cpp
feature_ = document_.removeFeature(featureId_);
```

```text
ModelDocument 失去 Feature 所有权
CreateFeatureCommand 把 Feature 暂时拿回来
```

### redo

CommandManager 再次调用 `execute()`：

```text
CreateFeatureCommand → ModelDocument
```

同一个堆对象来回移动，没有重复创建，也不会被两个 `unique_ptr` 同时拥有。

### 为什么必须单独保存 `featureId_`

execute 后：

```cpp
feature_ == nullptr
```

此时不能再调用 `feature_->id()`。所以命令必须在移动之前记住 ID，Undo 才知道从 Document 取回谁。

---

## 7. ModelDocument::removeFeature

过去 Document 只能接收所有权：

```cpp
addFeature(std::unique_ptr<Feature>)
```

为了支持撤销创建，现在还需要把所有权交出去：

```cpp
std::unique_ptr<Feature> removeFeature(std::string_view id);
```

核心过程：

```cpp
auto removedFeature = std::move(*it);
features_.erase(it);
return removedFeature;
```

这里和 Undo 栈的移动很像：

```text
std::move(*it)：拿走 Feature 所有权
erase(it)：删除 vector 中留下的空位置
return：把所有权交给调用方
```

按 ID 查找后使用 `erase(it)`，是因为目标不一定在 vector 末尾，不能使用 `pop_back()`。

---

## 8. ModelingService：让 UI 和 AI 共用命令历史

外部入口不直接创建命令，也不直接碰 `CommandManager`：

```text
Qt 属性面板 ─┐
              ├→ ModelingService → CommandManager → Command → Feature
AI Tool Call ─┘
```

这样有三个好处：

1. UI 和 AI 使用同样的参数校验；
2. UI 和 AI 产生的操作都能被 Ctrl+Z 撤销；
3. 将来更换内部历史实现时，外部入口不需要一起修改。

参数修改现在会创建命令：

```cpp
commandManager_.executeCommand(
    std::make_unique<ModifyParameterCommand>(
        *feature,
        std::string(parameterName),
        value));
```

创建 Feature 也会创建命令，然后再按稳定 ID 从 Document 找回引用并返回给调用方。

---

## 9. Qt 中的 Ctrl+Z / Ctrl+Y

`mainwindow.ui` 中新增两个 QAction：

```text
actionUndo：Ctrl+Z
actionRedo：Ctrl+Y
```

Qt 的自动连接规则会把它们连接到：

```cpp
on_actionUndo_triggered()
on_actionRedo_triggered()
```

槽函数不实现双栈，只调用 Service：

```text
QAction
→ MainWindow 槽
→ ModelingService::undo/redo
→ CommandManager
```

Undo/Redo 改变模型后，UI 必须同步刷新：

- 模型树；
- 属性面板；
- OCCT 三维视图；
- 当前选中的 Feature ID；
- 状态栏。

撤销最后一个创建操作后文档会变空，此时还要显式向 `Viewport3D` 传入空形状列表，否则旧模型可能继续残留在画面中。

---

## 10. 输入 50 为什么曾经记录成 5、50 两步

属性面板原来直接连接：

```cpp
QDoubleSpinBox::valueChanged
```

Qt 默认开启 keyboard tracking。键盘输入 `50` 时可能依次发生：

```text
输入字符 5  → valueChanged(5)  → 创建一张命令
输入字符 0  → valueChanged(50) → 又创建一张命令
```

于是按一次 Ctrl+Z 只会从 50 回到 5，而不是回到真正的修改前值。

修复：

```cpp
spin->setKeyboardTracking(false);
```

现在键盘输入会在按回车或输入框失去焦点时提交最终值。点击上下箭头仍会立即触发修改。

这个 Bug 的教训是：

> UI 发出的每个信号不一定都代表一次完整的用户意图。把信号直接转成历史命令前，要先理解控件的触发时机。

---

## 11. 测试在验证什么

本轮完成后共有 68 项测试通过。Undo/Redo 相关测试重点覆盖：

### CommandManager

- execute 后可以 Undo；
- Undo 后可以 Redo；
- 新命令清空 Redo；
- 空历史 Undo/Redo 不崩溃；
- 空命令被拒绝。

### ModifyParameterCommand

- 真实 Box 参数按 `50 → 100 → 50 → 100` 变化；
- 不存在的参数被拒绝；
- 越界值不改变 Feature，也不进入历史。

### CreateFeatureCommand

- execute 后 Document 找得到 Feature；
- undo 后 Document 找不到 Feature；
- redo 后恢复的是同一个对象地址；
- 空 Feature 被拒绝。

### ModelingService

- Service 创建的 Feature 可以撤销和重做；
- Service 修改的参数可以撤销和重做；
- “创建 + 修改”严格按后进先出顺序撤销；
- UI 和 AI 共享同一条命令入口。

---

## 12. 推荐学习顺序

不要一次把全部代码看完。按下面三轮学习。

### 第一轮：只理解双栈

阅读：

```text
Command.h
CommandManager.h
CommandManager.cpp
test_command_manager.cpp
```

能够回答：

1. Undo 和 Redo 两个栈分别保存什么？
2. 为什么新命令清空 Redo？
3. 为什么 move 后还要 pop_back？
4. 为什么 Redo 调用的是命令自己的 execute？

### 第二轮：理解参数命令

阅读：

```text
ModifyParameterCommand.h/.cpp
test_modify_parameter_command.cpp
```

能够回答：

1. 为什么要同时保存 oldValue 和 newValue？
2. 为什么命令只引用 Feature，不拥有它？
3. 为什么非法值在命令构造阶段拒绝？

### 第三轮：理解创建命令的所有权

阅读：

```text
CreateFeatureCommand.h/.cpp
ModelDocument::removeFeature
ModelingService.cpp
mainwindow.cpp
```

能够回答：

1. Feature 的所有权怎样在命令和 Document 之间移动？
2. 为什么 execute 后还必须保存 featureId？
3. 为什么撤销创建不能直接 delete Feature？
4. 为什么 UI 和 AI 都应该经过 ModelingService？

---

## 13. 自测题

1. 连续执行 A、B、C 后，Undo 两次，两个栈分别是什么？
2. 上一步之后执行新命令 D，Redo 栈应怎样变化？
3. `unique_ptr` 为什么不能复制？
4. `std::move` 是否会自动删除 vector 中的元素？
5. 修改命令的 Undo 使用旧值还是新值？
6. Redo 为什么不需要在 Command 接口中增加一个纯虚函数？
7. 创建命令 execute 后，`feature_` 为什么为空？
8. 创建命令 Undo 后，谁拥有 Feature？
9. `removeFeature()` 为什么返回 `unique_ptr`，而不是裸指针？
10. 为什么输入 `50` 曾经产生两条历史？

建议先用自己的话回答，不要背代码。能把所有权流转画出来，就真正掌握了这一部分。

---

## 14. 当前边界与下一步

当前已经支持：

```text
创建 Feature 的 Undo/Redo
修改参数的 Undo/Redo
Qt Ctrl+Z/Ctrl+Y
AI 创建和修改操作进入同一历史
```

尚未实现：

```text
DeleteFeatureCommand
命令历史数量上限
连续微调命令合并
Feature 依赖关系下的安全删除
Undo/Redo 失败后的统一 Result/Error 模型
```

下一步建议先实现 Feature 依赖图，再做安全删除；否则删除一个被其他 Feature 依赖的对象时，很难保证模型仍然有效。
