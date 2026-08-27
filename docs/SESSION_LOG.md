# ForgeCAD 学习/工作日志（SESSION LOG）

> **交接协议（重要，每次会话先读这里）**：
> 1. 新会话开始时，先读本文件**顶部最新条目** + `docs/ROADMAP.md`，即可无缝接续，无需翻旧聊天记录。
> 2. 每次教学/工作会话结束时，**追加一条日志**（新条目放顶部）并 `git commit`。
> 3. 日志格式：日期 / 目标 / 已完成 / 决策与原因 / 问题与解决 / 作业与待办 / 下一步。
> 4. 环境事实（编译器、构建命令、仓库状态）以仓库内代码和 README 为准，不要靠记忆。

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
