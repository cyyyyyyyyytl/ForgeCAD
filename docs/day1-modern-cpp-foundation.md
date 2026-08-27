# Day 1 — 现代 C++ 核心 + 工程化起步

> 目标：把 ForgeCAD 的 C++ 地基打牢。今天**只**学对象生命周期和资源管理，**不碰** Qt / OCCT / OpenGL / AI / 网络 / 数据库。
>
> 验收标准（睡觉前应全部做到）：
> ✓ 解释 RAII　✓ 解释 Rule of 5　✓ 会用三大智能指针　✓ 解释 move　✓ 解释 vector 扩容　✓ 写多态 Shape　✓ 用 unique_ptr 管理 Shape　✓ 用 CMake 构建　✓ ForgeCAD 第一版跑起来　✓ 不看资料回答末尾 15 题

---

## 1. RAII —— 资源获取即初始化

**一句话**：把资源（内存、文件、锁、网络）绑定到一个**对象的生命周期**上。对象创建时拿资源，对象销毁时（析构函数）自动释放资源。

> **先别懵：RAII 不是一段"要你写"的代码，它是一种"设计类的原则"。**
> 标准库已经帮你封装好了（`std::ofstream`、`std::lock_guard`、`std::unique_ptr` 内部就是按 RAII 设计的类），你作为使用者**直接拿来用、不用理会实现细节**；
> 只有当你**自己写一个管理资源的类**时，才需要自己实现 RAII——那就要写析构函数去释放资源，这就是第 2 节的 Rule of 3/5。

**为什么重要**：只要你忘了 `delete` / 忘了 `close`，RAII 不让你忘——因为析构函数**一定**会执行（离开作用域就一定执行）。

```cpp
#include <fstream>
#include <mutex>
#include <iostream>

// 例 1：文件 —— 用 RAII，不用手动 close
void writeFile() {
    std::ofstream file("data.txt");   // 构造时打开文件（获取资源）
    file << "hello";
    // 函数结束，file 析构，自动 close()。忘不了。
}

// 例 2：mutex —— 用 std::lock_guard 自动加锁/解锁
std::mutex g_mutex;
void increment(int& counter) {
    std::lock_guard<std::mutex> lock(g_mutex);  // 构造时 lock（获取资源）
    ++counter;                                  // 临界区
    // lock 析构时自动 unlock()，即使中间抛异常也会解锁！
}

// 对比：如果不 RAII，你要自己 close / unlock，
// 一旦有早退 return 或抛异常，就直接泄漏。RAII 就是"不用我管"。

int main() {
    writeFile();
    std::cout << "RAII 示例完成\n";
    return 0;
}
```

**用一句话记牢**：*"离开作用域 = 自动释放，天生防泄漏。"*

---

## 2. Rule of 0 / 3 / 5

当一个类管理**动态资源**（`new`/裸指针）时，你需要正确实现**析构 / 拷贝构造 / 拷贝赋值 / 移动构造 / 移动赋值**。

### Rule of 0（能没有就没有）
如果类**不**管理资源（成员都是 `std::vector`、`std::string` 这类 RAII 类型），就什么都别写——编译器自动生成的默认版本就够好。

```cpp
// Rule of 0：不写任何析构/拷贝/移动，全靠成员自己管
class Person {
    std::string name_;          // string 自己管理字符内存
    std::vector<int> scores_;   // vector 自己管理动态数组
public:
    Person(std::string name) : name_(std::move(name)) {}
};
```

### Rule of 5（自己管理资源时才写）
下面的 `Buffer` 自己管理 `new[]` 内存，所以 5 个都该考虑：

```cpp
#include <utility>
#include <algorithm>

class Buffer {
    int* data_;
    std::size_t size_;
public:
    // 构造：获取资源
    Buffer(std::size_t n = 0) : data_(n ? new int[n] : nullptr), size_(n) {}

    // ① 析构：释放资源（RAII 的核心）
    ~Buffer() { delete[] data_; }

    // ② 拷贝构造：深拷贝
    Buffer(const Buffer& other)
        : data_(other.size_ ? new int[other.size_] : nullptr),
          size_(other.size_) {
        std::copy(other.data_, other.data_ + other.size_, data_);
    }

    // ③ 拷贝赋值：先释放旧的，再深拷贝（注意自我赋值）
    Buffer& operator=(const Buffer& other) {
        if (this != &other) {            // 防止 a = a
            delete[] data_;
            data_ = other.size_ ? new int[other.size_] : nullptr;
            size_ = other.size_;
            std::copy(other.data_, other.data_ + other.size_, data_);
        }
        return *this;
    }

    // ④ 移动构造：把 other 的资源"偷"过来，other 置空
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // 让 other 变成"空壳"，其析构执行 delete[] nullptr，安全
        other.size_ = 0;
    }

    // ⑤ 移动赋值：先清空自己，再偷 other 的
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
};
```

