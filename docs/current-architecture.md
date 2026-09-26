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

## 依赖图目前的位置

`DependencyGraph` 保存“哪个 Feature 依赖哪个 Feature”的 ID 关系，并由 `ModelDocument`
拥有。创建特征会登记节点；`addDependency()` 可登记关系并拒绝循环。依赖关系和特征
一起进入 Undo/Redo 快照。`topologicalOrder()` 把被依赖者排在使用者之前；删除则先预览
整组受影响的特征，再按相反顺序一次删除，撤销可一次恢复整组。

AI 删除始终弹确认框；普通界面删除会在存在下游特征时弹框，两处都会列出级联范围。
Box、Cylinder、Sphere 独立构造几何；BooleanFeature 按主体、工具的依赖顺序读取上游形状，
参数修改后按拓扑顺序重建，布尔结果随之更新。

## 最重要的公开接口

日常开发先看 `ModelDocument` 的这些方法：

```cpp
Feature& createFeature(type, parameters);
Feature& createBooleanFeature(operation, baseId, toolId);
void setParameter(featureId, parameterName, value);
void deleteFeature(featureId);
void undo();
void redo();
const auto& features() const;
auto rebuildShapes() const;
auto visibleFeatureIds() const;
```

UI 和 AI 不直接增删 `features_`，也不各自实现参数校验。这样只有一条写入路径。

## 布尔特征和视图

“布尔运算”菜单提供差集、并集和交集，两个下拉框按稳定 ID 选择主体与工具形状。
相同输入不能确认，取消不修改模型。创建特征及两条依赖只记为一次 Undo。

`BooleanFeature` 保存运算种类；上游 ID 按主体、工具顺序保存在依赖图中。
`rebuildShapes()` 按拓扑顺序计算并传入上游 Shape，`visibleFeatureIds()` 隐藏被布尔
特征使用的输入和中间结果。模型树仍保留全部特征；删除结果或撤销会恢复输入的可见性。
AI 工具目前仍只提供基础体创建，尚未提供布尔运算入口。

菜单及对话框自动化测试放在 `forgecad_ui_tests`，通过
`FORGECAD_BUILD_UI_TESTS=ON` 启用，核心测试与之分开。菜单、对话框和撤销重做测试已通过。

## 本机运行库修复

本机 Windows 签名策略拦截 vcpkg FreeType 引入的 `libpng16.dll`、`bz2.dll`。
运行 `scripts/build-local-freetype.ps1`（可用 `-CMakeExe` 指定 CMake 路径），
从 vcpkg 源码缓存校验并构建同版本 FreeType 2.14.3 的 Release 和 Debug DLL。
该版本关闭压缩字体和 PNG 字体位图等可选外部依赖，普通 TrueType 字体仍可使用。
重新配置主工程后，CMake 自动发现 `build/local-freetype/runtime`，在应用和 UI 测试
部署的最后一步复制本地 DLL。也可用 `FORGECAD_FREETYPE_RUNTIME_DIR` 指定目录。
此修复不修改系统安全策略和全局 vcpkg 安装。

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

## 基本体位置

基本体的尺寸参数后保存 x/y/z 三个位置参数，单位毫米，默认零。
Box 使用基准角点，Cylinder 使用底面圆心并沿 +Z 构造，Sphere 使用球心。
位置定义在世界坐标系中；ShapeFactory::translate 用 OCCT Location 应用纯平移，
不改变尺寸和拓扑，布尔输入使用定位后的形状。当前不支持旋转和局部坐标系。

Registry 将位置登记为可选参数，省略任一分量时取零，尺寸仍必填，未知参数被拒绝。
位置范围 ±1,000,000 mm；尺寸和位置都拒绝 NaN/Infinity。快照保存全部参数，
不另存视口变换，因此撤销、重做和删除恢复都能保留位置。
UI 新建与属性面板显示毫米，支持三位小数，参数编辑时保持相机。

## 几何重建状态与失败反馈

