# Day 2 — STL 实战 + 模板 + 设计模式 + ForgeCAD Feature 系统开写

> 目标：昨天你把"内存"搞懂了。今天要学的是**面试官最常考的容器与模式**，并且**开始写 ForgeCAD 最有价值的部分：参数化 Feature 系统**。
>
> 验收标准（今天结束前应全部做到）：
> ✓ 能说清 vector/list/map/unordered_map 怎么选　✓ 迭代器失效规则　✓ 会写 lambda 和 std::function　✓ 会写函数模板　✓ 说清 optional/variant 用途　✓ 说清 Factory/Command/Observer 为什么用　✓ 手写 Parameter/Feature/BoxFeature/FeatureFactory　✓ 6 个单元测试全绿　✓ 不看资料回答末尾 12 题

---

## 1. STL 容器——怎么选（面试必考）

### vector vs list vs deque：一句话版
- **vector**：连续内存。随机访问 `v[i]` O(1)、遍历缓存友好、**尾部**增删快。绝大多数情况用它。
- **list**：双向链表。**中间插入/删除**是 O(1)（不移动别的元素），但没有随机访问、每个节点单独分配内存 → 遍历慢。只有当"频繁在中间插删"才用它。
- **deque**：双端队列。头尾增删都 O(1)，能随机访问。需要**头尾都要快速插删**时用。

> **面试高频**：为什么 vector 通常比 list 快？—— 连续内存，CPU 缓存命中率高；list 节点分散在堆上，每次访问都要"跳"。

### map vs unordered_map：一句话版
- **map**：红黑树实现。**有序**（按 key 从小到大遍历），增删查 O(log n)。
- **unordered_map**：哈希表实现。**无序**，平均 O(1)，最坏 O(n)。
- **怎么选**：需要"有序遍历"、"取最大/最小 key" → map；只需要"按 key 快速查找" → unordered_map。面试官爱问这个。

### set / unordered_set
就是"只要 key 不要 value"的 map。去重、判断存在性。

### 迭代器失效（必须背下来）
| 容器 | 什么操作让迭代器失效 |
|---|---|
| vector | **扩容**（push_back 超过 capacity）→ **全部失效**；中间插入/删除 → 之后的全失效 |
| map/set | 插入不影响；**删除只会让被删的那个失效**（其余安全）|
| unordered_map | 触发 **rehash**（扩容）→ 全部失效 |

```cpp
std::vector<int> v{1,2,3};
auto it = v.begin();
v.push_back(100);      // 可能扩容 → it 失效！不能再用了
```

**规避**：提前 `v.reserve(n)`，或索引访问，或在循环里用 `erase` 返回的新迭代器：
```cpp
for (auto it = v.begin(); it != v.end(); ) {
    if (*it % 2 == 0) it = v.erase(it);   // erase 返回下一个有效迭代器
    else ++it;
}
```

### emplace_back vs push_back
- `push_back(x)`：先构造 x（临时对象），再拷贝/移动进容器。
- `emplace_back(a, b, c)`：**在容器内存里就地构造**，少一次拷贝/移动。

```cpp
std::vector<std::pair<int, int>> pts;
pts.emplace_back(1, 2);        // 直接就地构造 pair
pts.push_back({1, 2});         // 也行，但多一次移动
```

---

## 2. lambda + std::function——把"行为"当参数传

### lambda 是什么
一段**匿名函数**。语法：`[捕获] (参数) { 函数体 }`。

```cpp
#include <algorithm>
#include <vector>

std::vector<int> v{5, 2, 8, 1};

// 从小到大排序
std::sort(v.begin(), v.end());
// 自定义规则：按绝对值从大到小
std::sort(v.begin(), v.end(), [](int a, int b) {
    return std::abs(a) > std::abs(b);
});
```

### 捕获列表（面试高频）
- `[]`：不捕获，函数体只能用自己的参数。
- `[=]`：按值捕获所有外部变量（拷贝）。
- `[&]`：按引用捕获所有外部变量（不拷贝，能改外部变量）。
- `[x]` / `[&x]`：只捕获 x（值 / 引用）。
- `[this]`：捕获当前对象成员（类里写 lambda 时用）。

```cpp
int threshold = 3;
auto countBig = std::count_if(v.begin(), v.end(),
    [threshold](int x) { return x > threshold; });   // [threshold] 按值捕获
```