> **练习**：把这个手写 `Buffer` 一定自己敲一遍，然后用 `std::vector<int>` 或 `std::unique_ptr<int[]>` 重写它——你会发现原来要写 5 个函数，换成 RAII 成员后一个都不用写（Rule of 0 现身）。

---

## 3. 智能指针

### std::unique_ptr —— 独占所有权
**一个对象只有一个 owner**，不能复制。被拷贝资源的语义说不通（两个 owner 谁负责释放？），所以 `unique_ptr` 的拷贝构造/赋值被**删除**。

```cpp
#include <memory>
#include <vector>

class Shape { /* 多态基类 */ };

void demo() {
    auto p = std::make_unique<Shape>();  // 优先用 make_unique
    // std::unique_ptr<Shape> p2 = p;     // ❌ 编译错误：不能复制
    auto p3 = std::move(p);               // ✅ 只能移动，所有权转移给 p3
    if (!p) { /* p 已经是空的了 */ }
}
```

**为什么不能复制？** 复制需要两个对象都能访问/释放，会产生**双 delete** 或所有权混乱。移动（转移所有权）就安全。

### std::shared_ptr —— 共享所有权（引用计数）
多个对象共享同一资源，**内部有一个引用计数**，计数归零才释放。

```cpp
auto a = std::make_shared<Shape>();
auto b = a;        // ✅ 可以复制，引用计数 1→2
auto c = a;        // 计数→3
// a,b,c 都销毁后计数归 0，资源才释放。谁最后走谁负责删。
```

**注意**：shared_ptr 的引用计数**保存在堆上的一个控制块里**（和对象分开或一起），所以拷贝时两个指针指向同一个控制块。

### std::weak_ptr —— 观察者，不增加引用计数，破解循环引用
**问题**：如果两个对象用 shared_ptr 互相指向对方，计数永远不会归 0 → **内存泄漏（循环引用）**。weak_ptr 就像"弱引用/旁观者"，**不增加计数**，用来打破环。

```cpp
struct Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node>   parent;   // 用 weak 防止 Node 互相引用死锁
};
```

`weak_ptr` 需要访问资源时先 `lock()` 临时升级成 shared_ptr：

```cpp
auto wp = std::weak_ptr<Shape>(a);
if (auto sp = wp.lock()) {   // 若 a 还活着，sp 获得一个 shared_ptr 副本
    // 用 sp 访问
} else {
    // a 已经被释放了
}
```

### 什么时候**不该**用 shared_ptr
- 能用 `unique_ptr` 就绝不用 `shared_ptr`（共享有开销、语义重）。
- 如果资源只归一个对象所有 → 用 `unique_ptr`。
- 如果只是想给别人看、不拥有 → 用**裸引用/裸指针**或 `weak_ptr`。
- **面试高频**：默认优先 `unique_ptr`，只有明确"多个所有者"才 `shared_ptr`。

---

## 4. 左值 / 右值 + 移动语义

### 左值 vs 右值（一句话）
- **左值（lvalue）**：有名字、能取地址的东西。`int a;` 里的 `a`。
- **右值（rvalue）**：临时对象、字面量。`42`、`a + b` 的结果。

```cpp
int a = 10;      // a：左值
int b = a + 20;  // a+20 是右值（临时）
```

### T&&、std::move、std::forward
- `T&&`：右值引用，能**绑定右值**。
- `std::move(x)`：把 x **标记为右值**（不搬数据，只是类型转换，告诉编译器"你可以偷走 x 的资源"）。`x` 本身没被移动，是**接收移动操作的构造函数/赋值在偷**。
- `std::forward`：用在**完美转发**（模板里把"左值传成左值、右值传成右值"），Day 2 泛型部分再细说。

```cpp
Buffer a(100);              // a 拥有一块 100 个 int 的内存
Buffer b(std::move(a));     // 调用移动构造：把 a 的内存偷给 b，a 变空
                            // 比拷贝快得多——没复制 100 个 int，只是"改了指针"
```

### 为什么移动比拷贝快？
- **拷贝**：把整块数据复制一份 → 大对象很贵。
- **移动**：把"指向数据的指针/句柄"搬走，旧对象置空 → **O(1)**，跟数据量无关。

### 为什么 vector 扩容需要移动语义？
vector 扩容时要把旧数组的元素"搬到"新数组。如果有移动构造，就**偷**（快）；只有拷贝构造就只能**复制**（慢）。这也是为什么给类加移动构造能显著提速。

```cpp
#include <vector>
std::vector<Buffer> v;
v.reserve(2);
v.emplace_back(Buffer(1000));   // 满
v.emplace_back(Buffer(2000));   // 满，第三次/第二次扩容时要用移动构造把旧元素转移
```

---

