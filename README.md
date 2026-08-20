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
# 1. 配置（指定 vcpkg 工具链；Qt 通过 CMAKE_PREFIX_PATH 指定）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/15389/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.2/msvc2022_64

# 2. 编译
cmake --build build --config Release

# 3. 运行
.\build\bin\Release\forgecad.exe

# 4. 测试
ctest --test-dir build -C Release
```

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
