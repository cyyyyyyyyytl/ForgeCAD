#include "ui/mainwindow.h"
#include "domain/AdvancedFeature.h"
#include "geometry/AdvancedModeling.h"
#include "geometry/ShapeFactory.h"
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QCheckBox>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <cmath>
#include <algorithm>
#include <map>

void MainWindow::on_actionNewCone_triggered() {createFeatureFromDialog(QStringLiteral("Cone"));}
void MainWindow::on_actionNewProfileSketch_triggered() {advancedFeatureDialog("ProfileSketch");}
void MainWindow::on_actionNewPath3D_triggered() {advancedFeatureDialog("Path3D");}
void MainWindow::on_actionTransform_triggered() {advancedFeatureDialog("Transform");}
void MainWindow::on_actionFillet_triggered() {advancedFeatureDialog("Fillet");}
void MainWindow::on_actionChamfer_triggered() {advancedFeatureDialog("Chamfer");}
void MainWindow::on_actionSweep_triggered() {advancedFeatureDialog("Sweep");}
void MainWindow::on_actionLoft_triggered() {advancedFeatureDialog("Loft");}

void MainWindow::advancedFeatureDialog(const std::string& type,const std::string& editId) {
    QDialog dialog(this);dialog.setObjectName(QStringLiteral("advancedFeatureDialog"));
    const QString title=type=="ProfileSketch"?QStringLiteral("自定义轮廓"):type=="Path3D"?QStringLiteral("空间路径"):
        type=="Transform"?QStringLiteral("实体／轮廓变换"):type=="Fillet"?QStringLiteral("圆角"):type=="Chamfer"?QStringLiteral("倒角"):type=="Sweep"?QStringLiteral("扫掠"):QStringLiteral("放样");
    dialog.setWindowTitle((editId.empty()?QStringLiteral("新建"):QStringLiteral("编辑"))+title);
    auto* form=new QFormLayout(&dialog);
    const auto* existing=dynamic_cast<const forge::domain::AdvancedFeature*>(document_.findFeature(editId));
    if (!editId.empty() && (!existing || existing->type()!=type))return;
    forge::domain::NumericParameters values;
    if(existing)for(const auto& p:existing->parameters())values[p.name()]=p.asDouble();
    const auto report=document_.rebuildReport();
    auto* source=new QComboBox(&dialog);source->setObjectName(QStringLiteral("advancedSource"));
    auto* pathSource=new QComboBox(&dialog);pathSource->setObjectName(QStringLiteral("advancedPath"));
    const bool profile=type=="ProfileSketch",path=type=="Path3D",dress=type=="Fillet" || type=="Chamfer";
    for(const auto& f:document_.features()) {
        const auto& result=report.at(f->id());if(result.status!=forge::core::RebuildStatus::Ready)continue;
        const auto label=QString::fromStdString(f->id());const bool wire=result.shape.ShapeType()==TopAbs_WIRE;
        const bool closed=wire && TopoDS::Wire(result.shape).Closed();
        if((dress && forge::geometry::ShapeFactory::isSolidBody(result.shape)) || (type=="Transform" && (wire || forge::geometry::ShapeFactory::isSolidBody(result.shape))) ||
            ((type=="Sweep" || type=="Loft") && closed)) source->addItem(label,label);
        if(wire)pathSource->addItem(label,label);
    }
    const auto selected=source->findData(QString::fromStdString(selectedFeatureId_));if(selected>=0)source->setCurrentIndex(selected);
    if (!profile && !path)form->addRow(type=="Sweep" || type=="Loft"?QStringLiteral("截面"):QStringLiteral("源特征"),source);
    if(type=="Sweep")form->addRow(QStringLiteral("路径"),pathSource);
    QTableWidget* table=nullptr;
    if(profile || path) {
        table=new QTableWidget(&dialog);table->setObjectName(QStringLiteral("definitionTable"));
        table->setColumnCount(profile?3:6);
        table->setHorizontalHeaderLabels(profile?QStringList{QStringLiteral("U (mm)"),QStringLiteral("V (mm)"),QStringLiteral("圆弧 bulge")}:
            QStringList{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z"),QStringLiteral("圆弧经过 X"),QStringLiteral("经过 Y"),QStringLiteral("经过 Z")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->setMinimumSize(profile?QSize(440,210):QSize(670,210));
        const auto row=[&](const QStringList& cells){const int r=table->rowCount();table->insertRow(r);for(int c=0;c<cells.size();++c)table->setItem(r,c,new QTableWidgetItem(cells[c]));};
        if(existing && profile)for(const auto& v:existing->definition().vertices)row({QString::number(v.u,'g',15),QString::number(v.v,'g',15),QString::number(v.bulge,'g',15)});
        else if(existing)for(const auto& p:existing->definition().pathPoints) {
            QStringList cells;for(double v:p.point)cells.append(QString::number(v,'g',15));
            for(int i=0;i<3;++i)cells.append(p.through?QString::number((*p.through)[i],'g',15):QString{});row(cells);
        } else if(profile) {row({"10","0","0"});row({"20","0","0"});row({"20","40","0"});row({"10","40","0"});}
        else {row({"0","0","0","","",""});row({"0","0","50","","",""});}
        form->addRow(table);auto* controls=new QHBoxLayout;
        auto* add=new QPushButton(QStringLiteral("添加点"),&dialog);auto* remove=new QPushButton(QStringLiteral("删除点"),&dialog);
        controls->addWidget(add);controls->addWidget(remove);form->addRow(controls);
        connect(add,&QPushButton::clicked,&dialog,[table]{const int r=table->rowCount();table->insertRow(r);for(int c=0;c<table->columnCount();++c)table->setItem(r,c,new QTableWidgetItem(c<3?"0":""));});
        connect(remove,&QPushButton::clicked,&dialog,[table]{if(table->currentRow()>=0)table->removeRow(table->currentRow());});
        auto* info=new QLabel(profile?QStringLiteral("UV 为所选平面的局部坐标；不要重复首点。bulge=tan(圆弧角/4)，正为逆时针，0 为直线。XYZ 为整体世界平移。"):
            QStringLiteral("每点的经过坐标描述到下一点的三点圆弧；留空表示直线。开放路径末点须留空。单位毫米。"),&dialog);info->setWordWrap(true);form->addRow(info);
    }
    std::map<std::string,QDoubleSpinBox*> spins;std::map<std::string,QComboBox*> combos;std::map<std::string,QCheckBox*> checks;
    const auto* descriptor=forge::domain::FeatureRegistry::find(type);
    for(const auto& p:descriptor->parameters) {
        const double v=values.contains(p.name)?values.at(p.name):p.defaultValue;
        if(p.name=="plane" || p.name=="selection") {
            auto* combo=new QComboBox(&dialog);combo->setObjectName(QString::fromStdString(p.name));
            combo->addItems(p.name=="plane"?QStringList{"XY","XZ","YZ"}:QStringList{QStringLiteral("全部边"),QStringLiteral("上沿"),QStringLiteral("下沿"),QStringLiteral("竖直线边"),QStringLiteral("指定边")});
            combo->setCurrentIndex(static_cast<int>(v));combos[p.name]=combo;form->addRow(p.name=="plane"?QStringLiteral("草图平面"):QStringLiteral("选边规则"),combo);
        } else if(p.name=="closed" || p.name=="align_profile" || p.name=="ruled") {
            auto* check=new QCheckBox(&dialog);check->setObjectName(QString::fromStdString(p.name));check->setChecked(v!=0);checks[p.name]=check;
            form->addRow(p.name=="closed"?QStringLiteral("闭合"):p.name=="ruled"?QStringLiteral("直纹面"):QStringLiteral("自动将截面中心与法线对齐路径起点"),check);
        } else {
            auto* spin=new QDoubleSpinBox(&dialog);spin->setObjectName(QString::fromStdString(p.name));spin->setRange(p.minimum,p.maximum);spin->setDecimals(3);spin->setValue(v);
            const bool angle=p.name=="rx" || p.name=="ry" || p.name=="rz";spin->setSuffix(angle?QStringLiteral(" °"):QStringLiteral(" mm"));spins[p.name]=spin;
            const QString label=p.name=="radius"?QStringLiteral("圆角半径"):p.name=="distance"?QStringLiteral("倒角距离"):
                p.name.starts_with("pivot_")?QStringLiteral("旋转中心 ")+QString::fromStdString(p.name.substr(6)).toUpper():
                angle?QStringLiteral("绕世界 ")+QString::fromStdString(p.name.substr(1)).toUpper():QStringLiteral("平移 ")+QString::fromStdString(p.name).toUpper();
            form->addRow(label,spin);
        }
    }
    QListWidget* edges=nullptr;QListWidget* sections=nullptr;
    if(existing && dress) { const auto deps=document_.dependenciesOf(editId);if(!deps.empty())source->setCurrentIndex(source->findData(QString::fromStdString(deps[0])));source->setEnabled(false); }
    if(dress) {
        edges=new QListWidget(&dialog);edges->setObjectName(QStringLiteral("edgeSelection"));edges->setMinimumHeight(130);form->addRow(QStringLiteral("指定边（勾选）"),edges);
        const auto populate=[&,edges]{edges->clear();const auto id=source->currentData().toString().toStdString();if(!report.contains(id))return;
            try {for(const auto& e:forge::geometry::AdvancedModeling::edges(report.at(id).shape)) {
                auto* item=new QListWidgetItem(QStringLiteral("%1  %2  长度 %3 mm  Z=%4…%5").arg(QString::fromStdString(e.id),QString::fromStdString(e.curve)).arg(e.length,0,'f',3).arg(e.bounds[2],0,'f',3).arg(e.bounds[5],0,'f',3),edges);
                item->setData(Qt::UserRole,QString::fromStdString(e.id));item->setCheckState(Qt::Unchecked);
                if(existing && std::find(existing->definition().edgeIds.begin(),existing->definition().edgeIds.end(),e.id)!=existing->definition().edgeIds.end())item->setCheckState(Qt::Checked);
            }}catch(const std::exception& e){statusBar()->showMessage(QString::fromUtf8(e.what()));}};
        populate();connect(source,&QComboBox::currentIndexChanged,&dialog,populate);
        edges->setEnabled(combos["selection"]->currentIndex()==4);connect(combos["selection"],&QComboBox::currentIndexChanged,edges,[edges](int i){edges->setEnabled(i==4);});
    }
    if(type=="Loft") {
        sections=new QListWidget(&dialog);sections->setObjectName(QStringLiteral("loftSections"));form->addRow(QStringLiteral("截面顺序"),sections);
        auto* controls=new QHBoxLayout;
        for(const auto& label:{QStringLiteral("添加"),QStringLiteral("删除"),QStringLiteral("上移"),QStringLiteral("下移")}) {
            auto* button=new QPushButton(label,&dialog);controls->addWidget(button);
            connect(button,&QPushButton::clicked,&dialog,[source,sections,label]{
                if(label==QStringLiteral("添加")) {const auto id=source->currentData().toString();if(!id.isEmpty() && sections->findItems(id,Qt::MatchExactly).isEmpty())sections->addItem(id);return;}
                const int row=sections->currentRow();if(row<0)return;
                if(label==QStringLiteral("删除")){delete sections->takeItem(row);return;}
                const int to=row+(label==QStringLiteral("上移")?-1:1);if(to>=0 && to<sections->count()){auto* item=sections->takeItem(row);sections->insertItem(to,item);sections->setCurrentRow(to);}
            });
        }form->addRow(controls);
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        try {
            forge::domain::NumericParameters p;for(const auto& [key,spin]:spins)p[key]=spin->value();for(const auto& [key,c]:combos)p[key]=c->currentIndex();for(const auto& [key,c]:checks)p[key]=c->isChecked()?1:0;
            forge::domain::FeatureDefinition d;std::vector<std::string> inputs;
            if(table)for(int r=0;r<table->rowCount();++r) {
                const auto number=[&](int c){bool ok=false;const auto* item=table->item(r,c);const double v=item?item->text().toDouble(&ok):0;if(!ok || !std::isfinite(v))throw std::invalid_argument("点坐标须是有限数字");return v;};
                if(profile)d.vertices.push_back({number(0),number(1),number(2)});
                else {forge::domain::PathPoint point;point.point={number(0),number(1),number(2)};bool arc=false;for(int c=3;c<6;++c)arc|=table->item(r,c) && !table->item(r,c)->text().trimmed().isEmpty();if(arc)point.through=std::array<double,3>{number(3),number(4),number(5)};d.pathPoints.push_back(point);}
            }
            if(dress && p["selection"]==4)for(int i=0;i<edges->count();++i)if(edges->item(i)->checkState()==Qt::Checked)d.edgeIds.push_back(edges->item(i)->data(Qt::UserRole).toString().toStdString());
            if(sections)for(int i=0;i<sections->count();++i)inputs.push_back(sections->item(i)->text().toStdString());
            else if(!profile && !path) {inputs.push_back(source->currentData().toString().toStdString());if(type=="Sweep")inputs.push_back(pathSource->currentData().toString().toStdString());}
            std::string id=editId;
            if(existing)document_.setAdvancedDefinition(editId,p,std::move(d));else id=document_.createAdvancedFeature(type,p,std::move(d),inputs).id();
            selectedFeatureId_=id;rebuildFeatureTree();selectFeature(id);refreshViewport();dialog.accept();
        }catch(const std::exception& e){QMessageBox::warning(&dialog,QStringLiteral("无法生成"),QString::fromUtf8(e.what()));}
    });
    dialog.exec();
}