### std::function——"万能函数包装器"
任何"能调用"的东西（普通函数、lambda、函数对象）都能装进 `std::function`：

```cpp
#include <functional>
#include <iostream>

void hello(int n) { std::cout << "hello " << n << "\n"; }

int main() {
    std::function<void(int)> f = hello;              // 普通函数
    f = [](int n) { std::cout << "lambda " << n << "\n"; };  // 换 lambda
    f(42);
}
```

**为什么重要**：回调、事件、策略注入都靠它。Qt 的 signal/slot 思想也类似——把"要执行的行为"作为对象传来传去。ForgeCAD 的命令系统、任务回调都会用到。

---

## 3. 模板——写一份，给任意类型用

### 函数模板
```cpp
template <typename T>
T myMax(T a, T b) {
    return a > b ? a : b;
}
// 用法：
int    m1 = myMax(3, 7);        // T = int
double m2 = myMax(3.5, 2.1);    // T = double —— 同一份代码两种类型
```

**编译期生成**：编译器看到 `myMax(3, 7)` 就现场生成一份 `int` 版本，看到 `myMax(3.5, 2.1)` 再生成一份 `double` 版本。所以模板是"给编译器看的代码工厂"。

### 类模板
```cpp
template <typename T>
class Point2 {
public:
    Point2(T x, T y) : x_(x), y_(y) {}
    T x() const { return x_; }
    T y() const { return y_; }
private:
    T x_, y_;
};

Point2<int>    pi(1, 2);       // 整数点
Point2<double> pd(1.5, 2.5);   // 浮点点
```

> 面试：**模板和虚函数的区别**？—— 模板是**编译期**多态（快，无运行时开销，但每种类型都生成代码）；虚函数是**运行时**多态（一个接口，运行时决定调用谁，有 vtable 开销）。ForgeCAD 里：算法性能敏感的用模板，架构解耦的用虚函数。

### 三个马上要用的现代类型
- **std::optional\<T\>**："可能有值，也可能没有"。比 `nullptr`/`-1` 哨兵值语义清晰：
  ```cpp
  std::optional<double> maybeDivide(int a, int b) {
      if (b == 0) return std::nullopt;   // 没有值
      return static_cast<double>(a) / b; // 有值
  }
  auto r = maybeDivide(10, 0);
  if (r) { /* 有值，用 *r */ } else { /* 没值 */ }
  ```
- **std::variant\<A, B, C\>**：一个值，类型是 A/B/C 之一（类型安全的 union）。ForgeCAD 的 `ParameterValue` 就用它：
  ```cpp
  using Value = std::variant<double, int, std::string>;
  Value v = 3.14;                       // 现在是 double
  v = "hello";                          // 也可以是 string
  if (auto* d = std::get_if<double>(&v)) { /* d 指向 double */ }
  ```
- **std::string_view**："只看不拥有"的字符串（像只读引用）。函数参数传它避免拷贝。注意：**它不持有数据**，被引用的 string 死了它就失效。

---

## 4. 三个设计模式——为 ForgeCAD 而讲（不讲空话）

> 设计模式不是"背名字"，是"解决具体问题"。我只讲 ForgeCAD 里真会用到的。

### ① Factory（工厂）——"创建"与"使用"解耦
**问题**：UI 层/命令层要知道"每种 Feature 怎么构造"——Box 要 3 个参数、Cylinder 要 2 个参数……加一个新类型就要改所有调用点。
**解决**：统一说"我要一个 box"，工厂负责内部构造。
```cpp
// 调用方只写这一行，不知道 BoxFeature 内部构造：
auto f = FeatureFactory::create("box", "Box001");
```
**好处**：加 Cylinder/Extrude 只改 `FeatureFactory.cpp` 一处。

### ② Command（命令）——把"操作"封装成对象 → 才能 Undo/Redo
**问题**：用户点"删除 Box001"，如果直接改数据，**怎么撤销？**你没法记住"改之前是啥"。
**解决**：把"一次操作"封装成一个对象（含执行 + 撤销两个方法），压进一个栈：
```
用户操作 → CreateFeatureCommand / ModifyParameterCommand / DeleteFeatureCommand
                 ↓ 执行
             压入 CommandStack
Ctrl+Z → 弹栈，调用 undo()；Ctrl+Y → 再压回，调用 execute()
```
ForgeCAD 第 2 周核心就是它。**面试必问：Undo/Redo 怎么实现？答 Command 栈。**