## 5. const / 引用 / 指针（面试高频但易错）

**四个 const 位置要分清**：

```cpp
const int* a;       // 指向 const int：a 指向的值不能改，但 a 本身可以改指向
int* const b;       // 指向 int 的 const 指针：b 本身不能改，但指向的值可改
const int* const c; // 两者都不能改
int* d;             // 都没 const：都能改
```

**记忆口诀**：`const` 修饰它**右边紧邻**的东西（从右往左读）。
- `const int* p`：p 指向 `const int` → **值**不能改。
- `int* const p`：p 是 `const` 指针 → **指针本身**不能改。

**const 引用 vs 右值引用**：

```cpp
void f(const std::string& s) { /* 接受左值，也接受临时对象，且不拷贝 */ }
void f(std::string&& s)      { /* 只接受右值（临时对象），常配合移动 */ }
// 能用 const T& 就用 const T&（读写都通、零拷贝）
```

---

## 6. vector —— 当成重点对象研究

### 核心接口
```cpp
#include <vector>
std::vector<int> v;

v.size();        // 当前元素个数
v.capacity();    // 已分配能存多少个（>= size）
v.reserve(100);  // 预留容量，避免反复扩容（预分配）
v.resize(50);    // 把 size 设成 50（多出的就默认构造/填充）
v.push_back(1);  // 在末尾添加
v.emplace_back(2); // 就地构造，比 push_back 少一次拷贝/移动
```

### vector 为什么扩容？
vector 是**连续内存**，容量只在需要时扩展。扩容策略通常**倍增**（×1.5 或 ×2）来摊薄拷贝成本（均摊 O(1)）。

### 扩容后原来的 iterator / pointer / reference 会怎样？
**全部失效！** 因为扩容要分配**新内存**、把元素拷/移动到新地址、释放旧内存——旧地址没了。

```cpp
std::vector<int> v = {1,2,3};
int* p = &v[1];      // 记下地址
v.push_back(42);     // 可能触发扩容 → 内存搬走了
// *p 现在可能读取到野内存！所以不要在扩容前长期保存迭代器/指针
```

**规避**：`v.reserve(...)` 提前预留，扩容期间就不同步失效。

### 为什么 vector 通常比 list 快？
- **缓存友好**：vector 元素连续排列，CPU 高速缓存命中率高。
- **随机访问 O(1)**：`v[i]` 直接算地址。
- list 分散在内存各处，遍历慢、`[]` 慢。所以"能用 vector 就用 vector"。

---

## 7. 把知识用进项目 —— ForgeCAD 第一版骨架

创建一个最小的多态 Shape 并用 `unique_ptr` 管理（这正是你 Day 1 要达成"写一个简单多态 Shape"）。

### 目录（你学习路线里建议的结构简化版）
```
ForgeCAD/
├── CMakeLists.txt
├── src/
│   ├── core/        # 核心基础（之后放 Result/Error/Logger）
│   ├── model/       # 领域模型（之后放 Feature/Body）
│   ├── geometry/    # 几何（之后放 Boolean/Transform）
│   └── main.cpp
├── include/
├── tests/
└── README.md
```

我们已经帮你建好并验证过的实际工程在 `D:\ForgeCAD`。这里是教学用的**最小版**，你今天亲手敲一遍。

### shape.h —— 多态基类
```cpp
#pragma once
// 多态基类：虚函数 + 虚析构
class Shape {
public:
    virtual ~Shape() = default;   // 必须虚析构，否则通过基类指针 delete 派生类不会释放派生部分
    virtual void build() = 0;     // 纯虚函数 → 抽象类，不能直接实例化
    virtual double volume() const = 0;
};
```

### box.h / box.cpp —— 派生类
```cpp
// box.h
#pragma once
#include "shape.h"
class Box : public Shape {
public:
    explicit Box(double l, double w, double h) : l_(l), w_(w), h_(h) {}
    void build() override;
    double volume() const override { return l_ * w_ * h_; }
private:
    double l_, w_, h_;
};

// box.cpp
#include "box.h"
#include <iostream>
void Box::build() {
    // 这里之后接 OCCT 的 BRepPrimAPI_MakeBox 真正生成几何体，
    // 今天先用体积大概意思一下"建造完成"。
    std::cout << "Box built, volume = " << volume() << "\n";
}
```

### main.cpp —— 用 unique_ptr 管理多态
```cpp
#include <memory>
#include "shape.h"
#include "box.h"

int main() {
    // unique_ptr<Shape> 指向派生类 Box → 多态
    auto shape = std::make_unique<Box>(10.0, 5.0, 3.0);
    shape->build();                 // 调用 Box::build（多态）
    std::cout << shape->volume() << "\n";   // 150

    // unique_ptr<Shape> 不能复制，只能移动（所有权转移）
    std::unique_ptr<Shape> another = std::move(shape);
    if (!shape) { /* shape 已是空 */ }
    return 0;  // another 析构 → 通过虚析构正确释放 Box → 完美（RAII）
}
```

