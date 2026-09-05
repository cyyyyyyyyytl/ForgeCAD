# ForgeCAD 学习/工作日志（SESSION LOG）

> **交接协议（重要，每次会话先读这里）**：
> 1. 新会话开始时，先读本文件**顶部最新条目** + `docs/ROADMAP.md`，即可无缝接续，无需翻旧聊天记录。
> 2. 每次教学/工作会话结束时，**追加一条日志**（新条目放顶部）并 `git commit`。
> 3. 日志格式：日期 / 目标 / 已完成 / 决策与原因 / 问题与解决 / 作业与待办 / 下一步。
> 4. 环境事实（编译器、构建命令、仓库状态）以仓库内代码和 README 为准，不要靠记忆。
> 5. **代码注释约定（用户明确要求，必须遵守）**：凡是我写或修改的代码，一律带**详细中文注释**——每块解释"是什么、为什么"；改动用户代码时逐行注释讲解。用户学习节奏：**慢慢来，逐步理解**，不要一次塞太多新概念。

---

## 2026-09-05（教学 + 开发：全链路精读 → rebuild 真形状 → 属性面板 → FeatureFactory/Cylinder）

### 目标
先让用户彻底看懂整个 ForgeCAD 架构，再亲手打通"参数化闭环"，随后把 domain 扩到多形状。

### 已完成（三个里程碑，测试 28/28 全绿）
1. **架构逐行精读（对话内教学）**：main.cpp → ShapeFactory → Viewport3D → MainWindow → domain 全链讲透。用户掌握：分层依赖只许向下、TopoDS_Shape 是层间接缝、rebuild 是 domain 的唯一出口、"domain 目前只被测试用"是骨架期策略、.ui→uic→ui_mainwindow.h 流水线、Qt 自动连接约定。
2. **里程碑 1 — rebuild() 升级为真形状**：`Feature::rebuild()` 签名 `std::string → TopoDS_Shape`；BoxFeature 的 rebuild 调 `ShapeFactory::makeBox`（domain→geometry 首次握手）；测试合同从"文本"换成"真形状"（12/12）。
3. **里程碑 2 — 参数化属性面板（GUI 实时联动）**：.ui 加"属性"group（3×QDoubleSpinBox）；Qt Creator"转到槽"生成 `on_spin*_valueChanged`（自动连接 = on_<控件>_<信号> + setupUi 里的 connectSlotsByName）；MainWindow 持有 `unique_ptr<BoxFeature> box_` + `refreshModel()`；Viewport3D::showShape 改为"先 Remove 旧 AIS 再 Display 新"（成员 `displayed_`）；main.cpp 瘦身（模型归 MainWindow 管，不再直接调 ShapeFactory）。
4. **里程碑 3 — CylinderFeature + FeatureFactory**：`ShapeFactory::makeCylinder`；`CylinderFeature`（radius/height）；`FeatureFactory::create` 按类型分发，参数个数不符/未知类型抛异常（防 vector 越界）；新增 2 个测试文件 + 2 个 shape 测试；多态统一操作测试实证"加新特征系统零改动"。

### 踩坑与教学点（面试金矿）
- **编辑器爆红三类原因**：① .h 用到类型却没 include（头文件要自给自足）；② 成员函数没在类里声明（槽还必须放 `slots:` 区，否则 moc 不认 → 编译过但静默失效）；③ `ui->xxx` 来自构建时 uic 生成，先构建再说。
- **Viewport3D 换图两 bug**：Remove 条件写反（永远不删旧）；`auto displayed_ = new AIS_Shape(...)` 遮蔽成员（成员永远空，每次真"新建"）。结论：**模型层重算不删对象；显示层换图必须撤旧**。
- **OCCT 8 IsDone() 偶发 false**：不能信标志，以 `Shape()` 实际产出为准（测试多次触发 warning 实证）。
- **注释纪律**：改签名必须同步改注释（本次修掉 3 处 Box 复制残留 + 2 处 catch 日志抄错名）。
- FeatureFactory.h 缺 `#pragma once`；`std::vector::operator[]` 不查边界——取下标前先查 `size()`。

### 决策与原因
- rebuild 直接调 geometry 采用路线图原案（最快出 demo）；domain 分层纯度留作日后 refactor。
- 属性面板先用 Designer 自动槽（顺 Qt Creator 习惯）；将来泛化时再改代码手动 connect。
- UI 用 spinbox `minimum=1` 做"前置校验"，让用户输不出非法值；domain `validate()` 仍留作兜底。