### ③ Observer（观察者）——"变化"通知"关注者"，互不认识
**问题**：用户改了 Box 长度 → 3D 视图要刷新、模型树要刷新、状态栏要更新。如果让"改参数"的代码直接调这三个地方，就耦合死了。
**解决**：主题（Subject）只管"我变了"；观察者（Observer）自己登记"我要听"。改参数的代码**不认识**视图，视图**自动**被通知。
```cpp
// 伪代码（Qt 里就是 signal/slot）：
paramChanged.emit();   // 主题发信号
// 3D 视图、模型树各自 connect 了这个信号，收到就刷新自己
```
**好处**：加一个新界面（比如属性面板）不用动业务代码。这就是 Qt `signal/slot` 的原理，W3 你会直接用上。

### ④ Strategy（策略，顺带提）——算法可替换
"分析体积"和"分析包围盒"是不同算法，但对外统一接口 `IShapeAnalyzer`。调用方不关心具体算法，只关心"给我分析结果"。W3 几何分析模块用。

> **面试窍门**：讲设计模式**永远带场景**："ForgeCAD 里 UI 和几何引擎要解耦，所以我用 Observer/信号槽……"——不要干背定义。

---

## 5. 把知识写进 ForgeCAD：domain 模块开写 🚀

从今天起，你写的每一行代码都会成为**面试时你能讲 30 分钟的深挖点**。目录：`src/domain/`（领域模型层——你的文档规划里的"Domain Model"）。

### 要创建 5 个文件 + 1 个测试文件

```
src/domain/
├── Parameter.h          # 参数：名字 + 值（用 std::variant 实战）
├── Feature.h            # 抽象基类：所有建模操作的统一接口
├── BoxFeature.h/.cpp    # 继承 Feature：长方体特征（参数校验 + 重建）
└── FeatureFactory.h/.cpp# 工厂：按类型名创建 Feature
tests/domain_test.cpp    # 6 个单元测试
```

### 文件 1：Parameter.h —— 参数
```cpp
#pragma once
#include <string>
#include <variant>
#include <stdexcept>
#include <utility>

namespace forge::domain {

// 参数值：一个值可能是 double / int / bool / std::string 之一（variant 实战）
using ParameterValue = std::variant<double, int, bool, std::string>;

// 参数：名字 + 值。Box 的 length/width/height 就是 Parameter
class Parameter {
public:
    Parameter(std::string name, ParameterValue value)
        : name_(std::move(name)), value_(std::move(value)) {}

    const std::string& name() const { return name_; }
    const ParameterValue& value() const { return value_; }
    void setValue(ParameterValue value) { value_ = std::move(value); }

    // 取数值（double 或 int 都行）；类型不对抛异常
    double asDouble() const {
        if (auto* d = std::get_if<double>(&value_)) return *d;
        if (auto* i = std::get_if<int>(&value_)) return static_cast<double>(*i);
        throw std::runtime_error("Parameter '" + name_ + "' 不是数值类型");
    }

private:
    std::string name_;
    ParameterValue value_;
};

} // namespace forge::domain
```

### 文件 2：Feature.h —— 抽象基类
```cpp
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "Parameter.h"

namespace forge::domain {

// 参数化特征基类：Box、Cylinder、Extrude……都是它的子类。
// 这是 ForgeCAD "领域模型"的核心抽象。
class Feature {
public:
    Feature(std::string id, std::string name)
        : id_(std::move(id)), name_(std::move(name)) {}
    virtual ~Feature() = default;   // 多态基类必须有虚析构（Day1 复习！）

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }

    virtual const std::vector<Parameter>& parameters() const = 0;
    virtual void setParameter(const std::string& name, ParameterValue value) = 0;
    virtual std::string validate() const = 0;   // 合法返回空串，不合法返回原因
    virtual std::string rebuild() const = 0;    // W3 接 OCCT；现在先返回描述文本

private:
    std::string id_;      // 唯一 ID：Feature 树 / 依赖图靠它寻址
    std::string name_;    // 用户可见名称，如 "Box001"
};

} // namespace forge::domain
```