`ModelDocument::rebuildReport()` 按拓扑顺序返回 ID -> ShapeResult，状态分为 Ready、Empty、Failed、Blocked。
Feature 的默认 rebuildResult 检查领域参数并检查重建后的拓扑；BooleanFeature 使用带内核诊断的布尔接口。
OCCT 的错误报告或异常保留在 message 中。每个特征单独处理失败，独立分支仍然重建。

合法空结果是非 Null 的空 Compound；空交集、主体被完全减去都不是失败。
空结果仍可作为布尔输入，按空集合的差集、并集、交集规则计算。
失败输入让下游成为 Blocked，并列出直接失败的输入 ID。

MainWindow 在一次刷新中共享同一报告：失败和阻塞节点标红，空结果显示单独状态，
属性区与状态栏给出原因。成功或合法空布尔结果继续隐藏输入；
失败布尔链则递归显示仍有效的上游，便于检查。参数编辑不重新生成属性控件，保持输入焦点。
状态不进入快照，修改参数、撤销重做及恢复删除后均重新计算。
AI 工具额外返回 rebuild_status/rebuild_message，数据写入成功不代表几何生成成功。

原生文件教学从 [native-file-first-steps.md](native-file-first-steps.md) 开始，当前尚未实现保存/打开。

## STEP 导出：当前教学进度

infrastructure/StepIO 提供 exportShape(shape, filePath)，输入当前世界坐标几何，不修改文档。
先复用 ShapeFactory::inspectShape 拒绝空/损坏形状，然后设置源坐标和输出单位为毫米，
通过 STEPControl_Writer::Transfer 转换、WriteStream 生成完整数据，最后 QSaveFile 原子提交。
文件提交失败保留旧文件，QString 路径支持中文。当前使用内存缓冲，面向现有小模型；
大模型后续可使用临时文件流并加入进度和取消。

新增 tests/test_step_io.cpp 的 6 个用例验证中文路径、明确毫米单位、多个实体、负坐标与
布尔孔的体积/质心/实体数量往返，拒绝无效/空形状，错误路径和锁定文件失败时保留旧数据。
生产 STEP 读取、ImportedFeature 和导入槽函数已实现，导入菜单 QAction 已接入并通过实际菜单触发测试。
“文件 → 导出 STEP…”已通过 QAction 自动连接；QFileDialog 提供 .step/.stp 过滤、默认 .step 后缀和覆盖确认。
ModelDocument::shapeForExport 只读重建并收集最终结果；任何失败/阻塞都拒绝导出，
正常最终结果中的空集合跳过并说明，全部为空则拒绝导出。
取消不写文件，导出不修改 Undo/Redo、选择或相机。
文档测试验证最终结果筛选、混合空结果与失败拒绝；UI 测试通过真实菜单和对话框导出，
再回读 STEP 验证差集体积，覆盖默认扩展名、取消和空文档提示。

## STEP 导入

StepIO::importShape 使用 QFile 读取 Unicode 路径，再通过 STEPControl_Reader::ReadStream 解析。
当前同步读取上限为 256 MB，文件单位统一转成毫米。要求全部根对象成功转换并通过几何有效性检查，
失败不返回部分模型。多个实体和原始位置作为一个几何组保留，当前不恢复装配对象树、颜色或原软件的参数历史。

ModelDocument::createImportedFeature 追加一个 Imported001 类型对象，保留现有模型，一次操作占一条 Undo。
ImportedFeature 保存独立复制的原始 B-Rep、来源文件名以及相对原始几何的 x/y/z 额外平移。
Registry 仅提供它的位置参数规则，不允许使用数值参数创建导入对象，AI 也不提供任意文件导入工具。
导入对象能参与布尔依赖与级联删除，也可随其他最终结果再次导出 STEP。

基本体快照仍保存参数；导入快照额外共享不可变的原始几何，恢复不依赖源文件。
原生文件将来必须保存这些对象的 B-Rep，而不能只存来源路径或数值参数。

MainWindow::on_actionImportStep_triggered 已实现选择文件、取消、错误提示、文档追加与显示刷新。
mainwindow.ui 的文件菜单已添加 QAction，objectName 为 actionImportStep，文字为“导入 STEP…”。
Qt 自动连接同名槽，不必再改头文件或手工 connect。未添加 QAction 前，测试通过元对象直接调用槽验证流程。
