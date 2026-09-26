# ForgeCAD

一个使用 C++、Qt 和 OpenCASCADE 开发的参数化三维建模学习项目。

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
几何重建失败反馈和 STEP 导入/导出已实现。下一步按 [原生文件教学](docs/native-file-first-steps.md) 分步实现保存/打开；原生文件需要保存导入模型的 BRep 几何资产。

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
当前支持查询特征、创建 Box/Cylinder/Sphere、按 Feature ID 修改数值参数，
以及在本机确认后删除指定特征。若该特征有依赖者，确认框会列出所有连带删除的 ID；
整组删除可通过一次撤销恢复。

运行前在系统或 IDE 的运行环境中配置：

- `DEEPSEEK_API_KEY`：必填，只保存在本机环境中，不要写入源码或提交到 Git。
- `DEEPSEEK_MODEL`：可选，默认 `deepseek-v4-flash`。
- `DEEPSEEK_BASE_URL`：可选，默认 `https://api.deepseek.com/chat/completions`。

内部调用链：

```text
Assistant UI -> AgentController -> DeepSeekClient
                                  -> ToolRegistry -> ModelDocument
```

第一版使用非流式、非 thinking 模式，并将 Agent 工具循环限制为最多 6 步。

## 新对话快速接手

可以直接把下面这段发给下一位助手：

> 请先阅读 D:/ForgeCAD/README.md 和 docs/current-architecture.md，并检查工作区差异。
> 当前已完成基本体、快照 Undo/Redo、依赖图及级联删除、布尔三种运算和菜单对话框，
> 最近 105 个测试全部通过。DLL 启动问题已通过本地 FreeType 修复；相关功能已纳入版本管理。
> 后续默认带我分小步写代码，测试你负责；只有我明确要求代写时才直接改功能文件。
> 对象 X/Y/Z 位置参数已实现并覆盖负坐标、布尔更新和 Undo/Redo；几何失败反馈也已完成，下一步按 docs/native-file-first-steps.md 学习原生文件。