### 文件 3：BoxFeature.h / BoxFeature.cpp —— 第一个具体 Feature
```cpp
// BoxFeature.h
#pragma once
#include "Feature.h"

namespace forge::domain {

class BoxFeature : public Feature {
public:
    BoxFeature(std::string id, double length, double width, double height);

    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    std::string rebuild() const override;

private:
    std::vector<Parameter> params_;
};

} // namespace forge::domain
```

```cpp
// BoxFeature.cpp
#include "BoxFeature.h"
#include <stdexcept>
#include <utility>

namespace forge::domain {

BoxFeature::BoxFeature(std::string id, double length, double width, double height)
    : Feature(std::move(id), "Box") {
    params_.emplace_back("length", length);
    params_.emplace_back("width",  width);
    params_.emplace_back("height", height);
}

const std::vector<Parameter>& BoxFeature::parameters() const { return params_; }

void BoxFeature::setParameter(const std::string& name, ParameterValue value) {
    for (auto& p : params_) {
        if (p.name() == name) { p.setValue(std::move(value)); return; }
    }
    throw std::invalid_argument("Box 没有参数: " + name);
}

std::string BoxFeature::validate() const {
    for (const auto& p : params_) {
        if (p.asDouble() <= 0.0) {
            return "参数 " + p.name() + " 必须大于 0，当前值 = "
                 + std::to_string(p.asDouble());
        }
    }
    return {};   // 空串 = 合法
}

std::string BoxFeature::rebuild() const {
    // W3 之前：用文本描述"重建了什么"；W3 接 OCCT 生成真 Shape
    return "Box[id=" + id()
         + ", length=" + std::to_string(params_[0].asDouble())
         + ", width="  + std::to_string(params_[1].asDouble())
         + ", height=" + std::to_string(params_[2].asDouble()) + "]";
}

} // namespace forge::domain
```

### 文件 4：FeatureFactory.h / FeatureFactory.cpp —— 工厂
```cpp
// FeatureFactory.h
#pragma once
#include <memory>
#include <string>
#include "Feature.h"

namespace forge::domain {

class FeatureFactory {
public:
    // 按类型名创建；未知类型抛 std::invalid_argument
    static std::unique_ptr<Feature> create(const std::string& type, const std::string& id);
};

} // namespace forge::domain
```

```cpp
// FeatureFactory.cpp
#include "FeatureFactory.h"
#include "BoxFeature.h"
#include <stdexcept>

namespace forge::domain {

std::unique_ptr<Feature> FeatureFactory::create(const std::string& type, const std::string& id) {
    if (type == "box") {
        return std::make_unique<BoxFeature>(id, 100.0, 50.0, 30.0);  // 默认尺寸
    }
    throw std::invalid_argument("未知特征类型: " + type);
}

} // namespace forge::domain
```

### 文件 5：tests/domain_test.cpp —— 6 个单元测试
```cpp
#include <gtest/gtest.h>
#include <stdexcept>
#include "domain/Feature.h"
#include "domain/BoxFeature.h"
#include "domain/FeatureFactory.h"

using namespace forge::domain;

TEST(BoxFeatureTest, HasThreeParameters) {
    BoxFeature box("Box001", 100, 50, 30);
    ASSERT_EQ(box.parameters().size(), 3u);
    EXPECT_EQ(box.parameters()[0].name(), "length");
    EXPECT_DOUBLE_EQ(box.parameters()[0].asDouble(), 100.0);
}

TEST(BoxFeatureTest, ValidDimsPass) {
    BoxFeature box("Box001", 100, 50, 30);
    EXPECT_TRUE(box.validate().empty());
}

TEST(BoxFeatureTest, NonPositiveDimsFail) {
    BoxFeature box("Box001", 100, 0, 30);
    EXPECT_FALSE(box.validate().empty());   // 返回了错误信息
}

TEST(BoxFeatureTest, SetParameterWorks) {
    BoxFeature box("Box001", 100, 50, 30);
    box.setParameter("height", 60.0);
    EXPECT_DOUBLE_EQ(box.parameters()[2].asDouble(), 60.0);
    EXPECT_THROW(box.setParameter("radius", 10.0), std::invalid_argument);
}

TEST(FeatureFactoryTest, CreatesBox) {
    auto f = FeatureFactory::create("box", "Box001");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->name(), "Box");
    EXPECT_EQ(f->id(), "Box001");
    EXPECT_TRUE(f->validate().empty());
}

TEST(FeatureFactoryTest, UnknownTypeThrows) {
    EXPECT_THROW(FeatureFactory::create("sphere", "S001"), std::invalid_argument);
}
```