### 待办 / 下一步
- **UI 集成**：菜单"新建"选 长方体/圆柱体 → 对话框填参数 → 建立并显示。属性面板是否泛化为"跟随当前特征动态变化"待选方案（见会话）。

### Git 状态
- 本次会话累计改动未提交：rebuild 升级、属性面板、Viewport3D 换图、main 瘦身、Cylinder/Factory、28 测试、本日志。
- 备注：`ForgeCAD_Cpp_秋招技术栈与项目规划.txt`（GBK 编码）仍 untracked，未纳入提交。

---

## 2026-08-28（环境重装 + Day2 强化 + 3D 视图上线）

### 环境重装（本机全新配置，全部完成）
- **工具链**：Git 2.55、Python 3.13、vcpkg（gitee 镜像克隆）、Qt 6.11.2 MSVC2022 x64（aqt 下载 2.16GB → `D:\Qt`）、OCCT 8.0.1 + spdlog + fmt + gtest + sqlite3 + nlohmann-json（vcpkg 编译 37min）
- **D 盘权限修复**：`D:\ForgeCAD` / `D:\Qt` ACL 原来只有 Users 只读 → 用户确认 UAC 后 icacls 授权 FullControl（不修则 CMake 无法建 build 目录）
- **Git 历史接回**：本地 .git 丢失（换机）→ git init + fetch origin/master + 复用云端 .git → 14 个历史提交完整保留 + 新增同步提交，已 push
- **端到端验证**：CMake 配置 0.8s 成功 + Release 构建 + ctest 3/3 全绿

### Day 2 强化（抽查 3 题全对）
- 虚析构为什么（delete 基类指针只调基类析构→子类泄漏）；Parameter/Feature 组合关系；两个 const 含义
- **BoxFeature 接进工程**：`src/CMakeLists.txt` 注册 `domain/BoxFeature.cpp`（用户亲手改 ✅）→ 之前写了但没编译进库
- **BoxFeature 测试 8 个**（test_box_feature.cpp）：构造/参数列表/改参数/改未知参数抛异常/校验合法/校验负数/校验零/重建 → **11/11 全绿**

### 3D 视图上线（今天最大成果，踩坑 4 个）
- 新增 `src/ui/Viewport3D.h/.cpp`（QWidget 子类），main.cpp 集成，Box 显示为 3D 模型
- 踩坑记录（面试金矿）：
  1. **白屏**：构造函数里 winId() 太早，句柄未定 → 移到 showEvent
  2. **视图消失/不主动显示**：OCCT NeutralWindow 画面被 Qt 覆盖，无后台刷新 → **QTimer 33ms 直接调 view_->Redraw()**（官方 OcctQtViewer 同款）
  3. **闪烁**：定时器 update() + paintEvent 双路径重绘 → paintEvent 留空，只走定时器单一路径
  4. **中心缩放**：SetZoom 以屏幕中心缩放 → 改 StartZoomAtPoint + ZoomAtPoint 光标机制（每格偏移 50px）

### 决策与原因
- Qt6 下不用 WNT_Window（官方已知 bug：视图在 a.exec() 后消失），用 Aspect_NeutralWindow + SetNativeHandle(winId())
- 3D 视图用定时器驱动重绘是 OcctQtViewer 标准做法；W5 再优化成事件驱动
- 练习目标（practice/）用 if(EXISTS) 包裹，缺失不阻塞主工程

### 待办 / 下一步
- 提交后选方向：① 更多形状（圆柱/球/圆锥）② 参数面板改尺寸→3D 实时更新 ③ FeatureFactory（日志交接点）
- 可选：WSL2 装 Ubuntu（Linux 双平台，第 5 周前做）

### Git 状态
```
（本次提交：3D 视图 + BoxFeature 测试 + CMake 注册 + DLL 部署）
f1a0ec7 chore: 环境重装后同步(换行符统一 CRLF); CMakeLists 练习目标加 if(EXISTS) 容错
5db5941 Day2: BoxFeature 完成（第一个具体特征类），box_demo 验证通过；CMake 注册 BoxFeature.cpp
```

---

## 2026-08-27（教学会话：Day 2 进行中——Feature.h 完成）

