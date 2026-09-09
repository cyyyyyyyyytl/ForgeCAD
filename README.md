# ForgeCAD

一个 C++ 参数化三维建模与几何分析平台（骨架阶段）。

## 技术栈
- 现代 C++20
- Qt 6（Widgets / Gui）
- OpenCASCADE（OCCT）几何内核
- CMake + vcpkg
- GoogleTest（单元测试）
- spdlog（日志）

## 构建（Windows + MSVC + vcpkg）
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

## 模块
- `src/core` 核心基础设施（Result/Error/Logger/Version）
- `src/domain` 领域模型（Feature/Body/Parameter/Shape）
- `src/geometry` 几何操作（ShapeFactory/Boolean/Transform/Analyzer）
- `src/application` 应用层（Command/TaskManager）
- `src/infrastructure` 基础设施（StepIO/Json/Config/Persistence）
- `src/threading` 线程池/任务系统
- `src/ui` Qt GUI
- `tests/` GoogleTest 单元测试
- `benchmarks/` 性能基准
- `docs/` 架构/设计/性能文档
- `docs/SESSION_LOG.md` 每日学习/工作日志（教学交接用，最新条目在顶部）

## AI 建模助手（DeepSeek）

程序右侧的“AI 建模助手”通过 DeepSeek Tool Calling 调用 ForgeCAD 的 C++ 建模服务。
当前支持查询特征、创建 Box/Cylinder/Sphere，以及按 Feature ID 修改数值参数。

运行前在系统或 IDE 的运行环境中配置：

- `DEEPSEEK_API_KEY`：必填，只保存在本机环境中，不要写入源码或提交到 Git。
- `DEEPSEEK_MODEL`：可选，默认 `deepseek-v4-flash`。
- `DEEPSEEK_BASE_URL`：可选，默认 `https://api.deepseek.com/chat/completions`。

内部调用链：

```text
Assistant UI -> AgentController -> DeepSeekClient
                                  -> ToolRegistry -> ModelingService -> ModelDocument
```

第一版使用非流式、非 thinking 模式，并将 Agent 工具循环限制为最多 6 步。