### 接入 CMake（就两步，我已在 tests 目录帮你改好自动扫描）
> 主工程 CMakeLists 里我已经加了 `if(MSVC) add_compile_options(/utf-8) endif()`——中文注释在 MSVC 下必须显式声明 UTF-8，否则报 C4819（Day1 教材第 8 节讲过）。
1. **改 `src/CMakeLists.txt`**：把两个新 .cpp 加进 `forgecore` 库（现在长这样，你需要加上 `domain/` 两行）：
```cmake
add_library(forgecore
    core/Version.cpp
    geometry/ShapeFactory.cpp
    domain/BoxFeature.cpp
    domain/FeatureFactory.cpp
)
```
2. **tests/CMakeLists.txt 我已改好**：测试目录现在自动扫描 `*.cpp`，你以后新建测试文件**不用再动 CMake**。

改完 CMakeLists 直接 `cmake --build build --config Release`——VS 生成器会自动重新配置，然后跑测试：
```powershell
ctest --test-dir build -C Release --output-on-failure
```
目标：**6 个测试全绿**。

> 💡 每一步都想一个面试问题：为什么 `validate()` 返回 `std::string` 而不是抛异常？—— 参数填错是**用户可预期的错误**，不该用异常（异常留给"意外"。第 4 周的错误模型还会细讲）。

---

## 6. 今晚面试复盘 —— 12 个必答问题

> 要求：不看资料，每题连续讲 2 分钟。答案提示在括号里，先自己讲再看。

1. **vector 和 list 的区别？什么时候用谁？**（连续内存/缓存友好/随机访问 vs 链表/中间插入；绝大多数用 vector）
2. **map 和 unordered_map 的区别？**（红黑树有序 O(log n) vs 哈希平均 O(1) 无序；要排序用 map，只要查找用 unordered_map）
3. **vector 迭代器什么时候失效？**（扩容全部失效；中间插入删除之后的全失效）
4. **emplace_back 和 push_back 的区别？**（就地构造 vs 拷贝/移动进容器）
5. **lambda 的 `[=]`、`[&]`、`[this]` 各是什么意思？**（值拷贝 / 引用 / 捕获成员）
6. **std::function 是什么？**（任意可调用对象的类型擦除包装；回调、事件）
7. **模板解决什么问题？和虚函数什么区别？**（一份代码多类型，编译期生成 vs 运行时 vtable 多态）
8. **std::optional 什么时候用？**（"可能没有值"的返回，语义清晰）
9. **std::variant 什么时候用？**（一组固定类型中恰是一个；类型安全 union）
10. **Factory 解决什么问题？ForgeCAD 哪里用？**（创建与使用解耦，加类型只改工厂；FeatureFactory）
11. **Command 解决什么问题？和 Undo/Redo 什么关系？**（操作封装成对象可存储可逆；Command 栈弹栈 undo）
12. **Observer 解决什么问题？和 Qt signal/slot 什么关系？**（变化通知关注者，业务不认识 UI；信号槽就是观察者模式）

---

## ✅ Day 2 是否达标自检表

- [ ] 能脱口说清 vector/list/map/unordered_map 选择
- [ ] 能说出迭代器失效的全部规则
- [ ] 会写 lambda 给 `std::sort` 传自定义比较
- [ ] 会写一个函数模板
- [ ] 说清 optional / variant / string_view 各自用途
- [ ] 结合 ForgeCAD 讲清 Factory / Command / Observer 为什么用
- [ ] Parameter / Feature / BoxFeature / FeatureFactory 写完
- [ ] 6 个单测全绿（ctest 通过）
- [ ] 12 个面试题不看资料讲完

> 全勾上，Day 2 成功！明天进入：**Feature 树 + 依赖图（DAG + 拓扑排序，你的算法主场）+ Command 栈 + Undo/Redo** —— ForgeCAD 最值钱的一周。
