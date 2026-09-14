# ForgeCAD 当前架构

本文是当前代码的唯一架构入口。`day*.md` 和 Undo/Redo 旧笔记只记录历史学习过程，
不代表现在的实现。

## 一分钟总览

```text
MainWindow ─┐
            ├─> ModelDocument ─> Feature ─> ShapeFactory ─> OCCT Shape
AI Tools ───┘          │
                       └─> FeatureRegistry
```

- `MainWindow`：显示界面，把用户意图交给文档。
- `ToolRegistry`：检查 AI 工具参数，把合法意图交给文档。
- `ModelDocument`：拥有所有 Feature，完成创建、修改、删除和 Undo/Redo。
- `FeatureRegistry`：描述可用特征，并创建对应的具体 Feature。
- `Feature`：保存一个模型对象的参数，通过 `rebuild()` 生成几何形状。

## 最重要的公开接口

日常开发先看 `ModelDocument` 的这些方法：

```cpp
Feature& createFeature(type, parameters);
void setParameter(featureId, parameterName, value);
void deleteFeature(featureId);
void undo();
void redo();
const auto& features() const;
```

UI 和 AI 不直接增删 `features_`，也不各自实现参数校验。这样只有一条写入路径。

## 一次创建如何运行

```text
MainWindow 或 ToolRegistry
  -> ModelDocument::createFeature
  -> FeatureRegistry::create
  -> BoxFeature / CylinderFeature / SphereFeature
  -> ModelDocument 保存 Feature
  -> UI 读取 features() 并刷新树和三维视图
```

## Undo/Redo 为什么现在使用快照

每次成功修改前，`ModelDocument` 保存一份只包含 ID、类型和参数的数据快照。
Undo 恢复上一份快照，Redo 恢复下一份快照。历史记录完全封装在 Document 内部，
调用方只看到 `undo()` 和 `redo()`。

这是当前小型模型最容易理解和验证的方案。出现下列情况时，再把 Document 内部替换成
Command 或增量历史；它的公开接口不需要改变：

- 单个文档达到数百或数千个复杂特征；
- 快照内存或恢复时间可以被实际测量为瓶颈；
- 出现草图依赖、布尔链、装配关系等增量操作。

恢复快照会重建 Feature 对象，因此不要跨 Undo/Redo 长期保存 `Feature*` 或引用。
长期身份使用稳定 ID，例如 `Box001`，需要对象时重新调用 `findFeature(id)`。

## 增加一种新特征

以未来的 Cone 为例：

1. 新建 `ConeFeature.h/.cpp`，实现参数、校验和 `rebuild()`。
2. 在 `FeatureRegistry.cpp` 登记参数范围和创建代码。
3. 在 CMake 中加入 `ConeFeature.cpp`。
4. 添加 Feature 与 Registry 测试。
5. 在 UI 中增加入口和中文显示名。

不需要修改 ModelDocument、Undo/Redo 或 AI 的通用参数处理流程。

## 推荐阅读顺序

1. `application/ModelDocument.h`
2. `domain/Feature.h`
3. `domain/BoxFeature.cpp`
4. `domain/FeatureRegistry.cpp`
5. `application/ModelDocument.cpp`
6. `ui/mainwindow.cpp` 或 `assistant/ToolRegistry.cpp`

先理解公开接口和一次完整操作，再阅读私有快照实现。
