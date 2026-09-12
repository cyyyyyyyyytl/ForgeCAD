#pragma once

namespace forge::application {

// Command 是所有“可撤销操作”的统一接口。
class Command {
public:
    // 通过基类指针销毁派生命令时，虚析构保证派生类资源被正确释放。
    virtual ~Command() = default;

    // 执行操作，例如创建特征或者修改参数。
    virtual void execute() = 0;

    // 撤销 execute() 造成的修改。
    virtual void undo() = 0;
};

} // namespace forge::application
