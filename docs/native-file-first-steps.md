# CAD 原生文档设计与分步实现

本文是下一阶段的教学设计，保存/打开功能尚未实现。采用 CAD 的应用/文档持久化方式：
先定义文档合同和数据导出，再实现包内编码、文件容器、事务式打开与保存，最后接入界面。

## 设计依据与本项目选择

CAD 没有统一的原生文件格式，不能把一种 JSON、ZIP 或二进制格式称为行业强制规范。
OCCT OCAF 提供 Application/Document、标签/属性、事务和 BinOcaf/XmlOcaf 持久化机制；
FreeCAD 的 FCStd 使用 ZIP 容器，分别保存文档对象、GUI 表示和 B-Rep 数据。

参考官方资料：
- https://github.com/Open-Cascade-SAS/OCCT/wiki/ocaf
- https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/File_Format_FCStd.md

ForgeCAD 已有自己的 ModelDocument、依赖图和历史，不同时再引入第二份 OCAF 文档作为模型真相源。
本阶段选择自定义文档持久化，.forgecad 使用 ZIP 文档包；JSON 只用于包内结构化数据。
这是结合现有架构的设计选择，并不声称与 FCStd 或其他 CAD 原生格式兼容。
如果将来采用 OCAF，需要整体规划文档所有权、事务和拓扑命名的迁移，不能仅换一个保存函数。

建议的文档包结构：

```text
part.forgecad (ZIP)
  manifest.json       # 格式标识、容器版本、所需能力、入口文件
  document.json       # 文档身份、单位、特征、参数、依赖、编号计数
  view.json           # 可选：颜色、显隐、相机等表现数据
  shapes/*.brep       # 可选：导入形状或带版本信息的几何缓存
  thumbnail.png       # 可选：预览图
```

第一版必须实现 manifest 和 document；其余仅保留扩展位置，不写没有使用的占位文件。
几何缓存不是参数模型的唯一真相；缓存缺失或不匹配时可重建。
后续 STEP 导入的对象如果不能用参数重新生成，则其 B-Rep 是必要数据，必须保存。

## 文档合同与验收底线

- 对象身份与显示名称分开；当前稳定 ID 在打开后保持不变，不按树行号或类型重新编号。
- 文档本身具有持久身份；另存为保留还是创建新身份必须明确，复制为新文档是独立操作。
- 明确世界坐标、长度单位与位置的归属；当前 x/y/z 表示世界坐标平移，未来姿态可迁移成 Placement。
- 布尔输入有明确主体/工具角色，依赖顺序不得丢失；普通依赖也保留。
- 格式版本、特征类型版本和迁移有明确规则；第一版拒绝无法安全解释的新版本，不能静默丢数据。
- 文档数据、显示状态、会话状态、派生几何各自归属清楚，不能混存内存对象。
- 保存原子提交，失败保留旧文件；打开先在临时文档完整校验，成功后再替换当前文档。
- 保存、另存为、打开、关闭遵守未保存修改提示；取消与失败不改变当前文档。
- 重建失败不能导致参数历史丢失；允许保存可编辑的失败模型，并在打开后重新显示诊断。
- ZIP 限制条目数量、解压大小和路径，拒绝重复入口、越界路径以及缺少必需数据的文件。

这些是本项目的验收要求，不能因先做教学小步而省略最终实现。

## 先理解要保存什么

原生文件保存建模数据：稳定 ID、类型、全部数值参数、每个特征按顺序排列的依赖，以及 ID 分配计数器。
Box/Cylinder/Sphere 的参数包括 x/y/z。布尔类型用现有的 Cut/Union/Intersection，dependencies 的第一个是主体，第二个是工具。
一般特征的手工依赖也必须保留，不能只保存布尔输入。

不保存 Feature 指针、unique_ptr、AIS 实例或内存中的 TopoDS_Shape 对象。
如保存几何，使用 OCCT 的 B-Rep 持久化编码；本阶段可重建基本体无需几何缓存。
重建状态和 Undo/Redo 栈属于派生状态与会话数据，第一版不作为文件权威数据。
打开时用类型和参数重建 Feature，再恢复依赖关系，最后重新计算几何状态。
重建失败的文档依然允许保存；结构合法和几何成功是两个不同的判断。

## 第一步：定义公开的数据结构

建议新建 src/application/DocumentData.h，使用普通 C++，不在应用层头文件中引入 QJsonObject。

```cpp
#pragma once
#include "domain/FeatureRegistry.h"
#include <map>
#include <string>
#include <vector>

namespace forge::application {
struct FeatureData {
    std::string id;
    std::string type;
    domain::NumericParameters parameters;
    std::vector<std::string> dependencies; // 布尔顺序：主体、工具。
};

struct DocumentData {
    std::vector<FeatureData> features;      // 保留文档顺序。
    std::map<std::string, int> sequences;   // 已分配序号，不因删除而回退。
};
}
```

它类似 ModelDocument 的私有快照，但服务于文件边界。不要直接公开 DocumentState：
快照保存内部 DependencyGraph，并且有不同的 ID 计数器恢复语义。