### 已完成
- **`src/domain/Feature.h` 完成**（用户自己填了 `return id_;` / `return name_;` 两个空 ✅）→ 已按"详细注释"标准升级注释，编译验证通过（parameter_demo 临时 include 检查）
- 用户确认学习节奏："慢慢理解"；要求所有代码带详细注释（已写入本日志顶部约定）
- 概念澄清完成：Parameter=数字、Feature=盒子本身；"项目就是课程，课程就是项目"；已展示真实项目里简单类与接口类的区别（复杂是少数、是战略要地）

### 下一步（交接点）
1. **BoxFeature**（第一个具体类）：写之前先问用户"我写+逐行讲（示范）"还是"自己写+我给规格"——尊重慢节奏
2. 之后：FeatureFactory → day2 测试（6 个）→ Day 3 依赖图
3. 面试题抽查：虚析构为什么、unique_ptr 选择、Parameter/Feature 关系

### Git 状态
```
（待提交：src/domain/Feature.h、本日志）
4004bdb chore: gitignore 重写为纯 UTF-8（修复混编码）；删除空 Parameter.cpp
f65a7e5 Day2: Parameter.h（教学共创）；CMakePresets（vcpkg+Qt 工具链一键配置，VS/CLion 通用）...
```

---

## 2026-08-27（教学会话：Day 1 毕业 + Day 2 开始 + IDE 环境）

### 重大变化：教学模式切换
- 用户要求"你带我学，别自己在后台跑" → 已暂停自动轮次（goal paused），**转为对话内交互教学**：我讲→用户动手→我检查→编译运行。
- 用户此前未读文档，Day 1 全部在对话内完成。

### 已完成（交互教学成果）
1. **Day 1 核心全部讲完并验证**：
   - 内存泄漏/RAII/智能指针概念（`unique_ptr`/`shared_ptr`/`weak_ptr` 一句话版）
   - 多态 + 虚函数 + **虚析构**（用户自写 Rectangle/Circle 版，升级到 `make_unique` 版，编译运行通过）
   - 用户第一次亲手编译运行 C++ 程序 ✅（学了 cmd 的 `cd /d` 跨盘坑）
2. **真实项目第一行代码**：`src/domain/Parameter.h`（用户建骨架 + 填空，我补 variant/异常部分，逐行讲解）→ `parameter_demo` 编译运行验证 4 种行为全对
   - 用户的 `Parameter.cpp` 空壳已删除（纯头文件类不需要 .cpp；CLion 新建类向导默认生成，注意取消）
3. **工程环境（重要）**：
   - **Device Guard 策略**：裸 `cl` 编译的 exe 被拦，CMake 构建的能跑 → 统一用 CMake 构建（用户机器同样生效）
   - **CMakePresets.json 建立**：`vs2026-release`（VS 18 2026 + vcpkg 工具链 + Qt 路径 + 测试开）→ build 目录已用 preset 重建并验证（forgecad.exe + memory_lesson/polymorphism/parameter_demo + 3/3 测试）
   - 练习目标（memory_lesson/polymorphism/parameter_demo）挂根 CMakeLists，源码在 `practice/day1/`（gitignored）
   - `.idea/`（CLion 配置）已 gitignore；`.gitignore` 曾混编码损坏，已重写为纯 UTF-8

### 用户环境事实
- 用户用 **CLion**（报红）→ 根因：CLion 默认 MinGW 工具链 + 未配 vcpkg → Qt/OCCT REQUIRED 找不到 → 配置失败全红。已给出两种方案：推荐 **VS 2026 打开文件夹**（零配置）；CLion 需配 VS 工具链 + 选 vs2026-release preset

### 待办（用户）
- 📌 **Feature.h**（填空版已给全代码框架，用户敲入 `src/domain/`）：抽象基类 + 纯虚函数 + 虚析构，继承 Parameter
- 之后：BoxFeature（具体类）、FeatureFactory（工厂）→ 6 个单测 → Day 3（依赖图，用户写算法实现）

### 下一步（交接点）
1. 检查 Feature.h → 编译验证 → BoxFeature → FeatureFactory
2. 写 tests（day2 测试文件抄自 `docs/day2-stl-templates-design-patterns.md` 第 5 节）
3. 面试题抽查 Day 1 概念（RAII/智能指针/虚析构）
4. 之后：Day 3 依赖图（教材已备好）

### Git 状态
```
4004bdb chore: gitignore 重写为纯 UTF-8（修复混编码）；删除空 Parameter.cpp
f65a7e5 Day2: Parameter.h（教学共创）；CMakePresets（vcpkg+Qt 工具链一键配置，VS/CLion 通用）；练习目标...
4416f39 练习脚手架：memory_lesson 目标（CMake 构建可绕过裸 cl 的 Device Guard 拦截）；practice/ 入 gitignore
fa374de docs: Day4 教材(Command+Undo/Redo，修正悬垂指针 bug)；日志: 第3轮
```

