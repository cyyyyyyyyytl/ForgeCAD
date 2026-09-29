# AI 工具与当前能力

AI 通过 DeepSeek Function Calling 生成工具名和 JSON 参数，`AgentController`
统一处理确认与刷新，`ToolRegistry` 校验并调用 `ModelDocument`。
文件、选择和相机需要窗口上下文，通过 `UiToolHandler` 转交
`MainWindow::executeAssistantUiTool`，不会通过触发 void 槽函数猜测是否成功。

## 已登记的 37 个工具

下表为原有26个；新增11个轮廓／路径创建编辑、变换、选边查询／更新、圆角／倒角、扫掠和放样工具及参数见 [扩展建模](advanced-modeling.md#ai-注册)。Cone使用现有create_feature入口。

| 操作 | 工具 | 关键参数 |
|---|---|---|
| 列出/查询对象 | `list_features`、`get_feature` | `feature_id` |
| 查询类型与参数规则 | `get_feature_types` | 无 |
| 查询空间规则与定位支持 | `get_spatial_rules` | 无，也随 `get_feature_types` 返回 |
| 分析实际几何 | `analyze_geometry` | 可选 `feature_id`；省略分析文档最终结果 |
| 查询文档、选择、历史和失败状态 | `get_document_status` | 无 |
| 创建基础体或草图 | `create_feature` | `type`、`parameters` |
| 修改一个参数 | `set_parameter` | `feature_id`、`parameter_name`、`value` |
| 一次设置 XYZ | `set_position` | `feature_id`、`x`、`y`、`z` |
| 拉伸 | `create_extrude` | `sketch_id`、`height`、可选 `direction` |
| 拉伸切除 | `create_extrude_cut` | `base_id`、`sketch_id`、`height`、可选 `direction` |
| 旋转实体 | `create_revolve` | `sketch_id`、`angle`、可选 `axis` |
| 差集/并集/交集 | `create_boolean` | `operation`、`base_id`、`tool_id` |
| 级联删除 | `delete_feature` | `feature_id`，本机确认 |
| 撤销/重做 | `undo`、`redo` | 无 |
| 新建/打开原生文档 | `new_document`、`open_document` | 打开时可选 `path` |
| 保存/另存为 | `save_document`、`save_document_as` | 另存为可选 `path` |
| STEP 导入/导出 | `import_step`、`export_step` | 可选 `path` |
| 选择对象 | `select_feature` | `feature_id` |
| 点击定位/取消 | `begin_point_placement`、`cancel_point_placement` | 启动时 `feature_id` |
| 适配/缩放/平移/旋转相机 | `control_view` | `operation`；缩放 `factor`；平移/旋转 `dx`、`dy` |

create_feature类型是 `Box`、`Cylinder`、`Sphere`、`Cone`、`RectangleSketch`、`CircleSketch`。
尺寸与XYZ使用毫米，angle／rx/ry/rz使用度。草图plane支持XY/XZ/YZ，自定义轮廓使用create_profile。
基础体与草图的 XYZ 表示世界基准位置；STEP 导入对象的 XYZ 是相对原始几何的附加平移。
Revolve 的 XYZ 是旋转生成后的世界平移量，替换已有值而非累计；目标环心=原始环心+XYZ。
拉伸方向使用 `forward`、`reverse`、`symmetric`；默认正向，对称两侧各一半总高度。
旋转轴使用X/Y/Z，默认Y，经过世界原点，须平行草图平面；禁止跨越轴的径向边界。
绕 Y 保持点的 y，旋转 x/z；绕 X 保持 x，旋转 y/z。以 Z 为竖直时，
绕 Y 的圆环中心圆平行 XZ，绕 X 平行 YZ，不能把绕 Y 的圆环称为水平环。
对于 z=0 的圆形草图，圆心 (10,20,0)、半径2、绕Y整圈得到环心(0,20,0)、
中心圆半径10、管半径2（管直径4）；圆心(25,10,0)绕X则环心(25,0,0)。
环心可沿轴偏移，不一定是原点；改变草图z会改变扫掠几何，并非把结果沿z平移。
矩形轮廓也能旋转生成实体，旋转不只生成圆环。
布尔操作使用 `difference`、`union`、`intersection`，差集为主体减去工具。

`set_parameter` 保持数值协议：axis的0/1/2表示X/Y/Z，plane的0/1/2表示XY/XZ/YZ，
`direction` 的 0/1/2 表示正向/反向/对称。查询规则会返回这些枚举的含义。
相机的 `dx`/`dy` 是像素增量，向右/下为正；缩放 `factor>1` 放大。
相机操作与对象参数化旋转不同，不修改模型历史。

`get_feature_types` 和对象查询返回 `supports_position`。
Box/Cylinder/Sphere/Cone/RectangleSketch/CircleSketch/ProfileSketch/Path3D/Transform/Imported/Revolve可使用set_position；
Extrude/ExtrudeCut/Cut/Union/Intersection没有直接XYZ参数，可用create_transform创建依赖变换结果。
可先造圆环/半环，再平移Revolve到侧面做简化杯耳，分析后与杯身合并。
Revolve结果平移不改变输入草图坐标；create_transform可绕指定pivot旋转和平移实体／线框。

## 几何分析与自动反馈

`analyze_geometry({"feature_id":"Union001"})` 查询该特征；省略参数则分析当前文档
最终可用结果，排除被消费的布尔输入，不把上游和下游的体积重复相加。
整文档含失败/阻塞时返回失败，不分析为显示兜底而展开的上游；指定有效独立特征仍可分析。
最终空分支保留在 `feature_ids` 中，但不参与非空几何合并。

`geometry` 返回：
- `valid`、`empty`、`solid_body`、`solid_count`、`single_solid`；
- 世界坐标 `bounds_mm.min/max/size`（各含x/y/z）、`volume_mm3`、`surface_area_mm2`；
- `volume_centroid_mm` 和唯一拓扑面/边/顶点数量 `topology`；
- `warnings` 与分析限制。

分析使用精确 B-Rep，不依赖相机和显示网格。实体数按唯一拓扑实体计数，
不是 Feature 数或 Union ID 数。纯实体集合的体积是几何成员的属性累加，
未经布尔合并而相互重叠的独立实体可能重复占据空间，不能将它当成融合后的体积。
空结果的包围盒/质心为空、体积0；草图或混合几何的体积/体积质心为null，不能当作0。
包围盒重叠不能证明真实相交；单实体不证明壁厚、强度、制造可靠性或审美合格。

Agent 每轮发生模型修改后，在下一次模型请求前自动执行整文档分析，
把事实作为本轮临时 system 反馈发送给模型。没有额外网络轮次、不占模型工具调用预算，
不增加模型历史，不永久写入旧几何数据。后续修改后的请求重新测量。
正常建模结束和中断进度中会附本机实体数量/尺寸或分析失败信息，避免只显示模型自行总结。
一体目标遇多个实体时提示先修正连接；装配等多个实体目标仍合法，宿主不自动删除或移动模型。
提示要求建模前核对能力，结束前读取几何分析；不能强制保证模型遵循全部设计要求。

## 成功、失败与交互

复杂建模最多执行 24 轮、96 次工具调用。一轮返回多个调用时逐个执行，
连续三次工具/几何失败、用户取消或等待点击定位会停止当前批次的剩余调用。
被跳过的调用仍返回与 `tool_call_id` 配对的 `success=false, skipped=true`，保证下一轮消息完整。
停止后仅请求一次禁止工具调用的总结（`tool_choice=none`）；
即使服务继续返回工具也不会执行。收尾指令只作用于这一轮，不永久写入 system 历史。
达到上限或网络断开不会自动删除已经建出的模型；本机进度会报告工具调用数、
模型更新、涉及的特征 ID 和当前失败/阻塞数量，原有撤销历史保留。

“你直接来”“按建议来”等自由设计授权允许 AI 选用合理尺寸并开始建模。
精确零件缺少尺寸仍会询问。恢复中断任务先查询已有特征，避免重新创建相同对象。
当前支持自定义直线／圆弧轮廓、圆角／倒角、圆锥／圆台、扫掠和放样；渐扩杯身可用圆台、轮廓旋转或有序截面放样。系统提示不指定固定奖杯配方。
旋转出的工具实体可参与普通差集，但不能冒充已实现的独立旋转切除功能。

## 自由设计的第一步对照

自由设计先查询文档与能力，在首次修改前简述风格、主体轮廓、尺寸比例、连接方式与限制，
再直接执行。说明与工具调用可出现在同一响应中，Controller 在执行前显示说明，
不会新增确认步骤或重复写入对话历史。明确的单参数修改不要求完整设计方案。
完成前要求查询并对照方案核对参数和重建状态；这属于提示约束，尚无强制设计验收门槛。
工具没有图片反馈，不允许把数值分析描述为已看过成品或验证审美。

第一步对照只改变设计提示和说明展示，当时24个工具、模型服务配置、几何能力及显示设置保持不变。
本次增加空间规则与几何分析后属于新的实验阶段，应另记录版本，不与第一步混算。
为了初步比较，保存旧奖杯与截图，重启 build-feedback 的 Release 程序以使用新提示并清除旧对话，
在空白文档输入同一请求（例如“奖杯，你直接来”），记录方案、工具调用及新文档，
以相同视角比较主体轮廓、比例、连接和无关细节。重启前保存当前未保存模型。
单次更好或更差不能证明提示的效果；正式对照需要固定模型、输入、起始文档和观察视角，
旧版/新版分别多次生成。不要把继承旧对话后的参数修改当成独立对照样本。

离线测试验证说明先于修改展示、执行继续、历史不重复；没有请求真实模型，
因此尚未验证模型遵循新提示的程度或审美效果。空间规则和几何分析已接入；
旋转结果XYZ平移已实现；通用结果变换、自定义轮廓与视觉反馈仍是后续独立步骤。

对象结果包含稳定 ID、参数、按输入顺序排列的 `dependencies`，以及
`rebuild_status`、`rebuild_message`。修改结果还返回 `document_status.failed_features`，
避免 AI 把上游参数写入成功误报为所有下游几何成功。

工具 Schema 与执行共用字段、类型、范围和枚举校验，未知工具/字段及非法参数被拒绝。
每次建模或一次 XYZ 修改沿用文档历史；撤销/重做没有可用历史时返回明确失败。
没有窗口上下文时，文件和视图等工具返回不可用。

文件工具省略路径时打开本机文件选择框，AI 不必猜测路径。
另存为/STEP 导出补默认后缀后再检查覆盖；新建/打开沿用未保存修改提示。
取消返回 `success=false, cancelled=true`，不能继续声称文件或模型已经改变。
失败打开先保留当前文档，STEP 导入只追加对象，STEP 导出仅收集最终有效几何。
保存更新原生文档路径和保存点，导出不增加历史。

点击定位仅启动已有拾取模式，返回 `pending_user_input=true`；用户点击才提交 XYZ，
Esc 或取消工具结束操作。模型变化和历史恢复会清理过期拾取。
基础体与草图支持点击定位；Imported/Revolve 通过 `set_position` 或属性面板设置平移。

## 可直接输入的示例

- “创建一个半径 2、圆心 (10,0,0) 的圆形草图，绕世界 Y 轴旋转 360 度。”
- “把 Revolve001 的旋转角度改成 180 度。”
- “用 RectangleSketch001 对 Box001 做高度 20 的反向拉伸切除。”
- “用 Box001 减去 Cylinder001。”
- “撤销上一步，然后重做。”
- “另存为 ForgeCAD 文档。”
- “导出 STEP，让我选择保存位置。”
- “选中 CircleSketch001 并开始点击定位。”

## 验证范围

2026-09-29：`build-feedback` 的 `forgecad`、`forgecad_tests`、`forgecad_ui_tests`
三个Release目标构建成功，扩展建模后205／205 CTest用例通过，包括新工具、嵌套Schema、选边修复、文件往返和组合奖杯；不调用DeepSeek服务。
新增 Agent 循环测试注入离线传输，实际执行 13 轮奖杯建模并检查单实体、总高、开口杯腔和文件往返；
另覆盖轮数/调用预算、失败计数、用户取消、点击等待、禁止收尾工具执行、网络失败兜底与继续请求。

测试直接构造工具 JSON，经过与网络响应相同的执行入口，不访问 DeepSeek、无 API 费用。
覆盖登记完整性、几何体积、依赖、非法输入、下游失败、历史、文件往返与取消、
级联删除确认、属性联动和点击定位。UI 测试使用 offscreen 模式；
新增分析覆盖世界坐标尺寸/体积/质心、两轴偏移圆环、上游排除、空/混合/失败结果、
点接触两个实体及重叠后的修复、共享拓扑计数，以及自动反馈刷新/最终报告/下一轮不残留旧事实。
真实模型的自然语言工具选择、服务连接及相机渲染仍需运行主程序验收。
运行环境需要 `DEEPSEEK_API_KEY`，可选 `DEEPSEEK_MODEL` 和 `DEEPSEEK_BASE_URL`。
