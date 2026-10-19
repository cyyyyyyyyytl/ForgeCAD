#pragma once // Command 被历史栈和具体命令共同包含，只定义一次。

#include <string> // label() 返回可读名称，例如“修改 Box001.length”。

namespace forge::application {

class ModelDocument;

// ============================================================
// Command：一条可撤销的模型修改（命令模式）
// ------------------------------------------------------------
// 旧实现每次修改前把整份文档（全部 Feature 参数 + 依赖图）复制进历史栈，
// Undo 时再整体重建所有 Feature。命令模式改为“只记录这次操作改了什么”：
//   · ModifyParameterCommand 只保存 1 个参数名和旧值/新值；
//   · CreateFeatureCommand 保存新建对象的 unique_ptr 和依赖 ID；
//   · DeleteFeatureCommand 保存被删对象的 unique_ptr、原下标和依赖。
// 因此历史占用的内存与“改动的数据量”成正比，而不是与文档大小成正比。
//
// execute() 与 undo() 必须严格对称：
//   · 首次执行和 Redo 都调用 execute()；
//   · Undo 调用 undo()；
//   · 两者都只能修改自己负责的数据，不允许整文档重建。
// 调用顺序由 CommandManager 按 LIFO 保证，命令内部可以假定“执行时所处的
// 文档状态”正是自己上次离开时的状态。
// ============================================================
class Command {
public:
    virtual ~Command() = default;

    // 应用这条修改（首次执行与 Redo 共用）。
    virtual void execute(ModelDocument& document) = 0;
    // 撤销这条修改，必须恢复到 execute() 之前的状态。
    virtual void undo(ModelDocument& document) = 0;
    // 人类可读的操作名，供状态栏、测试失败信息和后续历史面板使用。
    virtual std::string label() const = 0;
};

} // namespace forge::application
