# ForgeCAD OCCT/Qt API 实战手册

> **用途**：这是给"独立开发"用的 API 速查表。
> 不是理论教材，是"写代码时查一下怎么用"的手册。
> 每个 API 都带**项目里的真实用法** + **中文注释** + **可拓展示例**。
>
> 使用姿势：想写新功能 → 搜类名 → 抄示例 → 改参数 → 编译跑起来。
> 查不到精确签名 → 去 https://dev.opencascade.org/doc/refman/html/index.html 搜。

---

## 目录（按功能）

1. [创建基本几何体](#1-创建基本几何体)
2. [布尔运算](#2-布尔运算)
3. [变换](#3-变换)
4. [形状分析](#4-形状分析)
5. [STEP 导入导出](#5-step-导入导出)
6. [3D 显示（Viewport3D 体系）](#6-3d-显示)
7. [数学基础（gp_*）](#7-数学基础)
8. [拓扑遍历](#8-拓扑遍历)
9. [CMake 链接](#9-cmake-链接)

---

<details><summary>⚡ 快速开始：造一个盒子并显示（你的第一个 API）</summary>

```cpp
// ① 造盒子（数学形状）
#include <BRepPrimAPI_MakeBox.hxx>
TopoDS_Shape box = BRepPrimAPI_MakeBox(100, 50, 30).Shape();  // 长宽高

// ② 显示到 3D 视图
viewport->showShape(box);   // Viewport3D 提供的方法
```
</details>

---

## 1. 创建基本几何体

所有"造形状"的类都叫 `BRepPrimAPI_MakeXXX`，模式都一样：
**构造 → `.Shape()` 取出结果**。这是 OCCT 的"Builder API"风格。

### ✅ 你已在用：Box（长方体）
```cpp
#include <BRepPrimAPI_MakeBox.hxx>
TopoDS_Shape makeBox(double length, double width, double height) {
    if (length <= 0 || width <= 0 || height <= 0) return TopoDS_Shape();  // 防御：非法尺寸返回空
    try {
        BRepPrimAPI_MakeBox maker(length, width, height);
        return maker.Shape();     // 取出造好的形状
    } catch (const Standard_Failure& e) {
        return TopoDS_Shape();    // 出错不崩，返回空
    }
}
```
> 📌 项目位置：`src/geometry/ShapeFactory.cpp`
> 要点：`maker.Shape()` 返回 `TopoDS_Shape`；出错抛 `Standard_Failure` 要 try-catch。

### ⭐ 可拓展：圆柱 / 球 / 圆锥 / 圆环
```cpp
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>

TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(radius, height).Shape();      // 圆柱：半径, 高
TopoDS_Shape sph = BRepPrimAPI_MakeSphere(radius).Shape();                // 球：半径
TopoDS_Shape cone = BRepPrimAPI_MakeCone(r1, r2, height).Shape();         // 圆锥：下半径, 上半径, 高
TopoDS_Shape torus = BRepPrimAPI_MakeTorus(majorR, minorR).Shape();       // 圆环：主半径, 管半径
```

### ⭐ 可拓展：拉伸(Extrude) / 旋转(Revolve)
```cpp
#include <BRepPrimAPI_MakePrism.hxx>    // 拉伸
#include <BRepPrimAPI_MakeRevol.hxx>    // 旋转
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>

// 拉伸：把某个 Face/Wire 沿向量拉伸
TopoDS_Shape prism = BRepPrimAPI_MakePrism(face, gp_Vec(0,0,100)).Shape();

// 旋转：绕轴转角度
TopoDS_Shape revolve = BRepPrimAPI_MakeRevol(face, gp_Ax1(0,0,0, 0,0,1), 360.0).Shape();
```
> 注意：Extrude/Revolve 需要先有轮廓（Face/Wire），一般来自草图或已有形体。后面做 Sketch 才用得到。

---

## 2. 布尔运算

三个类：`BRepAlgoAPI_Fuse`(并) / `Cut`(差) / `Common`(交)。

```cpp
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>

// 并集（把两个形状合体）
TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    BRepAlgoAPI_Fuse op(a, b);          // 构造布尔运算对象
    op.SetFuzzyValue(1e-6);             // （可选）容差，越大越"宽容"，处理微小缝隙
    op.Build();                          // 执行
    if (!op.IsDone()) return TopoDS_Shape();  // 失败检查
    return op.Shape();
}
// 差集（a 减去 b）：BRepAlgoAPI_Cut(a, b)
// 交集（a 和 b 重叠部分）：BRepAlgoAPI_Common(a, b)
```
> 📌 对应需求：Boolean Kernel（Fuse/Cut/Common）
> 面试点：`IsDone()` 检查是否成功；`SetFuzzyValue` 容差；失败要能返回可解释错误。

---

## 3. 变换（平移/旋转/缩放）

```cpp
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>

TopoDS_Shape transform(const TopoDS_Shape& s) {
    gp_Trsf trsf;                        // 变换矩阵
    trsf.SetTranslation(gp_Vec(10, 0, 0));        // 平移 (x,y,z)
    // trsf.SetRotation(gp_Ax1(0,0,0, 0,0,1), 45.0);  // 绕 Z 轴转 45°
    // trsf.SetScale(gp_Pnt(0,0,0), 2.0);            // 绕原点缩放 2 倍
    BRepBuilderAPI_Transform op(s, trsf);          // 应用变换
    return op.Shape();
}
```
> 要点：`gp_Trsf` 是"变换矩阵"，可叠加多种变换；`BRepBuilderAPI_Transform` 应用它。

---

## 4. 形状分析（体积/面积/质心/包围盒）

```cpp
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

// 体积、表面积、质心
double volume(const TopoDS_Shape& s) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);    // 算体积属性
    return props.Mass();                        // 质量=体积（看作密度1）
}
// 表面积：BRepGProp::SurfaceProperties(s, props);  props.Mass()
// 质心：props.CentreOfMass() 返回 gp_Pnt

// 包围盒 AABB
Bnd_Box box;
BRepBndLib::Add(s, box);                     // 计算包围盒
double xmin, ymin, zmin, xmax, ymax, zmax;
box.Get(xmin, ymin, zmin, xmax, ymax, zmax); // 取出范围
```
> 📌 对应需求：几何分析模块（IShapeAnalyzer）

---

## 5. STEP 导入导出

```cpp
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>

// 导入 STEP → TopoDS_Shape
TopoDS_Shape importStep(const std::string& file) {
    STEPControl_Reader reader;
    if (reader.ReadFile(file.c_str()) != IFSelect_RetDone) return TopoDS_Shape();
    reader.TransferRoots();                    // 转换所有根形状
    return reader.OneShape();                  // 取出第一个形状
}

// 导出 TopoDS_Shape → STEP
bool exportStep(const TopoDS_Shape& s, const std::string& file) {
    STEPControl_Writer writer;
    writer.Transfer(s, STEPControl_AsIs);      // 传递形状
    return writer.Write(file.c_str()) == IFSelect_RetDone;  // 写文件
}
```
> 📌 对应需求：STEP 文件系统（Import/Export）

---

## 6. 3D 显示（Viewport3D 体系）

这是你项目里最复杂也最核心的部分。**架构**：

```
Viewport3D (QWidget, 你写的)
 ├─ viewer_ (V3d_Viewer)          // 场景容器
 ├─ view_   (V3d_View)            // 画布 + 相机
 ├─ context_ (AIS_InteractiveContext) // 管显示/选择物体
 └─ showShape(shape): 把形状放进场景
```

### ① 初始化（你已写好，在 Viewport3D.cpp）
```cpp
#include <OpenGl_GraphicDriver.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>

auto driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection());
viewer_ = new V3d_Viewer(driver);
viewer_->SetDefaultLights();         // 默认灯光
viewer_->SetLightOn();
view_ = new V3d_View(viewer_);
view_->SetBackgroundColor(Quantity_NOC_GRAY30);
context_ = new AIS_InteractiveContext(viewer_);
view_->SetWindow(new Aspect_NeutralWindow());   // 中立窗口（Qt 集成）
```

### ② 显示形状（showShape）
```cpp
auto aisShape = new AIS_Shape(shape);          // 包装成可显示物体
context_->Display(aisShape, AIS_Shaded, 0, false);  // 着色显示
view_->FitAll();      // 调整相机让模型完整可见
view_->Redraw();      // 重画
```

### ③ 交互（旋转/缩放）
```cpp
// 左键旋转
view_->StartRotation(ex, ey);   // 鼠标按下
view_->Rotation(ex, ey);        // 鼠标拖动
// 滚轮光标缩放
view_->StartZoomAtPoint(x, y);
view_->ZoomAtPoint(x, y, x+offset, y);   // 以光标为中心
```

> 📌 对应需求：交互式 3D View
> ⚠️ OCCT 8 的坑：`Window` 用 `Aspect_NeutralWindow`（别用 WNT_Window，Qt6 下视图会消失）；重绘用 QTimer 驱动（`view_->Redraw()`）。

---

## 7. 数学基础（gp_*）

这些是**值类型**（按值传递，不用句柄）。

```cpp
#include <gp_Pnt.hxx>   // 点 (x,y,z)
#include <gp_Vec.hxx>   // 向量
#include <gp_Dir.hxx>   // 方向（单位向量）
#include <gp_Ax1.hxx>   // 轴（点+方向）
#include <gp_Trsf.hxx>  // 变换矩阵

gp_Pnt p(1, 2, 3);          // 点
gp_Vec v(1, 0, 0);          // 向量
gp_Dir d(0, 0, 1);          // 方向
gp_Ax1 axis(gp_Pnt(0,0,0), gp_Dir(0,0,1));  // Z 轴

double dot = v.Dot(gp_Vec(0,1,0));       // 点积
gp_Vec cross = v.Crossed(gp_Vec(0,1,0)); // 叉积
double len = v.Magnitude();              // 模长
```
> 📌 对应需求：3D/数学（向量、点积叉积、坐标变换）

---

## 8. 拓扑遍历

遍历一个形状里的**面/边/顶点**（用 TopExp_Explorer）。

```cpp
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>

int countFaces(const TopoDS_Shape& s) {
    int n = 0;
    for (TopExp_Explorer exp(s, TopAbs_FACE); exp.More(); exp.Next()) {
        // TopoDS_Face face = TopoDS::Face(exp.Current());  // 拿到当前面
        n++;
    }
    return n;
}
// TopAbs_FACE / TopAbs_EDGE / TopAbs_VERTEX / TopAbs_SOLID / TopAbs_SHELL
```
> 📌 对应需求：几何分析（顶点/边/面/实体计数）

---

## 9. CMake 链接

你的 `CMakeLists.txt` 已经配置好，新加的 OCCT 类不用改 CMake（库全局链接了）。

```cmake
# src/CMakeLists.txt 里已列出（不用动）
set(OCCT_LIBRARIES
    TKPrim    # 基本体（BRepPrimAPI_*）
    TKBO      # 布尔（BRepAlgoAPI_Fuse/Cut/Common）
    TKBRep    # B-Rep 核心
    TKFillet  # 圆角倒角
    TKDESTEP  # STEP 导入导出
    TKV3d     # 3D 视图（AIS）
    TKService # 图形服务
    TKOpenGl  # OpenGL 驱动
    TKMath    # gp_* 数学
)
```
> 如果你加了新功能（如网格/曲面），可能要在 `src/CMakeLists.txt` 的 `OCCT_LIBRARIES` 里补对应 TK 库。

---

## 📌 常用规则速记

1. **造形状**：`BRepPrimAPI_MakeXXX(...).Shape()` → `TopoDS_Shape`
2. **布尔**：`BRepAlgoAPI_Fuse/Cut/Common(a,b).Shape()`
3. **分析**：`BRepGProp::VolumeProperties(s, props)` → `props.Mass()`
4. **变换**：`gp_Trsf` + `BRepBuilderAPI_Transform`
5. **显示**：`new AIS_Shape(shape)` + `context_->Display()`
6. **出错**：都抛 `Standard_Failure`，用 try-catch，返回空 `TopoDS_Shape()` 表示失败
7. **查类**：https://dev.opencascade.org/doc/refman/html/index.html

---

## 📁 参考（免费中文）

- 系列教程（第01-18章）：https://www.cnblogs.com/znlgis/
- DeepWiki AI 版文档：https://deepwiki.com/Open-Cascade-SAS/OCCT
- 官方 API 参考：https://dev.opencascade.org/doc/refman/html/index.html