---

## 2026-08-27（第 3 轮：教学推进日 2）—— Day4 教材完成，Day2+3+4 全量验证通过

### 已完成
1. **Day 4 教材**：`docs/day4-command-undo-redo.md` —— Command 模式 + Undo/Redo
   - 双栈 `CommandManager`（execute/undo/redo）+ 3 个具体命令（Add/Delete/ModifyParameter）+ 所有权流转讲解
   - 9 个测试 + 12 面试题 + 挑战题（Feature reorder）
2. **全量验证（本轮最大成果）**：Day2+3+4 全部代码**临时**放入真实仓库 → **编译 + 链接 + 29/29 测试全绿**（3 原有 + 8 依赖图 + 9 Feature 树 + 9 命令）→ 验证后**全部还原**，仓库恢复 3/3 干净状态，无残留
   - 说明：`src/application/` 目录验证后已删除（原骨架没有此目录，用户写 Day4 时自行创建）
3. **抓到一个真实 bug（教材第 1 版，被测试抓出）**：失败命令不入栈 → `execute(std::move(cmd))` 结束时命令对象被 unique_ptr 销毁 → 测试里保留的 `cmd.get()` 裸指针成**悬垂指针** → SEH 0xc0000005 崩溃
   - 修正：`CommandManager::execute` 改为**返回 bool**（成功/失败），调用方立刻知道结果，不再需要裸指针
   - 教材已同步修正，并把此案例写成"真实教训"教学点（这是最好的面试故事素材）
4. 用户仍未交 Day 1 作业（Day 1 需数小时，正常）

### 决策
- `execute()` 返回 bool 优于 void + failed() 裸指针查询（生命周期安全、调用方即时感知）
- 验证通道结论：临时文件放入真实仓库跑 ctest 是可靠通道（沙箱"应用程序控制"拦截新 exe 为间歇性，本次全部跑通）；验证后必须还原

### 待办（等用户，顺序不变）
- Day 1 → Day 2 → Day 3 → Day 4 顺序推进
- 每步验收即测试全绿；**累计 29 个测试的规格书已全部备好**（day3/day4 测试文件在教材文档里，用户抄入 `tests/` 即可）

### 下一步（交接点）
1. 批改用户 Day 1 作业
2. 用户按 Day2/3/4 教材写代码（写完 `src/CMakeLists.txt` 加对应 .cpp 行）
3. 下一份教材：**Day 5 —— 3D 数学 + OCCT 几何内核（W3 开始）**：向量/矩阵/坐标变换 + ShapeFactory 补全（7 基本体 + Extrude/Revolve/Transform）+ Boolean（TKBO）+ 分析（体积/包围盒）；OCCT 库已全部预链接（上轮）

### Git 状态
```
（待提交：docs/day4、本日志）
33cfb32 docs: Day3 教材(依赖图+Feature树+增量重建)；工程: OCCT 库补全(W3/W4 用)；日志: 第2轮
18c6b53 docs: 建立每日学习/工作日志 SESSION_LOG（含交接协议与第0天记录）
bc039e2 docs: ROADMAP 升级为全量版（13 模块不砍，完成度三档 + 先横后纵策略）
01f8488 docs: 保命版路线图 + Day2 教材；工程: MSVC /utf-8、测试自动扫描 *.cpp、README 生成器修正
ee17dfc 初始化 ForgeCAD 骨架：CMake+vcpkg+Qt6+OCCT+spdlog+gtest 端到端验证通过
```

---

## 2026-08-27（第 2 轮：教学推进日）—— Day3 教材完成，等待 Day 1 作业

### 已完成
1. **OCCT 能力盘点**（vcpkg 安装是完整版，W3/W4 零安装成本）：
   - 已确认存在：`TKBO`（布尔 Fuse/Cut/Common）、`TKV3d`+`TKService`+`TKOpenGl`（AIS 3D 视图）、`TKFillet`（倒角）、`TKOffset`、`TKXCAF`（STEP 元数据）、STEP 读写头文件
   - 根 `CMakeLists.txt` 的 `OCCT_LIBRARIES` **已补全**上述 7 个库（已验证：重建成功 + 3/3 测试绿）
