# 扩展建模与 Designer 接线

开发目录 `D:/ForgeCAD`，构建目录 `D:/ForgeCAD/build-feedback`。本轮不修改 Designer 文件，不提交或推送 Git。

## 可用能力

| 特征 | 定义与行为 |
|---|---|
| Cone | 圆锥／圆台：底／顶半径、高度、XYZ；底面圆心为基准，轴沿 +Z。一个半径可为0；相等时为圆柱。 |
| ProfileSketch | 直线和圆弧组成的开放／闭合轮廓，最多256个顶点；闭合轮廓至少3点，拒绝重复相邻点、零面积、自交。 |
| Path3D | 世界三维坐标的连接路径；每段直线或三点圆弧，可闭合，最多256个点。 |
| Transform | 保留源依赖，绕同一世界旋转中心依次 X、Y、Z 旋转，然后世界平移；支持实体和线框。 |
| Fillet / Chamfer | 圆角半径／等距倒角；规则选边或明确指定边。 |
| Sweep | 闭合平面截面沿路径扫掠。默认自动将截面面积中心移到路径起点，法线对齐起始切线。 |
| Loft | 按顺序连接2到32个不同闭合平面截面；默认平滑插值，可选择直纹面。 |

这些特征接入依赖图、重建状态、输入隐藏／失败展开、级联删除、撤销重做、原生文件和 STEP 导出。创建时先验证输入与几何，失败不增加特征或历史；修改后下游失败会显示原因，不隐瞒失败。

### 草图平面和旋转

矩形、圆形和自定义轮廓支持 `plane=0:XY / 1:XZ / 2:YZ`。局部 UV 对应 XY／XZ／YZ；XYZ 是世界平移。拉伸规范法线分别为 +Z／-Y／+X，反向取负，对称两侧各一半。点击定位仍只支持已有基础体和 XY 矩形／圆形草图，其他平面用 XYZ 参数。

旋转轴 `axis=0:X / 1:Y / 2:Z` 经过世界原点，必须平行草图平面：X接受XY/XZ，Y接受XY/YZ，Z接受XZ/YZ。旋转角度0.001–360度；结果XYZ保持生成后的世界平移语义。旧文档的草图缺少plane时补0，旋转缺少XYZ时补0。

自定义顶点的 `bulge=tan(有向圆弧角/4)`，正值为UV逆时针，作用于当前点到下一点的边，0为直线；不要重复首点。开放轮廓末点bulge须0。空间路径的 `through` 是当前点到下一点圆弧上的经过点，开放路径末点不接受through。

例如 XZ 闭合杯壁截面 `(U,V)=(10,0),(15,0),(30,50),(25,50)` 绕Z旋转360度，生成空心渐扩杯壁；180度生成半圈。圆弧轮廓可让杯身曲线变化，沿空间圆弧扫掠圆截面可做弯曲杯耳。

### 选边

`selection=0:all / 1:top / 2:bottom / 3:vertical / 4:explicit`。

上／下沿指整条边位于实体世界Z最高／最低处；竖直边指世界Z方向的直线边。规则会随上游尺寸重选。倾斜模型、局部开口等可使用指定边。

`list_edges` 返回不透明 `edge_id`、曲线类型、长度和世界包围盒。ID含拓扑索引与几何签名；上游改变导致ID不匹配时明确失败，不能按旧索引误选其他边。使用 `set_edge_selection` 更新已有圆角／倒角，或属性面板“编辑选边”。该方式是保守失效检查，尚非完整持久拓扑命名系统。

## AI 注册

总计37个工具。Cone通过现有 `create_feature`，草图平面通过数值参数plane；新增11个：

| 工具 | 关键参数 |
|---|---|
| create_profile / set_profile | vertices数组 `{u,v,bulge?}`；plane字符串XY/XZ/YZ、closed布尔、XYZ可选；编辑须feature_id，vertices整体替换。 |
| create_path / set_path | points数组 `{x,y,z,through?:{x,y,z}}`，closed布尔、XYZ可选；编辑须feature_id，points整体替换。 |
| create_transform | source_id、rx/ry/rz（度）、pivot_x/y/z、x/y/z（mm）。 |
| list_edges | feature_id。 |
| set_edge_selection | feature_id、selection字符串、explicit时edge_ids数组。 |
| create_fillet / create_chamfer | base_id、radius／distance、selection、explicit时edge_ids。 |
| create_sweep | profile_id、path_id、align_profile布尔默认true。 |
| create_loft | profile_ids有序数组、ruled布尔默认false。 |

布尔、文件、历史、查询、定位和视图工具继续可用。数值、布尔、数组和嵌套对象严格校验，未知字段拒绝。`get_feature`／`list_features`附结构化definition；`get_feature_types`返回20类特征的参数规则。所有修改工具通过文档API，返回重建状态，并触发现有宿主几何检查。Agent提示已去掉“无圆锥／任意轮廓／圆角／扫掠／姿态旋转”的过期限制。

## Designer 已接线的 QAction

2026-09-29检查：八个动作的objectName、菜单文字和所属菜单正确；圆锥／圆台在新建，轮廓／路径在草图，其余在建模。界面测试已改为触发真实QAction，验证自动槽连接、创建、编辑和取消；新增动作唯一性与菜单归属检查。三个Release目标构建成功，206／206测试通过，最新日志为`build-feedback/designer-actions-tests.log`。

| text | objectName |
|---|---|
| 圆锥／圆台 | actionNewCone |
| 自定义轮廓 | actionNewProfileSketch |
| 空间路径 | actionNewPath3D |
| 变换 | actionTransform |
| 圆角 | actionFillet |
| 倒角 | actionChamfer |
| 扫掠 | actionSweep |
| 放样 | actionLoft |

放进相应“新建／建模”菜单即可，Qt按 `on_<objectName>_triggered` 自动连接；无须手写connect。已有 `actionRevolve` 保留，代码对话框已加入世界Z轴。参数对话框、轮廓／路径编辑表格、选边列表和有序截面列表均由代码创建，不需要新Designer对话框。AI工具无需等待这些菜单即可使用。

## 验证范围与当前限制

扩展造型首次验证：三个Release目标构建成功，205／205 CTest通过，日志位于`build-feedback/advanced-full-tests.log`。组合奖杯测试将曲线空心杯身、放样立柱、两侧扫掠杯耳和圆台底座并为单实体，确认杯腔点在实体外、底部点在实体内。Designer接线后改用真实动作并新增菜单检查，206／206通过。

使用离线Tool JSON和offscreen Qt测试，不调用DeepSeek，不验证实际三维渲染或自然语言设计质量。几何测试检查体积、包围盒、实体数量、依赖、文件往返、失败和历史；数值正确仍不能证明造型美观。

圆角／倒角的过大尺寸、尖锐或自交扫掠、重合／顺序错误截面的放样可能被内核拒绝。暂不提供完整草图约束求解、Bezier／样条轮廓编辑、可变半径圆角、任意旋转轴、专门旋转切除或视觉反馈。变换后的任意平面线框可用于放样／扫掠；拉伸与旋转目前要求平行XY/XZ/YZ。路径或放样的合理性须由具体几何结果核对，不能以本功能承诺任意输入成功。

原生JSON新增可选 `definition`（vertices/path_points/edge_ids），旧文件保持可读，结构化定义同时进入历史快照。旧程序不会识别新增特征种类，含新特征的文件应使用本轮程序打开。
