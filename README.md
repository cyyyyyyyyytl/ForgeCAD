# ForgeCAD

一个使用 C++、Qt 和 OpenCASCADE 开发的参数化三维建模学习项目。

## 当前完成度

按原13模块路线图粗略估算约 **60%**，建模、文件、历史、界面和AI工具的演示闭环已打通；增量重建、异步任务、约束求解、性能基准和视觉反馈仍待完成。当前20类特征、37个AI工具，最近206／206测试通过。详见 [逐模块进度与下一阶段](docs/project-status.md)。这个估算不是测试覆盖率，也不代表工业CAD已完成。

## 扩展造型能力（2026-09-29）

已补齐圆锥／圆台、自定义直线／圆弧轮廓、空间路径、实体／轮廓变换、圆角／倒角、扫掠和放样；草图支持XY/XZ/YZ，旋转加入世界Z轴。全部接入参数、依赖、历史、原生文件和AI。当前 **37个AI工具、20类特征**，可用XZ杯壁轮廓绕Z生成空心杯身，用空间圆弧扫掠杯耳。

代码对话框和槽已完成，Designer已接好8个QAction，真实菜单动作测试验证自动槽连接，详见 [扩展建模与接线清单](docs/advanced-modeling.md)。最新三个Release目标构建成功，206／206测试通过。下面各日期章节保留此前阶段的测试记录。

`build-feedback` 三个Release目标构建成功，最新**206／206测试通过**；覆盖真实菜单动作、新工具、文件／历史及曲线空心杯身＋放样立柱＋扫掠杯耳组成的单实体奖杯。联网模型设计质量和实际渲染观感仍待主程序验收。

## AI 工具与旋转完善（2026-09-29）

AI 已登记当前功能对应的 26 个工具，覆盖建模、原生文件、STEP、历史、定位、选择、相机、空间规则和几何分析。
旋转统一角度范围并补充跨轴失败诊断，支持结果XYZ平移；圆环/半圈、下游布尔连接、历史、旧文件兼容与文件往返已验证。
Agent 已支持 24 轮/96 次调用预算、停止后的总结与本机进度兜底，新增 13 轮奖杯流程验证。
在 `D:/ForgeCAD/build-feedback` 构建三个 Release 目标成功，**191/191 测试通过**。
测试使用离线工具 JSON 与 offscreen UI，不调用 DeepSeek 服务；联网自然语言操作和三维渲染需运行主程序验收。
详见 [AI 工具说明](docs/ai-tools.md) 与 [旋转指南](docs/revolve.md)。

## 旋转建模进度（2026-09-27）

旋转特征已接入文档、依赖、属性面板和原生文件，支持矩形/圆形草图绕经过世界原点的 X/Y 轴旋转，角度 0.001–360°。创建槽为 `on_actionRevolve_triggered`，Designer QAction 命名为 `actionRevolve`。草图修改会重算，支持撤销重做和级联删除，跨轴轮廓被拒绝。Release 构建成功，**155/155 测试通过**，详见 [旋转指南](docs/revolve.md)。

## 草图、拉伸和点击定位进度（2026-09-27）

矩形和圆形草图支持 X/Y/Z 位置；拉伸支持正向、反向和对称，拉伸切除支持通孔与盲孔。草图或主体编辑会重算下游，支持参数修改、撤销重做、级联删除与原生保存/打开，详见 [草图与拉伸指南](docs/sketch-extrude.md)。

属性面板新增“点击定位”：通过精确曲面拾取或工作平面投影选择位置，黄色标记预览，单击确认、Esc 取消，一次撤销恢复 XYZ。草图保持平行于 XY 平面，尚未实现面附着和坐标轴拖动，详见 [点击定位指南](docs/click-placement.md)。Release 构建成功，**151/151 测试通过**；界面测试模拟拾取结果验证接线，实际渲染交互需在主程序中验证。

## 原生文件进度（2026-09-27）

原生 .forgecad 的 ZIP 包保存/读取、JSON 编码/解析、BRep 资产、文档恢复和未保存修改提示已实现。四个菜单槽已提供，QAction 留给用户在 Designer 添加，详见 [实现与接 UI](docs/native-file-implementation.md)。ZIP 已改用固定版本 miniz 压缩库；修改状态由历史保存点管理；STEP 显式导出 AP242 并提供转换诊断。Release 构建和主程序启动成功，**124/124 测试通过**（110 核心、14 界面）。下面的 105 项通过记录属于 9 月 26 日版本。

## 交接记录（2026-09-26）

### 已完成

