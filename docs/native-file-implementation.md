# 原生文件：实现与接 UI

`.forgecad` 是 ForgeCAD 自己的原生文档包，不与其他 CAD 软件原生格式兼容。
ZIP 使用固定版本 miniz 3.1.2 静态库，CMake 下载带 SHA256 校验的上游源码。
新文件使用 Deflate 压缩，同时兼容之前的 stored-v1 文件。ZIP 解码与 CRC 交给库处理，
应用负责拒绝重复/非法路径、加密/不支持条目，并在解压前检查声明的总大小。
文件大小与解压总大小分别限制为 64 MiB；最多 10000 个特征、10002 个包条目，JSON 最大 16 MiB。
所有资源只解压到内存，不写到任意磁盘路径。

## 在 Designer 添加四个 QAction

不需要手写连接，也不要重复生成槽函数。MainWindow 已提供以下槽：

| text | objectName | 建议快捷键 |
|---|---|---|
| 新建文档 | actionNewDocument | Ctrl+N |
| 打开… | actionOpenDocument | Ctrl+O |
| 保存 | actionSaveDocument | Ctrl+S |
| 另存为… | actionSaveDocumentAs | Ctrl+Shift+S |

加在文件菜单里，保持大小写完全一致。原来的新建基本体菜单用于追加特征，
与这里的“新建空文档”是两种操作。保存 .ui 后重新构建并退出旧程序。

新建、打开和关闭会检查未保存修改，提供保存、放弃、取消。
取消保存或保存失败时不继续丢弃当前模型。另存为保留文档 UUID。
打开成功清空 Undo/Redo；历史栈不写入文件。相机、颜色、选择状态尚不持久化。

## 数据怎么流动

1. `ModelDocument::exportData()` 从特征取 ID、类型、版本、参数和依赖。
   ImportedFeature 的原始几何用 BRepTools 编码，位置另外保存，避免重复平移。
2. `DocumentJson::encode()` 把结构化数据变成 UTF-8 JSON。
   只记录几何资源名，不把 BRep 文本塞进 JSON。
3. `NativeDocumentIO::save()` 校验定义，将 manifest.json、document.json 和
   shapes/*.brep 封装成 ZIP。QSaveFile 完成原子替换，失败保留旧文件。
4. `NativeDocumentIO::load()` 检查 ZIP 目录、条目边界、重复名字、路径和 CRC32，
   再解析 JSON 和几何资源文本；这里不重复构造文档。
5. `NativeDocumentIO::prepare()` 调用 `replaceData()`，在临时文档准备所有对象及依赖图；
   MainWindow 确认未保存修改后读取一次，准备成功再用 `ModelDocument::swap()` 提交。
   重复 ID、未知类型、未来版本、错误编号、非法参数、循环依赖、缺失或损坏 BRep 都拒绝。
6. MainWindow 刷新模型树、属性区和视口。几何由定义重新计算，重建诊断不作为文件权威数据。

`documentId` 表示整个模型的身份；`Box001` 表示模型内一个对象的身份。
`sequences` 表示已分配的编号上限，删除或撤销后仍保留，打开后继续创建不会复用旧编号。
布尔依赖顺序是主体、工具，不能排序。保存结构合法但重建失败的布尔定义，打开后重新显示诊断。

## 后端调用

```cpp
auto data = document.exportData();
forge::infrastructure::NativeDocumentIO::save(data, path);

auto prepared = forge::infrastructure::NativeDocumentIO::prepare(path);
document.swap(*prepared);
```

失败抛异常；Qt 槽已捕获标准异常和 OCCT 异常并显示提示。
文件读取不直接修改当前文档；只有临时文档准备成功，swap 才完成模型替换。

保存点由文档的历史版本标记管理，不序列化 JSON/BRep 来判断修改。成功保存调用 markSaved；
撤销回保存点时变为未修改，重做离开保存点或产生新分支时恢复修改标记。窗口标题显示星号。
把参数设置为原值不产生新历史，也不会错误标记为修改。

新增测试覆盖数据导出、中文路径文件往返、导入几何、身份与历史、
损坏文件、保存失败保护、非法依赖和参数、编号以及合法空布尔结果。
2026-09-27：Release 应用、核心测试和界面测试构建成功，124/124 测试通过（110 核心、14 界面）。
独立 .NET ZIP 写入器产生的压缩包也可读取；测试覆盖 ZIP 损坏、重复路径、越界路径和压缩炸弹。
主程序启动检查通过。Windows 智能应用控制当前已关闭；项目代码没有修改安全策略。

## STEP 的明确范围

原生“打开/保存”恢复 ForgeCAD 的模型定义；STEP“导入/导出”用于几何交换。
当前 STEP 导出显式指定 AP242，长度单位毫米。导入统一换算到毫米，
保留原始几何位置，把全部成功转换的根形状作为一个 Imported 特征追加。
不把中性 STEP 当作参数化建模历史，也不覆盖当前原生文档路径和保存点。

导入结果提供根对象数、已转换数与 OCCT 转换诊断；部分根转换失败时拒绝整个导入。
错误框可展开详细诊断，成功后的状态栏显示根数量，提示文字说明几何交换范围。
当前不保存产品名称、颜色、装配实例树或 PMI；AP242 导出不代表支持其全部能力。
装配和元数据需要后续引入 XDE/STEPCAFControl 及对应文档模型。

当前文件读取和几何转换仍同步执行，STEP 文件上限 256 MiB。
大模型的后台任务、进度与真正的内核取消是后续工作，不能声称已实现。
STEP 导出不会把文档标记为已保存；应保存 .forgecad 才能恢复可编辑模型。

官方设计参考：
- https://github.com/Open-Cascade-SAS/OCCT/wiki/step
- https://github.com/Open-Cascade-SAS/OCCT/wiki/ocaf
- https://github.com/richgel999/miniz/tree/3.1.2

第三方 miniz 的许可证在 CMake 获取的源码 LICENSE 中；分发程序时必须随包保留。