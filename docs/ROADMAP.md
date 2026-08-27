# ForgeCAD 全量冲刺路线图（一个模块都不砍）

> 更新于 2026.8.27，你的决定：**13 个模块全做，以最高质量找 C++/工业软件工作。**
> 我的态度：支持，并且这条路**可行**——但有一个前提，就是下面这套策略。不靠"砍"，靠"工程优先级管理"（真实项目本来就这么做）。
>
> **时间账本（先把丑话说清楚）**：秋招约 4-5 周。按每天 6h+、每周 6 天算 ≈ **150-180 小时**。全量做下来大约需要 150h——**做得到，但一天都不能浪费**。所以每个模块都有明确"达标线"，到线就往下走，回头再加深。

---

## 策略三原则（让"全量"可行的核心）

### 原则 1：让库干活，不自己造轮子
原文档自己就写了"不需要自己实现 CAD Kernel"。具体到实现：
- **几何内核**：OCCT 负责（布尔、B-Rep、STEP、分析全是现成 API）
- **3D 交互**：用 OCCT 的 **AIS**（自带旋转/平移/缩放/Fit All/正交透视/拾取/高亮/选择）——**业界 mini-CAD 都这么干**，手写 OpenGL 是浪费时间的"高质量陷阱"
- **你的价值**：应用层架构、参数化系统、Feature 树、依赖图、命令/撤销、任务系统、测试、性能——这些才是面试官深挖的地方

### 原则 2：先横后纵（breadth-first, then depth）
- **前 3 周横着铺**：把 13 个模块全部做出"v1 可演示版"——第 4 周末你就有一个**什么都有**的 ForgeCAD
- **后 2 周纵着挖**：把面试深挖点做深（Feature 系统/依赖图/Undo/任务系统 + 一个性能优化案例 + 一个并发案例）
- 好处：任何一天被面试/被打断，你手里的 ForgeCAD 都是"完整可讲"的

### 原则 3：完成度三档（不是砍，是优先级）
| 档位 | 定义 | 适用模块 |
|---|---|---|
| 🟢 **完整档** | 能做深、能扛 10+ 层追问、现场能改 | Feature 系统、依赖图、Undo/Redo、任务系统 |
| 🟡 **精简档** | 能跑、有测试、能讲 3 层追问 | 几何/布尔/分析、Qt GUI、STEP、日志/错误 |
| 🔵 **演示档** | 能跑、能演示、能讲 1-2 层 | Sketch 约束求解器、AI 助手 |

> 原文档自己也是这么定的：Sketch 章节写了"不要追求完整求解器"；AI 章节写了"作为增强方向"。所以这三档**不是违背原文档，是原文档精神的执行细则**。

---

## 全量功能清单（对照原文档，13 个模块一个不少）

| # | 模块 | 实现方式 | 达标线 |
|---|---|---|---|
| 1 | 参数化建模系统（Box/Cylinder/Sphere/Cone/Torus/Extrude/Revolve） | 领域模型 + OCCT | 🟢 完整档 |
| 2 | Feature 树 / 参数历史树（父子依赖/增量重建/suppress/reorder） | 纯 C++ DAG + 拓扑排序 | 🟢 完整档 |
| 3 | Boolean 内核（Fuse/Cut/Common） | OCCT BRepAlgoAPI | 🟡 精简档 + 错误处理 |
| 4 | 几何分析（体积/面积/质心/包围盒/顶点边面数） | OCCT + IShapeAnalyzer 接口 | 🟡 精简档 |
| 5 | 交互式 3D 视图（旋转/平移/缩放/Fit/正交透视/拾取/高亮/联动） | **OCCT AIS** | 🟡 精简档 |
| 6 | Sketch 草图（点线圆弧矩形 + 水平/垂直/共点/平行/垂直/距离/半径约束） | 自研几何对象 + 基础约束 | 🔵 演示档 |
| 7 | Undo/Redo（Command 栈 + Ctrl+Z/Y） | 纯 C++ Command 模式 | 🟢 完整档 |
| 8 | STEP 导入导出（含错误处理/最近文件/元数据） | OCCT Data Exchange | 🟡 精简档 |
| 9 | 异步任务系统（Task/Queue/ThreadPool/Future/进度/取消） | 纯 C++ 多线程 | 🟢 完整档 |
| 10 | 日志与错误系统（spdlog + Result/Error 模型） | spdlog + 自研 | 🟡 精简档 |
| 11 | 性能基准（Shape/Boolean/STEP/分析耗时 + P95） | chrono + Benchmark 模块 | 🟡 精简档 |
| 12 | 测试系统（GoogleTest 全模块覆盖 + 回归测试） | GTest | 🟢 完整档（贯穿全程） |
| 13 | AI 建模助手（自然语言→结构化指令→C++ 执行） | 离线指令解析 + 可选 LLM API | 🔵 演示档 |

---

## 五周计划（每天 6h+ 版）

### 第 1 周：C++ 核心 + 工程化 → ForgeCAD v0.1（参数化地基）
- **学**：内存管理/RAII/智能指针/移动语义 → STL 容器/迭代器失效 → 模板/optional/variant → 设计模式（Factory/Command/Observer/Strategy）
- **做**：`Parameter` / `Feature` 抽象 / `BoxFeature` / `CylinderFeature` / `FeatureFactory` + 单测
- **产出**：`src/domain` 完整雏形，10+ 个单测全绿
- **面试必讲**：RAII、智能指针选择、vector 扩容、迭代器失效、为什么用 Factory