- Box、Cylinder、Sphere 的创建、尺寸及 X/Y/Z 位置修改、树选中和三维显示。
- ModelDocument 统一管理模型；使用文档快照实现 Undo/Redo。
- 特征依赖图、拓扑排序、级联删除：删除被依赖的对象时，同时删除依赖它的结果，一次撤销恢复整组对象。
- 布尔差集、并集、交集：几何函数、BooleanFeature、文档接口和菜单对话框已接通。
- 布尔对象记录主体和工具的依赖顺序；修改输入参数会重新计算结果。
- 三维视图隐藏被布尔运算使用的输入对象，显示最终结果；模型树仍保留输入对象。
- 布尔对话框支持选择主体与工具，同一对象不能同时作为两者；取消不修改文档。
- 重建报告区分正常、合法空结果、计算失败、上游失败；树标红失败节点，属性区显示原因。失败布尔链展示有效上游，修正参数和历史恢复后状态重新计算。
- AI 查询和修改结果提供 rebuild_status/rebuild_message，数据操作成功与几何重建成功分开表示。
- DeepSeek 助手可查询、创建基本体、修改参数和确认后级联删除；目前尚未增加布尔工具。

- STEP 导出底层 StepIO::exportShape 已实现：毫米单位、中文路径、形状检查及 QSaveFile 原子提交。6 个测试覆盖几何往返与失败保护；“文件 → 导出 STEP…”已接入，支持默认扩展名、取消与失败提示，只导出最终几何，失败特征阻止导出；STEP 导入底层、导入特征和界面槽函数已实现，支持毫米换算、中文路径、平移、布尔运算和撤销重做；导入菜单 QAction 已接入（objectName：actionImportStep）。

### 最近验证

- Release 构建成功，**105/105 测试通过**（94 个核心测试、11 个 Qt 界面测试）。
- 实际主程序启动成功，OCCT 三维视图初始化完成。
- Windows 曾以 `0xc0e90002` 拦截 `bz2.dll` 和 `libpng16.dll`。
  已用相同版本 FreeType 2.14.3 的本地构建关闭这些可选依赖，保留普通 TrueType 字体。
  应用和 UI 测试编译结束后自动部署该 DLL，不修改系统安全策略或全局 vcpkg。

本次新版在 `build-feedback` 独立目录构建并通过全部测试，避免覆盖正在运行的旧程序。
可启动 `D:/ForgeCAD/build-feedback/bin/Release/forgecad.exe` 查看新版。
标准 `build` 目录在旧进程退出后可按下面的常规命令重新构建。

### Git 状态

布尔运算、位置参数、重建反馈、STEP 导入/导出、测试、文档和本地 DLL 构建修复已纳入版本管理。

### 接下来建议做什么

对象位置参数 **X/Y/Z 已实现**：世界坐标系、毫米单位、默认零、允许负值，范围为 ±1,000,000 mm。
Box 的位置为基准角点（沿 XYZ 正方向延伸），Cylinder 为底面圆心（沿 +Z 延伸），Sphere 为球心。
UI 输入精度为 0.001 mm；AI 可省略位置，未提供的分量默认零。位置修改支持 Undo/Redo，
布尔运算按定位后的形状重算。参数编辑保持当前相机，创建对象仍自动适配视图。
这是三轴平移；旋转、局部坐标系和装配约束尚未实现。
几何重建失败反馈和 STEP 导入/导出已实现。原生文件后端与菜单槽已实现，下一步按 [接 UI 指南](docs/native-file-implementation.md) 添加 QAction 并验证交互。

### 后续协作方式

- 用户希望分小步讲解、带着写；默认不要直接代写新功能。
- 用户明确说“你去写吧”“你自己做好”时，可直接实现该部分。
- 测试由助手负责，不要求用户手动编写测试。
- 注释使用中文，解释数据流和设计原因；以当前代码和架构文档为准。

## 技术栈
- 现代 C++20
- Qt 6（Widgets / Gui）
- OpenCASCADE（OCCT）几何内核
- CMake + vcpkg
- GoogleTest（单元测试）
- spdlog（日志）

## 构建（Windows + MSVC + vcpkg）

本机 CMake 没有加入 PATH 时，可使用以下完整路径。当前工作目录为 `D:\ForgeCAD`：

```powershell
$cmakeExe = 'D:/JetBrains/CLion 2026.2.1/bin/cmake/win/x64/bin/cmake.exe'
$ctestExe = 'D:/JetBrains/CLion 2026.2.1/bin/cmake/win/x64/bin/ctest.exe'

& $cmakeExe --preset vs2026-release -DFORGECAD_BUILD_UI_TESTS=ON
& $cmakeExe --build build --config Release --target forgecad forgecad_tests forgecad_ui_tests --parallel
& $ctestExe --test-dir build -C Release --output-on-failure
& './build/bin/Release/forgecad.exe'
```

如果清空了 build，需要先重新生成本机的 FreeType 运行库，然后配置主工程：

```powershell
& './scripts/build-local-freetype.ps1' -CMakeExe $cmakeExe
```

