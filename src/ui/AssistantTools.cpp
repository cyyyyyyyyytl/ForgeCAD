#include "ui/mainwindow.h"
#include "ui/Viewport3D.h"
#include "domain/Feature.h"
#include "infrastructure/NativeDocumentIO.h"
#include "infrastructure/StepIO.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace {
QJsonObject cancelled()
{
    return {{"success",false},{"cancelled",true},{"error","用户取消操作"}};
}
QJsonObject failed(const QString& error)
{
    return {{"success",false},{"error",error}};
}
}

// UI 工具只处理窗口上下文，建模仍由 ToolRegistry -> ModelDocument 完成。
// 每条分支返回实际结果，不能通过触发一个 void QAction 就假定操作成功。
QJsonObject MainWindow::executeAssistantUiTool(const QString& name, const QJsonObject& arguments)
{
    if (name=="get_document_status")
        return {{"success",true},{"path",documentPath_},{"selected_feature_id",QString::fromStdString(selectedFeatureId_)},{"point_placement_active",viewport_->isPickingPoint()}};
    if (name=="select_feature" || name=="begin_point_placement") {
        const auto id=arguments.value("feature_id").toString().trimmed().toStdString();
        const auto* feature=document_.findFeature(id);
        if (!feature) return failed(QStringLiteral("找不到 Feature: ")+QString::fromStdString(id));
        if (name=="begin_point_placement" && feature->type()!="Box" && feature->type()!="Cylinder" &&
            feature->type()!="Sphere" && feature->type()!="RectangleSketch" && feature->type()!="CircleSketch")
            return failed(QStringLiteral("此特征不支持点击定位，可对支持位置参数的对象使用 set_position"));
        if (name=="begin_point_placement") for (const auto& p:feature->parameters())
            if (p.name()=="plane" && p.asDouble()!=0) return failed(QStringLiteral("XZ／YZ 草图请使用 set_position 定位"));
        viewport_->cancelPointPick();
        selectFeature(id);
        rebuildFeatureTree();
        if (name=="begin_point_placement") {
            on_actionPlaceFeature_triggered();
            return {{"success",true},{"feature_id",QString::fromStdString(id)},{"pending_user_input",true},{"message","等待用户点击位置；当前 XYZ 尚未修改，Esc 取消"}};
        }
        return {{"success",true},{"selected_feature_id",QString::fromStdString(id)}};
    }
    if (name=="cancel_point_placement") {
        viewport_->cancelPointPick(); positioningFeatureId_.clear();
        return {{"success",true},{"point_placement_active",false}};
    }
    if (name=="control_view") {
        const auto operation=arguments.value("operation").toString();
        if (!viewport_->controlView(operation,arguments.value(operation=="zoom" ? "factor" : "dx").toDouble(),arguments.value("dy").toDouble()))
            return failed(QStringLiteral("三维视图尚未初始化，请先显示主窗口"));
        return {{"success",true},{"operation",operation}};
    }
    if (name=="save_document" || name=="save_document_as") {
        QJsonObject outcome;
        saveDocument(name=="save_document_as",arguments.value("path").toString().trimmed(),&outcome);
        return outcome;
    }
    if (name=="new_document") {
        if (!confirmDiscardChanges()) return cancelled();
        forge::application::ModelDocument empty;
        document_.swap(empty);
        documentPath_.clear(); selectedFeatureId_.clear();
        setWindowTitle(QStringLiteral("未命名[*] — ForgeCAD"));
        return {{"success",true},{"model_changed",true},{"feature_count",0}};
    }
    auto path=arguments.value("path").toString().trimmed();
    if (name=="open_document" || name=="import_step") {
        if (path.isEmpty()) path=QFileDialog::getOpenFileName(this,
            name=="open_document" ? QStringLiteral("打开 ForgeCAD 文档") : QStringLiteral("导入 STEP"),{},
            name=="open_document" ? QStringLiteral("ForgeCAD 文档 (*.forgecad)") : QStringLiteral("STEP 文件 (*.step *.stp)"));
        if (path.isEmpty()) return cancelled();
        if (name=="open_document") {
            if (!confirmDiscardChanges()) return cancelled();
            auto prepared=forge::infrastructure::NativeDocumentIO::prepare(path);
            document_.swap(*prepared);
            documentPath_=path; selectedFeatureId_.clear();
            setWindowTitle(QFileInfo(path).fileName()+QStringLiteral("[*] — ForgeCAD"));
            return {{"success",true},{"model_changed",true},{"path",path}};
        }
        const auto imported=forge::infrastructure::StepIO::importShape(path);
        if (!imported.success) return failed(imported.error);
        auto& feature=document_.createImportedFeature(imported.shape,QFileInfo(path).fileName().toStdString());
        return {{"success",true},{"model_changed",true},{"feature_id",QString::fromStdString(feature.id())},{"rebuild_status","ready"},{"path",path},{"diagnostics",imported.diagnostics}};
    }
    if (name=="export_step") {
        const auto geometry=document_.shapeForExport();
        if (geometry.status!=forge::core::RebuildStatus::Ready) return failed(QString::fromStdString(geometry.message));
        if (path.isEmpty()) path=QFileDialog::getSaveFileName(this,QStringLiteral("导出 STEP"),QStringLiteral("model.step"),
            QStringLiteral("STEP 文件 (*.step *.stp)"),nullptr,QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty()) return cancelled();
        if (QFileInfo(path).suffix().isEmpty()) path+=QStringLiteral(".step");
        if (QFileInfo::exists(path) && QMessageBox::question(this,QStringLiteral("覆盖文件"),QStringLiteral("目标文件已存在，是否覆盖？"),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return cancelled();
        const auto result=forge::infrastructure::StepIO::exportShape(geometry.shape,path);
        if (!result.success) return failed(result.error);
        return {{"success",true},{"path",path},{"message",QString::fromStdString(geometry.message)}};
    }
    return failed(QStringLiteral("未登记的窗口工具: ")+name);
}