---

## 8. CMake —— 把工程编起来

今天掌握 6 个命令，让 `cmake -S . -B build` + `cmake --build build` 编译成功。

### CMakeLists.txt（顶层）
```cmake
cmake_minimum_required(VERSION 3.20)          # 最低 CMake 版本

project(ForgeCAD LANGUAGES CXX)               # 项目名 + 语言

# 需要 C++20（Day 2 起可用 C++17 也行）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 把"可执行文件"加进工程
add_executable(forgecad
    src/main.cpp
    src/geometry/box.cpp
)

# 告诉编译器去哪找头文件（比如 #include "shape.h"）
target_include_directories(forgecad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include   # include/shape.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/geometry
)

# 以后如果要链接第三方库/自己写的库，用它：
# target_link_libraries(forgecad PRIVATE 某个库)
```

### 编译三步曲（在项目根目录）
```powershell
cmake -S . -B build     # ① 配置：生成构建系统          （-S 源码目录，-B 构建目录）
cmake --build build      # ② 编译+链接：产出可执行文件
.\build\Debug\forgecad.exe   # ③ 运行
```

> 注意：ForgeCAD 实际工程因接了 Qt/OCCT/vcpkg，需要额外 `-DCMAKE_TOOLCHAIN_FILE` 和 `-DCMAKE_PREFIX_PATH`（见 `D:\ForgeCAD\README.md`）。今天教学的纯 C++ 版不需要。

> **💡 中文注释与 MSVC 编码（今天就会遇到的真实问题）**：
> 如果你的代码里有中文注释，而文件是 UTF-8 编码（VS Code 默认），MSVC 默认却按 GBK 读取源码，会报 `C4819` 警告甚至语法错乱。
> **解决办法**：在你的 CMakeLists 里加两行（ForgeCAD 主工程我已经加好了）：
> ```cmake
> if(MSVC)
>     add_compile_options(/utf-8)   # 告诉 MSVC：源码是 UTF-8
> endif()
> ```

---

## 9. 今晚面试复盘 —— 15 个必答问题

> 要求：**不看资料**，对每个问题连续讲 2~3 分钟。答案提示在括号里，先自己讲再看。

1. **什么是 RAII？**（资源获取即初始化，生命周期绑定对象生命周期，析构自动释放）
2. **为什么需要智能指针？**（应对手动 new/delete 泄漏、异常安全，自动管理所有权）
3. **unique_ptr 和 shared_ptr 区别？**（独占 vs 共享；可否复制）
4. **weak_ptr 解决什么问题？**（循环引用导致 refcount 不归零；观察不拥有）
5. **什么情况下不应该用 shared_ptr？**（单所有权用 unique；只观察用引用/weak）
6. **什么是左值和右值？**（有名字能取地址 vs 临时对象）
7. **std::move 做了什么？**（类型转换标记为右值，使移动操作可被编译器选中；本身不搬数据）
8. **移动构造为什么比拷贝构造快？**（偷指针 O(1) vs 复制整块数据）
9. **vector 的 size 和 capacity 区别？**（有效元素数 vs 已分配容量）
10. **vector 扩容发生什么？**（新分配更大内存→拷贝/移动→释放旧）
11. **reserve 和 resize 区别？**（reserve 改容量不建元素；resize 改 size 并填充）
12. **vector 扩容后 iterator 为什么可能失效？**（内存搬到新地址，旧指针指向已释放区域）
13. **const int* 和 int* const 区别？**（const 修饰值 vs 修饰指针本身）
14. **虚函数为什么需要虚析构？**（否则 delete 基类指针不调用派生析构→泄漏）
15. **什么是多态？**（同接口不同实现，运行时绑定，虚函数/vtable）

---

## ✅ Day 1 是否达标自检表

自己打勾：
- [ ] 能脱口解释 RAII（不看资料）
- [ ] 能解释 Rule of 0/3/5 并手写 Buffer
- [ ] 熟练用 unique_ptr/shared_ptr/weak_ptr 并说清区别
- [ ] 解释 move 为什么快、vector 扩容为什么需要移动
- [ ] 分清 4 种 const 指针和 const T& / T&&
- [ ] 说出 vector 扩容、失效、reserve/resize
- [ ] 手写多态 Shape + Box + unique_ptr 管理
- [ ] CMakeLists 6 命令，能 `cmake -S . -B build && cmake --build build`
- [ ] ForgeCAD 第一版工程编译、运行通过
- [ ] 15 个面试题不看资料回答完

> 全勾上，Day 1 成功！明天进入：**STL + 模板 + 泛型编程 + 设计模式 + ForgeCAD 的 Feature/Command 架构**。