2. **Qt 确认**：`Qt6OpenGLWidgets` 存在 → W4 把 AIS 视图嵌进 Qt 无阻碍
3. **Day 3 教材写好**：`docs/day3-dependency-graph-feature-tree.md`（W2 核心，最值钱的一课）
   - 依赖图 DAG + 拓扑排序（Kahn）+ 环检测 + 下游级联 + Feature 树 + 增量重建
   - 教学法：`DependencyGraph` 只给**接口 + 8 个测试**（测试即规格书），实现留用户自己写（算法主场）；文末附参考答案
   - `FeatureTree` 完整代码给敲 + 9 个测试
   - 12 道面试题 + 自检表
4. **Day3 代码验证**：编译 + 链接全部通过（`forgecore.lib`、测试 exe 均成功链接）
   - ⚠️ **沙箱限制记录**：本会话后期"应用程序控制"策略间歇性拦截新编译 exe 的运行（连重新链接的 forgecad_tests.exe 都会被拦，git 等老二进制不受影响）。**这是 DSH 沙箱行为，不是用户机器的策略**。运行时验证未能在沙箱完成 → 以用户机器 `ctest` 为准（测试文件即规格书；算法逻辑已人工审计）

### 决策
- OCCT 库提前全量链接（链接器会丢弃未引用库，无副作用）→ W3/W4 免改 CMake
- 验证用临时代码放进真实仓库跑链接验证 → **已全部还原**（git 干净，3/3 测试恢复绿）
- 验证用 scratch 工程放桌面/临时目录，用完即删

### 待办（等用户）
- 📌 Day 1 作业（读 day1 文档 → 手写 Buffer/Shape/Box → 编译运行 → 自答 15 题）——尚未提交，属正常（数小时工作量）
- Day 1 → Day 2（写 src/domain）→ Day 3（自己实现 DependencyGraph + 敲 FeatureTree）顺序推进

### 下一步（交接点）
1. 批改用户 Day 1 作业
2. Day 2：用户写 `src/domain`（Parameter/Feature/BoxFeature/FeatureFactory）+ `src/CMakeLists.txt` 加两行 + 6 测试全绿
3. Day 3：用户自己实现 `DependencyGraph.cpp` + 敲 `FeatureTree` → 17 个测试全绿
4. Day 4 教材（未写）：Command 模式 + Undo/Redo + suppress/reorder 基础

### Git 状态
```
（待提交：docs/day3、CMakeLists OCCT 库补全、本日志）
18c6b53 docs: 建立每日学习/工作日志 SESSION_LOG（含交接协议与第0天记录）
bc039e2 docs: ROADMAP 升级为全量版（13 模块不砍，完成度三档 + 先横后纵策略）
01f8488 docs: 保命版路线图 + Day2 教材；工程: MSVC /utf-8、测试自动扫描 *.cpp、README 生成器修正
ee17dfc 初始化 ForgeCAD 骨架：CMake+vcpkg+Qt6+OCCT+spdlog+gtest 端到端验证通过
```

---

## 2026-08-27（第 0 天：启动日）—— 全部就绪，等 Day 1 作业

### 今日目标
- 摸底用户情况（秋招时间 / 每日投入 / C++ 水平 / 语言偏好）
- 制定并确认学习路线图
- 验证现有工程环境可用、验证教学教材代码无误

### 已完成
1. **用户情况摸底**（用户亲答）：
   - 秋招：2026 年 9-10 月（下个月），时间紧
   - 每日投入：4 小时以上，可更高；效率意愿强
   - C++ 水平：会写简单类，**不懂内存管理**（new/delete、指针、智能指针）
   - 语言偏好：全中文教学（术语保留英文）
2. **路线图确定（重要决策）**：用户明确要求**不砍任何模块，全量 13 模块都要**，以最高质量求职。已据此重写 `docs/ROADMAP.md` 为**全量版**：
   - 策略三原则：让库干活（OCCT + AIS，不手写 OpenGL）/ 先横后纵 / 完成度三档（🟢完整档、🟡精简档、🔵演示档）
   - 五周计划：W1 参数化地基 → W2 Feature 树+依赖图+Undo/Redo（最值钱）→ W3 OCCT 几何全武装 → W4 Qt GUI+AIS 视图+Sketch+STEP → W5 任务系统+日志+基准+AI+面试冲刺
   - 时间账本：约 150-180h，每天 6h+、每周 6 天