脚本默认使用本机 vcpkg 的 FreeType 2.14.3 源码缓存，并校验 SHA512；
缓存位置不同可传 `-ArchivePath`。生成 Release、Debug 两种 DLL 后，主工程重新配置
会自动发现 `build/local-freetype/runtime`；也可显式设置 `FORGECAD_FREETYPE_RUNTIME_DIR`。

下面是 CMake 已在 PATH 中时的通用命令：

```powershell
# 已有 build 目录时，直接增量编译即可（CMakeLists 变更会自动重新配置）：
cmake --build build --config Release

# 全新配置（指定 vcpkg 工具链；Qt 通过 CMAKE_PREFIX_PATH 指定）
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/15389/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.2/msvc2022_64

# 2. 编译
cmake --build build --config Release

# 3. 运行
.\build\bin\Release\forgecad.exe

# 4. 测试
ctest --test-dir build -C Release
```

> 注意：本机只装了 Visual Studio 2026（工具集 v180），全新配置时生成器必须写 `Visual Studio 18 2026`；写 `17 2022` 会报 MSB8020（找不到 v143 工具集）。

## 当前架构

当前代码刻意保持四个主要角色：

- `src/application/ModelDocument.*`：模型唯一所有者，也是 UI/AI 的建模入口
- `src/domain/Feature*`：基本体与 BooleanFeature 的参数和重建行为
- `src/domain/FeatureRegistry.*`：特征参数说明与创建登记
- `src/ui`、`src/assistant`：两个外部入口，都调用同一个 ModelDocument

先阅读 [`docs/current-architecture.md`](docs/current-architecture.md)。`docs/day*.md` 和
`docs/undo-redo-implementation-study.md` 记录早期学习过程，其中的 Command 架构已不再使用。

## AI 建模助手（DeepSeek）

程序右侧的“AI 建模助手”通过 DeepSeek Tool Calling 调用 ForgeCAD 的模型文档接口。
当前登记37个工具，新增轮廓、路径、变换、圆角／倒角、扫掠和放样能力，覆盖查询、基础体和草图创建、拉伸、拉伸切除、旋转、
布尔三种运算、参数修改、一次 XYZ 定位、级联删除、撤销重做、原生文件、STEP、
选择、点击定位及相机适配/缩放/平移/旋转，详见 [AI 工具说明](docs/ai-tools.md)。
空间规则明确旋转方向、环心偏移和定位支持；几何分析返回实际尺寸、体积、质心和实体数量。
每轮修改后的请求自动提供最终几何反馈，结束时附本机检查结果；一个Union特征不等于一个实体。
删除仍需本机确认；文件工具沿用覆盖确认和未保存提示；点击定位等待用户点击。
工具返回依赖和重建诊断，上游参数写入成功不等于下游几何重建成功。

运行前在系统或 IDE 的运行环境中配置：

- `DEEPSEEK_API_KEY`：必填，只保存在本机环境中，不要写入源码或提交到 Git。
- `DEEPSEEK_MODEL`：可选，默认 `deepseek-v4-flash`。
- `DEEPSEEK_BASE_URL`：可选，默认 `https://api.deepseek.com/chat/completions`。

内部调用链：

```text
Assistant UI -> AgentController -> DeepSeekClient
                                  -> ToolRegistry -> ModelDocument
```

当前使用非流式、非 thinking 模式。组合建模允许最多 24 轮、96 次工具调用；
连续三次工具/几何失败、用户取消或等待点击定位时停止后续执行。
达到预算后另发一轮禁止工具调用的总结，汇报已完成和未完成内容，网络总结失败时用本机进度兜底。
用户授权自由设计时可采用合理默认尺寸；计划须遵守现有轮廓和工具能力，恢复中断任务先查询已有模型。
自由设计先简述轮廓、比例、连接和限制再执行，不再指定固定奖杯配方；同一工具响应中的说明会在模型修改前显示。
当前反馈没有图片，设计效果需人工比较，不能以重建成功替代造型验收。

## 新对话快速接手

可以直接把下面这段发给下一位助手：

> 请先阅读 D:/ForgeCAD/README.md 和 docs/current-architecture.md，并检查工作区差异。
> 当前已完成基本体、快照 Undo/Redo、依赖图及级联删除、布尔三种运算和菜单对话框，
> 最近 105 个测试全部通过。DLL 启动问题已通过本地 FreeType 修复；相关功能已纳入版本管理。
> 后续默认带我分小步写代码，测试你负责；只有我明确要求代写时才直接改功能文件。
> 对象 X/Y/Z 位置参数已实现并覆盖负坐标、布尔更新和 Undo/Redo；几何失败反馈也已完成，下一步按 docs/native-file-first-steps.md 学习原生文件。