接着为 ModelDocument 增加 `DocumentData exportData() const;`。
实现按 features_ 顺序读取 id/type/parameters；每个特征的 dependencies 取自
`dependencyGraph_.dependenciesOf(feature->id())`，sequences 复制 sequenceByType_。
这里只导出数据，不修改文档，不重建几何，不记录 Undo。

第一步验收：建两个带位置的基本体及一个 Cut，导出后应有三个条目，
Cut 的 dependencies 恰好按 [Box001, Cylinder001] 排列。由助手负责写测试。

## 第二步：规定包内文档格式

扩展名为 .forgecad，文件本身是 ZIP 包。包内 document.json 使用 UTF-8 JSON，便于阅读、排错和版本迁移。
manifest.json 单独声明 ForgeCAD 格式标识、包版本与 document.json 入口。下面是包内建模数据示例，
实现时还要加入文档持久身份和各特征数据版本：
例如一个箱体减去偏心圆柱：

```json
{
  "format": "ForgeCAD",
  "version": 1,
  "units": "mm",
  "sequences": {"Box": 1, "Cylinder": 1, "Cut": 1},
  "features": [
    {"id": "Box001", "type": "Box",
     "parameters": {"length": 10, "width": 10, "height": 10, "x": 0, "y": 0, "z": 0},
     "dependencies": []},
    {"id": "Cylinder001", "type": "Cylinder",
     "parameters": {"radius": 1, "height": 12, "x": 3, "y": 4, "z": -1},
     "dependencies": []},
    {"id": "Cut001", "type": "Cut", "parameters": {},
     "dependencies": ["Box001", "Cylinder001"]}
  ]
}
```

format 防止误读其他 JSON；version 为以后升级留入口；units 第一版只接受 mm。
不要根据文件里的任意类型名称执行任意代码，只使用现有 Registry 和布尔白名单。

## 第三步：包内编码、ZIP 容器与原子保存

在 src/infrastructure/NativeDocumentIO.h/.cpp 中把 DocumentData 与 QJsonDocument 相互转换。
从内存 JSON 往返测试开始，然后用 ZIP 容器组织条目，再加读写路径。
容器解析、模式迁移与 ModelDocument 业务恢复分层处理，禁止把解压动作直接写入模型。

保存使用 QSaveFile：写临时文件，检查写入字节数，最后 commit()；任何失败都返回明确原因。
不能直接先截断原文件，避免写到一半丢失已有模型。
加载时限制压缩及解压大小、条目数量和特征数量，严格检查字段类型；QJsonValue::toDouble/toInt 的默认值
不能当成“字段校验”，特别要拒绝非数字、非整数版本和无穷数值。

## 第四步：安全导入文档

为 ModelDocument 增加 importData。不能循环调用 createFeature/createBooleanFeature：
它们会重新分配 ID，并产生多条历史，依赖也可能因文件顺序而暂时找不到。

先在临时容器中建立全部 Feature 和所有图节点，再按保存的顺序添加依赖边。
拒绝重复或空 ID、未知类型、非法参数、缺失引用、自依赖、重复边、循环、布尔输入不是两项等错误。
校验 sequences 为合法计数，同时确保分配新 ID 不会与现存 ID 冲突。

全部结构检查通过后，准备好容器、图和计数器，再一次性 swap 替换当前文档。
任何解析或校验失败都必须保留原文档及历史。成功打开新文档后清空 Undo/Redo；
几何重建失败则保留新文档，用现有状态反馈显示问题，允许用户继续修正。

## 第五步：菜单与修改状态

接入保存、另存为、打开、文件路径、窗口标题和未保存修改提示。
取消文件对话框不改变任何状态；保存失败不能清除“已修改”标记；
打开前如有未保存修改，提供保存/放弃/取消，只有确实保存成功才继续。

第一版暂不保存历史：打开后的 Undo 栈为空。
修改状态可以先比较当前 exportData 与“上次成功保存的数据”，
这样撤销回到保存状态时能准确去掉标题中的星号，之后再考虑优化。

## 后续验证

- 保存→打开，ID、参数、负坐标、特征顺序与依赖顺序一致。
- 布尔链和合法空结果能恢复；失败重建状态重新推导。
- 删除过的高序号不会在重新打开后被重复分配。
- 损坏文件、未知版本、重复 ID、缺失引用及循环不会覆盖当前文档。
- 保存错误保留旧文件；取消打开保留当前模型。
- 文件往返后仍能修改参数、创建特征和进行新的 Undo/Redo。
- 容器版本不支持、入口缺失、ZIP 损坏、重复条目、过量解压等情况下旧文档保持完整。
- 显示状态与建模数据分别恢复；B-Rep 缓存失效时不覆盖参数定义。


## STEP 导入后的补充

当前已存在 ImportedFeature，原始几何不能由 x/y/z 重新生成。
上述 DocumentData 的教学雏形在用于正式原生保存前，必须扩展导入对象的几何资产引用，
并将原始 B-Rep 写入 shapes/ 条目；打开时从资产恢复，而不能依赖外部 STEP 路径。
数值参数、依赖和来源文件名仍要保留。快照里的 shared_ptr 是运行时历史机制，不能直接写入文件。