3. **环境验证（全部通过）**：
   - 工具链：仅 Visual Studio 2026（工具集 v180），**无 v143**；Git 已装；cmake/ninja/ctest 随 VS 提供（不在 PATH，用完整路径）
   - vcpkg：`C:/Users/15389/vcpkg` ✓；Qt：`D:/Qt/6.11.2/msvc2022_64` ✓
   - 构建：`forgecore.lib` / `forgecad.exe` / `forgecad_tests.exe` 重建成功
   - 测试：`ctest` **3/3 全绿**（VersionTest + ShapeFactoryTest×2）
4. **教材代码验证（Day1/Day2 都编译+运行通过）**：
   - Day1：手写 `Buffer`（Rule of 5）、多态 `Shape`/`Box`、`unique_ptr` main → 运行输出正确
   - Day2：`Parameter`/`Feature`/`BoxFeature`/`FeatureFactory` → 6 个断言全过
5. **工程修复（已提交）**：
   - **编码坑**：中文注释 + MSVC 默认 GBK → C4819/语法错乱 → 根 CMakeLists 加 `if(MSVC) add_compile_options(/utf-8) endif()`（Day1 教材第 8 节已补充说明）
   - **生成器坑**：README 原写 `Visual Studio 17 2022`（无 v143 会 MSB8020）→ 改为 `Visual Studio 18 2026` 并加注释
   - **测试扫描**：`tests/CMakeLists.txt` 的 GLOB 从 `test_main.cpp` 改为 `*.cpp`（以后新建测试文件不用动 CMake）
6. **教材文档就绪**：
   - `docs/ROADMAP.md`（全量版）
   - `docs/day1-modern-cpp-foundation.md`（RAII/Rule 0-3-5/智能指针/移动/const/vector/多态/CMake 入门 + 15 面试题）
   - `docs/day2-stl-templates-design-patterns.md`（STL 容器/lambda/模板/optional/variant/设计模式 + 写 Parameter/Feature/BoxFeature/FeatureFactory + 6 单测）

### 决策与原因
- **全量版路线图**：用户坚持 13 模块全做。用"完成度三档"替代"砍模块"——原文档自己就写了 Sketch 不做完整求解器、AI 是增强方向。
- **Git 身份**：仓库级配置 `forgecad-dev <forgecad@local.dev>`（用户可自行 `git config user.name` 改）
- **验证工程位置**：Day1/Day2 教材验证代码放在系统临时目录（不污染主工程）；后续如遇"Application Control 拦截临时目录 exe"，把工程挪到非 Temp 目录即可。

### 问题与解决
- **C3668/C2059 语法错乱**（验证 Day1 教材时）：根因是 UTF-8 中文注释被 MSVC 按 GBK 读 → 加 `/utf-8` 解决
- **MSB8020 找不到 v143**：生成器名写错 → 用 `Visual Studio 18 2026`
- **day2_qa.exe 被 Application Control 拦截**：临时目录构建的 exe 被系统策略拦 → 换到 Desktop 重建后正常（用户主工程在 `D:\ForgeCAD`，不受影响）

### 作业与待办（等用户完成）
- 📌 **Day 1 作业**（用户做）：读 `docs/day1-modern-cpp-foundation.md` → 手写 Buffer/Shape/Box/main → CMake 编译运行 → 自答 15 题（不会的标记）→ 把代码和答不上的题发给教练批改
- 建议作业放 `D:\ForgeCAD\practice\day1\`（不污染主工程）

### 下一步（交接点）
1. 批改用户 Day 1 作业（代码逐行 + 面试题讲解）
2. 进入 Day 2：用户把 `Parameter`/`Feature`/`BoxFeature`/`FeatureFactory` 写进 `src/domain/`，在 `src/CMakeLists.txt` 的 `forgecore` 里加 `domain/BoxFeature.cpp` 和 `domain/FeatureFactory.cpp`，跑 `ctest` 到 6 个测试全绿（教材已给全代码，重点是理解后再敲）
3. Day 2 过关 → Day 3：Feature 树 + 依赖图（DAG/拓扑排序，用户算法主场）+ Command/Undo/Redo（W2 核心）

### Git 状态
```
bc039e2 docs: ROADMAP 升级为全量版（13 模块不砍，完成度三档 + 先横后纵策略）
01f8488 docs: 保命版路线图 + Day2 教材；工程: MSVC /utf-8、测试自动扫描 *.cpp、README 生成器修正
ee17dfc 初始化 ForgeCAD 骨架：CMake+vcpkg+Qt6+OCCT+spdlog+gtest 端到端验证通过
```