### 第 2 周：Feature 树 + 依赖图 + Undo/Redo → ForgeCAD v0.2（最值钱的一周）
- **学**：DAG/拓扑排序（你的算法主场）、Command 模式实战、观察者/事件
- **做**：
  - `DependencyGraph`：Feature 之间的父子依赖（纯 C++，可单测）
  - `FeatureTree`：添加/删除/抑制/重排 + 参数修改后的**增量重建**（拓扑序）
  - `Command` 体系：`CreateFeatureCommand` / `ModifyParameterCommand` / `DeleteFeatureCommand` + `CommandManager` 栈
  - **Undo/Redo**：Ctrl+Z / Ctrl+Y，全部命令可逆
  - 重建失败状态（Feature 变红，可恢复）
- **产出**：控制台可跑的完整参数化历史系统 + 20+ 单测
- **面试必讲**：DAG 怎么存、拓扑排序怎么用、增量重建、Undo/Redo 的 Command 实现

### 第 3 周：3D 数学 + OCCT 几何内核 → ForgeCAD v0.3（几何全武装）
- **学**：向量/点积叉积/法向量/坐标系/欧拉角/四元数/4x4 矩阵/坐标变换/AABB/射线拾取基础/容差与浮点误差
- **做**：
  - `ShapeFactory` 补全：Box/Cylinder/Sphere/Cone/Torus + Extrude + Revolve + Transform（OCCT）
  - `BooleanKernel`：Fuse/Cut/Common + 失败错误检测 + 无效 Shape 检查
  - `Analyzer`：体积/面积/质心/包围盒/顶点边面数（`IShapeAnalyzer` 接口 + Strategy）
  - Feature 的 `rebuild()` 从"文本占位"升级为"真生成 OCCT Shape"
- **产出**：控制台能建 7 种基本体、做布尔、算分析
- **面试必讲**：B-Rep（Geometry vs Topology）、布尔容差、形状有效性、为什么用 OCCT

### 第 4 周：Qt GUI + 3D 视图 + Sketch + STEP → ForgeCAD v0.4（看得见摸得着）
- **学**：Qt 6（QObject/signal-slot/QWidget/Model-View/QTreeView/QDockWidget/属性面板）、AIS 3D 视图集成、STEP 数据交换
- **做**：
  - `MainWindow` + `ModelTree`（QTreeView 显示 Feature 树）+ `PropertyPanel`（改参数触发重建）+ `Viewport`（AIS 3D 视图）
  - **联动**：模型树 ↔ 3D 视图 ↔ 属性面板（Observer/signal-slot）
  - 拾取/高亮 Face/Edge/Vertex、Fit All、正交/透视切换
  - Sketch 简化版：几何对象（点/线/圆/弧/矩形）+ 基础约束（水平/垂直/共点/平行/垂直/距离/半径）+ 参数驱动
  - STEP 导入导出 + 最近打开文件 + 大模型异步加载（提前用上线程，见 W5）
- **产出**：**可以给任何人演示的 ForgeCAD 桌面应用**
- **面试必讲**：signal/slot 解耦、Model/View、GUI 与业务解耦、AIS 为什么不自绘

### 第 5 周：任务系统 + 工程收尾 + AI 助手 + 面试冲刺 → ForgeCAD v1.0
- **学**：多线程（mutex/condition_variable/atomic/future/线程池/生产者消费者/死锁）、spdlog、错误模型、benchmark
- **做**：
  - 自研 `ThreadPool` + `TaskQueue` + 进度/取消/错误回调 → Boolean/STEP 加载/分析全部异步化，GUI 不卡
  - 日志系统铺全（Feature 创建/Boolean/STEP/Task/异常/性能）
  - `Result`/`Error` 错误模型落地（用户可理解错误）
  - Benchmark 模块（单次/平均/P95）+ **至少一个性能优化案例**（比如重建缓存）
  - AI 助手：`自然语言 → 结构化命令 JSON → C++ 校验执行`（离线解析器先跑通，可选接 LLM API）
  - README + 架构文档 + 演示 GIF + 50 题面试问答稿 + 简历项目描述
- **产出**：ForgeCAD v1.0 完整交付
- **面试必讲**：线程池实现、生产者消费者、死锁预防、任务取消、错误模型、性能优化案例、AI 为什么不能直接操作内核

---

## 每天怎么过（6h 版）

1. **学新知识**（1.5h）：我讲 / 读教材，大白话
2. **手写练习**（1h）：不动笔不算学
3. **项目推进**（2.5h）：把今天学的写进 ForgeCAD，每完成一步就跑测试
4. **面试复盘**（1h）：当天知识点+项目点，不看资料讲 2 分钟

## 教学循环

```
我讲 → 你动手 → 发代码给我批改 → 我反馈 + 出面试题 → 进项目 → 下一课
```

## 风险与缓冲（提前说，不慌）

- **3D 视图**：AIS 自带交互，绝不手写 OpenGL（那是"高质量陷阱"）
- **Sketch 求解器**：按原文档建议做基础版，不做完整自由度求解
- **AI 模块**：离线指令解析先跑通（一定可演示），LLM API 是加分项不是依赖
- **万一某周落后**（真实工程常态）：深挖优先级 = **Feature/依赖图/Undo > 任务系统 > 几何分析 > GUI 联动 > STEP > Sketch > AI**——后面的档位自动降为"能讲"，前面的一个不降
- **每天一个 Git 提交**：面试展示真实开发历史；出问题能回滚

## 验收铁律

- 每个模块：能跑 + 有测试 + 你能讲清"为什么这么设计"
- 每周五晚：给我看本周 demo + 测试通过截图，我出本周面试题考你
- **讲不清的代码 = 没写。宁可降档，不可装懂。**
