#include <gtest/gtest.h>

#include "infrastructure/StepIO.h"
#include "geometry/ShapeFactory.h"
#include "geometry/ShapeAnalyzer.h"
#include "ui/mainwindow.h"
#include "ui/Viewport3D.h"
#include <QKeyEvent>
#include "ui/RebuildFeedback.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QTemporaryDir>
#include <STEPControl_Reader.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <sstream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include <QMenu>
#include <numbers>
#include <QPushButton>
#include <QTimer>
#include <QTableWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QTreeView>
#include <QCloseEvent>
#include "infrastructure/NativeDocumentIO.h"
#include "assistant/AgentController.h"

namespace {

void ensureApplication()
{
    // 只测试菜单和对话框；主窗口不 show()，避免依赖显卡和 OCCT 视口。
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    static int argc = 1;
    static char name[] = "forgecad_ui_test";
    static char* argv[] = {name, nullptr};
    static QApplication application(argc, argv);
}

bool hasFeature(MainWindow& window, const QString& id)
{
    const auto* model = window.findChild<QTreeView*>("modelTree")->model();
    if (model->rowCount() == 0) return false;
    return !model->match(model->index(0, 0), Qt::UserRole, id, 1,
                         Qt::MatchExactly | Qt::MatchRecursive).isEmpty();
}

void createBox(MainWindow& window, double size)
{
    QTimer::singleShot(0, [size]() {
        // activeModalWidget 避免误取主窗口中预先存在的 AI 对话框。
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing Box dialog"; return; }
        for (const char* name : {"length", "width", "height"}) dialog->findChild<QDoubleSpinBox*>(name)->setValue(size);
        dialog->accept();
    });
    window.findChild<QAction*>("actionNewBox")->trigger();
}

void performBoolean(MainWindow& window, const char* actionName, bool accept)
{
    auto* action = window.findChild<QAction*>(actionName);
    ASSERT_NE(action, nullptr);
    QTimer::singleShot(0, [&window, accept]() {
        auto* dialog = window.findChild<QDialog*>("booleanFeatureDialog");
        if (!dialog) { ADD_FAILURE() << "Missing Boolean dialog"; return; }
        auto* base = dialog->findChild<QComboBox*>("booleanBaseCombo");
        auto* tool = dialog->findChild<QComboBox*>("booleanToolCombo");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!base || !tool || !buttons) {
            ADD_FAILURE() << "Missing Boolean controls";
            dialog->reject();
            return;
        }
        base->setCurrentIndex(base->findData(QStringLiteral("Box001")));
        tool->setCurrentIndex(tool->findData(QStringLiteral("Box001")));
        EXPECT_FALSE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        tool->setCurrentIndex(tool->findData(QStringLiteral("Box002")));
        EXPECT_TRUE(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        if (accept) buttons->button(QDialogButtonBox::Ok)->click();
        else buttons->button(QDialogButtonBox::Cancel)->click();
    });
    action->trigger();
}

} // namespace

TEST(BooleanUiTest, MenusCreateResultsAndSupportCancellationAndHistory)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10.0);
    createBox(window, 5.0);
    ASSERT_TRUE(hasFeature(window, "Box001"));
    ASSERT_TRUE(hasFeature(window, "Box002"));

    performBoolean(window, "actionBooleanDifference", true);
    ASSERT_TRUE(hasFeature(window, "Cut001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Cut001"));
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_TRUE(hasFeature(window, "Cut001"));

    performBoolean(window, "actionBooleanUnion", false);
    EXPECT_FALSE(hasFeature(window, "Union001"));
    performBoolean(window, "actionBooleanUnion", true);
    EXPECT_TRUE(hasFeature(window, "Union001"));
    performBoolean(window, "actionBooleanIntersection", true);
    EXPECT_TRUE(hasFeature(window, "Intersection001"));
}

TEST(BooleanUiTest, EmptyDocumentShowsInputRequirement)
{
    ensureApplication();
    MainWindow window;
    bool warned = false;
    QTimer::singleShot(0, [&warned]() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (message) {
            warned = message->text().contains(QStringLiteral("至少两个"));
            message->accept();
        } else {
            ADD_FAILURE() << "Missing input requirement message";
        }
    });
    window.findChild<QAction*>("actionBooleanDifference")->trigger();
    EXPECT_TRUE(warned);
    EXPECT_FALSE(hasFeature(window, "Cut001"));
}


TEST(PositionUiTest, SignedMillimeterCoordinatesAndUndoRedo)
{
    ensureApplication();
    MainWindow window;
    QTimer::singleShot(0, []() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog, nullptr);
        auto* x = dialog->findChild<QDoubleSpinBox*>("x");
        ASSERT_NE(x, nullptr);
        EXPECT_DOUBLE_EQ(x->value(), 0.0);
        EXPECT_LT(x->minimum(), 0.0);
        EXPECT_EQ(x->decimals(), 3);
        EXPECT_EQ(x->suffix(), QStringLiteral(" mm"));
        x->setValue(-12.125);
        dialog->findChild<QDoubleSpinBox*>("y")->setValue(4.5);
        dialog->findChild<QDoubleSpinBox*>("z")->setValue(-2.0);
        dialog->accept();
    });
    window.findChild<QAction*>("actionNewBox")->trigger();
    auto currentX = [&window]() {
        // 历史刷新会重建控件，只从当前布局读取，避免误取等待 deleteLater 的旧控件。
        auto* panel = window.findChild<QWidget*>("paramPanelContainer");
        auto* form = qobject_cast<QFormLayout*>(panel->layout());
        return qobject_cast<QDoubleSpinBox*>(form->itemAt(3, QFormLayout::FieldRole)->widget());
    };
    ASSERT_NE(currentX(), nullptr);
    EXPECT_DOUBLE_EQ(currentX()->value(), -12.125);
    currentX()->setValue(7.375);
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_DOUBLE_EQ(currentX()->value(), -12.125);
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_DOUBLE_EQ(currentX()->value(), 7.375);
}


TEST(RebuildFeedbackUiTest, EmptyResultLabelTreeAndHistoryRecover)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    createBox(window, 5);
    auto* tree = window.findChild<QTreeView*>("modelTree");
    auto indexOf = [tree](const char* id) {
        return tree->model()->match(tree->model()->index(0, 0), Qt::UserRole, QString::fromUtf8(id), 1,
            Qt::MatchExactly | Qt::MatchRecursive).first();
    };
    // Box002 当前选中；将它移到不相交的位置，然后建立交集。
    auto* panel = window.findChild<QWidget*>("paramPanelContainer");
    auto* form = qobject_cast<QFormLayout*>(panel->layout());
    qobject_cast<QDoubleSpinBox*>(form->itemAt(3, QFormLayout::FieldRole)->widget())->setValue(20);
    performBoolean(window, "actionBooleanIntersection", true);
    EXPECT_EQ(indexOf("Intersection001").data(Qt::UserRole + 1).toInt(), static_cast<int>(forge::core::RebuildStatus::Empty));
    EXPECT_TRUE(indexOf("Intersection001").data(Qt::ToolTipRole).toString().contains("结果为空"));
    auto currentStatus = [panel]() {
        auto* form = qobject_cast<QFormLayout*>(panel->layout());
        return qobject_cast<QLabel*>(form->itemAt(form->rowCount() - 1, QFormLayout::FieldRole)->widget())->text();
    };
    EXPECT_TRUE(currentStatus().contains("空结果"));
    EXPECT_TRUE(window.statusBar()->currentMessage().contains("空结果 1"));
    // 通过实际树点击路径选中工具，修改后树立即更新，不必重新创建布尔特征。
    const auto index = indexOf("Box002");
    EXPECT_TRUE(QMetaObject::invokeMethod(tree, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, index)));
    form = qobject_cast<QFormLayout*>(panel->layout());
    qobject_cast<QDoubleSpinBox*>(form->itemAt(3, QFormLayout::FieldRole)->widget())->setValue(2);
    EXPECT_EQ(indexOf("Intersection001").data(Qt::UserRole + 1).toInt(), static_cast<int>(forge::core::RebuildStatus::Ready));
    EXPECT_FALSE(indexOf("Intersection001").data().toString().contains("空结果"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_EQ(indexOf("Intersection001").data(Qt::UserRole + 1).toInt(), static_cast<int>(forge::core::RebuildStatus::Empty));
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_EQ(indexOf("Intersection001").data(Qt::UserRole + 1).toInt(), static_cast<int>(forge::core::RebuildStatus::Ready));
}


TEST(RebuildFeedbackUiTest, FailureColorReasonAndRecoveryClearOldDecoration)
{
    QStandardItem item;
    item.setData("Cut001", Qt::UserRole);
    forge::core::ShapeResult result{{}, forge::core::RebuildStatus::Failed, "布尔计算失败：测试诊断"};
    forge::ui::decorateRebuildItem(item, "Cut001", result);
    EXPECT_EQ(item.foreground().color(), QColor(190, 45, 45));
    EXPECT_TRUE(item.text().contains("重建失败"));
    EXPECT_TRUE(item.toolTip().contains("测试诊断"));
    EXPECT_EQ(item.data(Qt::UserRole).toString(), "Cut001");
    result.status = forge::core::RebuildStatus::Blocked;
    result.message = "输入特征失败：Box002";
    forge::ui::decorateRebuildItem(item, "Cut001", result);
    EXPECT_TRUE(item.text().contains("上游失败"));
    EXPECT_TRUE(item.toolTip().contains("Box002"));
    result.status = forge::core::RebuildStatus::Ready;
    result.message = "重建成功";
    forge::ui::decorateRebuildItem(item, "Cut001", result);
    EXPECT_EQ(item.text(), "Cut001");
    EXPECT_EQ(item.foreground().style(), Qt::NoBrush);
    EXPECT_FALSE(item.toolTip().contains("Box002"));
}


TEST(StepExportUiTest, MenuExportsFinalCutAndAddsDefaultSuffix)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    createBox(window, 5);
    performBoolean(window, "actionBooleanDifference", true);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    auto* action = window.findChild<QAction*>("actionExportStep");
    ASSERT_NE(action, nullptr);
    const QString path = temp.filePath(QStringLiteral("最终零件.step"));
    QTimer::singleShot(0, [&temp]() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing STEP file dialog"; return; }
        EXPECT_EQ(dialog->objectName(), "stepExportDialog");
        EXPECT_EQ(dialog->acceptMode(), QFileDialog::AcceptSave);
        dialog->setDirectory(temp.path());
        dialog->selectFile(QStringLiteral("最终零件")); // 默认添加 .step。
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    action->trigger();
    ASSERT_TRUE(QFile::exists(path));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    std::istringstream input(std::string(bytes.constData(), bytes.size()));
    STEPControl_Reader reader;
    ASSERT_EQ(reader.ReadStream("test.step", input), IFSelect_RetDone);
    reader.SetSystemLengthUnit(1.0);
    ASSERT_GT(reader.TransferRoots(), 0);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(reader.OneShape(), properties);
    EXPECT_NEAR(properties.Mass(), 875, 1e-5);
    EXPECT_TRUE(window.statusBar()->currentMessage().contains("STEP 导出成功"));
    EXPECT_TRUE(hasFeature(window, "Cut001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Cut001")); // 导出不产生历史记录。
}

TEST(StepExportUiTest, CancellationWritesNothingAndPreservesHistory)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString path = temp.filePath("cancel.step");
    QTimer::singleShot(0, [&temp]() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing STEP file dialog"; return; }
        dialog->setDirectory(temp.path());
        dialog->selectFile("cancel.step");
        dialog->reject();
    });
    window.findChild<QAction*>("actionExportStep")->trigger();
    EXPECT_FALSE(QFile::exists(path));
    EXPECT_TRUE(hasFeature(window, "Box001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Box001"));
}

TEST(StepExportUiTest, EmptyDocumentExplainsWhyExportIsUnavailable)
{
    ensureApplication();
    MainWindow window;
    bool warned = false;
    QTimer::singleShot(0, [&warned]() {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE() << "Missing empty-document warning"; return; }
        warned = message->text().contains("没有可导出的几何");
        message->accept();
    });
    window.findChild<QAction*>("actionExportStep")->trigger();
    EXPECT_TRUE(warned);
    EXPECT_FALSE(hasFeature(window, "Box001"));
}


TEST(StepImportUiTest, MenuImportsSelectsFeatureAndRestoresHistory)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("导入零件.step"));
    ASSERT_TRUE(forge::infrastructure::StepIO::exportShape(forge::geometry::ShapeFactory::makeBox(5, 5, 5), path).success);
    QTimer::singleShot(0, [&temp, &path]() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing import dialog"; return; }
        EXPECT_EQ(dialog->objectName(), "stepImportDialog");
        EXPECT_EQ(dialog->acceptMode(), QFileDialog::AcceptOpen);
        dialog->setDirectory(temp.path());
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    auto* importAction = window.findChild<QAction*>("actionImportStep");
    ASSERT_NE(importAction, nullptr);
    importAction->trigger();
    EXPECT_TRUE(hasFeature(window, "Box001"));
    EXPECT_TRUE(hasFeature(window, "Imported001"));
    EXPECT_TRUE(window.statusBar()->currentMessage().contains("STEP 导入成功"));
    ASSERT_TRUE(QFile::remove(path));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Imported001"));
    EXPECT_TRUE(hasFeature(window, "Box001"));
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_TRUE(hasFeature(window, "Imported001"));
    // 历史恢复保留当前仍存在的选择（Box001），应先像用户一样选中导入节点。
    auto* tree = window.findChild<QTreeView*>("modelTree");
    const auto matches = tree->model()->match(tree->model()->index(0, 0), Qt::UserRole,
        QStringLiteral("Imported001"), 1, Qt::MatchExactly | Qt::MatchRecursive);
    ASSERT_EQ(matches.size(), 1);
    ASSERT_TRUE(QMetaObject::invokeMethod(tree, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, matches.first())));
    auto* panel = window.findChild<QWidget*>("paramPanelContainer");
    auto* form = qobject_cast<QFormLayout*>(panel->layout());
    auto* x = qobject_cast<QDoubleSpinBox*>(form->itemAt(0, QFormLayout::FieldRole)->widget());
    ASSERT_NE(x, nullptr);
    x->setValue(-2.125);
    window.findChild<QAction*>("actionUndo")->trigger();
    form = qobject_cast<QFormLayout*>(panel->layout());
    EXPECT_DOUBLE_EQ(qobject_cast<QDoubleSpinBox*>(form->itemAt(0, QFormLayout::FieldRole)->widget())->value(), 0.0);
}

TEST(StepImportUiTest, CancelLeavesExistingModelUntouched)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    QTimer::singleShot(0, []() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing import dialog"; return; }
        dialog->reject();
    });
    auto* importAction = window.findChild<QAction*>("actionImportStep");
    ASSERT_NE(importAction, nullptr);
    importAction->trigger();
    EXPECT_FALSE(hasFeature(window, "Imported001"));
    EXPECT_TRUE(hasFeature(window, "Box001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Box001"));
}

TEST(StepImportUiTest, CorruptFileShowsErrorWithoutChangingModel)
{
    ensureApplication();
    MainWindow window;
    createBox(window, 10);
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = temp.filePath("broken.step");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("invalid STEP");
    file.close();
    bool warned = false;
    QTimer::singleShot(0, [&temp, &path, &warned]() {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE() << "Missing import dialog"; return; }
        dialog->setDirectory(temp.path());
        dialog->selectFile(path);
        QTimer::singleShot(0, [&warned]() {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!message) { ADD_FAILURE() << "Missing import error"; return; }
            warned = !message->text().isEmpty();
            message->accept();
        });
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
    });
    auto* importAction = window.findChild<QAction*>("actionImportStep");
    ASSERT_NE(importAction, nullptr);
    importAction->trigger();
    EXPECT_TRUE(warned);
    EXPECT_TRUE(hasFeature(window, "Box001"));
    EXPECT_FALSE(hasFeature(window, "Imported001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window, "Box001"));
}

TEST(NativeDocumentUiTest, SaveOpenAndHistoryRestoreDocumentCleanState)
{
    ensureApplication();
    MainWindow window;
    createBox(window,10);
    EXPECT_TRUE(window.isWindowModified());
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("part.forgecad");
    QTimer::singleShot(0,[&]() {
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing save dialog"; return; }
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionSaveDocument_triggered",Qt::DirectConnection));
    EXPECT_TRUE(QFile::exists(path)); EXPECT_FALSE(window.isWindowModified());
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_TRUE(window.isWindowModified());
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_FALSE(window.isWindowModified());
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewDocument_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"Box001"));
    QTimer::singleShot(0,[&]() {
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing open dialog"; return; }
        dialog->selectFile(path);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionOpenDocument_triggered",Qt::DirectConnection));
    EXPECT_TRUE(hasFeature(window,"Box001")); EXPECT_FALSE(window.isWindowModified());
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_TRUE(hasFeature(window,"Box001"));
}

TEST(NativeDocumentUiTest, CancelCloseKeepsDirtyDocument)
{
    ensureApplication();
    MainWindow window;
    createBox(window,10);
    QTimer::singleShot(0,[]() {
        auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE()<<"Missing save prompt"; return; }
        message->button(QMessageBox::Cancel)->click();
    });
    QCloseEvent event;
    QApplication::sendEvent(&window,&event);
    EXPECT_FALSE(event.isAccepted());
    EXPECT_TRUE(hasFeature(window,"Box001")); EXPECT_TRUE(window.isWindowModified());
}

TEST(NativeDocumentUiTest, CancelSaveAndFailedOpenKeepCurrentModel)
{
    ensureApplication(); MainWindow window; createBox(window,10);
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing save dialog"; return; }
        dialog->reject();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionSaveDocument_triggered",Qt::DirectConnection));
    EXPECT_TRUE(window.isWindowModified()); EXPECT_TRUE(hasFeature(window,"Box001"));
    QTemporaryDir dir; const auto path=dir.filePath("bad.forgecad");
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("bad archive"); file.close();
    QTimer::singleShot(0,[&]() {
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing open dialog"; return; }
        dialog->selectFile(path);
        QTimer::singleShot(0,[]() {
            auto* prompt=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!prompt) { ADD_FAILURE()<<"Missing dirty prompt"; return; }
            QTimer::singleShot(0,[]() {
                auto* error=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!error) { ADD_FAILURE()<<"Missing open error"; return; }
                error->accept();
            });
            prompt->button(QMessageBox::Discard)->click();
        });
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionOpenDocument_triggered",Qt::DirectConnection));
    EXPECT_TRUE(window.isWindowModified()); EXPECT_TRUE(hasFeature(window,"Box001"));
}

TEST(RectangleSketchUiTest, MenuCreatesSketchAndSupportsCancellationAndHistory)
{
    ensureApplication(); MainWindow window;
    auto* action=window.findChild<QAction*>("actionNewRectangleSketch");
    ASSERT_NE(action,nullptr);
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing rectangle dialog"; return; }
        EXPECT_EQ(dialog->windowTitle(),QStringLiteral("新建矩形草图"));
        EXPECT_EQ(dialog->findChildren<QDoubleSpinBox*>().size(),5);
        dialog->findChild<QDoubleSpinBox*>("length")->setValue(10);
        dialog->findChild<QDoubleSpinBox*>("width")->setValue(20);
        dialog->findChild<QDoubleSpinBox*>("x")->setValue(-3);
        dialog->findChild<QDoubleSpinBox*>("y")->setValue(4);
        dialog->accept();
    });
    action->trigger();
    EXPECT_TRUE(hasFeature(window,"RectangleSketch001"));
    auto* tree=window.findChild<QTreeView*>("modelTree");
    EXPECT_EQ(tree->model()->index(0,0).data().toString(),QStringLiteral("矩形草图"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window,"RectangleSketch001"));
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_TRUE(hasFeature(window,"RectangleSketch001"));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing rectangle dialog"; return; }
        dialog->reject();
    });
    action->trigger();
    EXPECT_FALSE(hasFeature(window,"RectangleSketch002"));
}

TEST(ExtrudeUiTest, SlotCreatesExtrusionFromSketchAndSupportsCancelAndHistory)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing sketch dialog"; return; }
        dialog->findChild<QDoubleSpinBox*>("length")->setValue(10);
        dialog->findChild<QDoubleSpinBox*>("width")->setValue(20);
        dialog->accept();
    });
    window.findChild<QAction*>("actionNewRectangleSketch")->trigger();
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing extrude dialog"; return; }
        EXPECT_EQ(dialog->objectName(),"extrudeDialog");
        auto* combo=dialog->findChild<QComboBox*>("extrudeSketchCombo");
        ASSERT_NE(combo,nullptr); EXPECT_EQ(combo->count(),1);
        EXPECT_EQ(combo->currentData().toString(),"RectangleSketch001");
        dialog->findChild<QDoubleSpinBox*>("extrudeHeight")->setValue(5);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrude_triggered",Qt::DirectConnection));
    EXPECT_TRUE(hasFeature(window,"Extrude001"));
    window.findChild<QAction*>("actionUndo")->trigger(); EXPECT_FALSE(hasFeature(window,"Extrude001"));
    EXPECT_TRUE(hasFeature(window,"RectangleSketch001"));
    window.findChild<QAction*>("actionRedo")->trigger(); EXPECT_TRUE(hasFeature(window,"Extrude001"));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing extrude dialog"; return; }
        dialog->reject();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrude_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"Extrude002"));
}

TEST(ExtrudeUiTest, EmptyDocumentExplainsSketchRequirement)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE()<<"Missing sketch requirement"; return; }
        EXPECT_TRUE(message->text().contains(QStringLiteral("矩形草图"))); message->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrude_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"Extrude001"));
}

TEST(CircleSketchUiTest, CreationSlotAndExtrusionSupportHistoryAndCancellation)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing circle dialog"; return; }
        EXPECT_EQ(dialog->windowTitle(),QStringLiteral("新建圆形草图"));
        EXPECT_EQ(dialog->findChildren<QDoubleSpinBox*>().size(),4);
        dialog->findChild<QDoubleSpinBox*>("radius")->setValue(10);
        dialog->findChild<QDoubleSpinBox*>("x")->setValue(-3);
        dialog->findChild<QDoubleSpinBox*>("y")->setValue(4);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewCircleSketch_triggered",Qt::DirectConnection));
    ASSERT_TRUE(hasFeature(window,"CircleSketch001"));
    auto* tree=window.findChild<QTreeView*>("modelTree");
    EXPECT_EQ(tree->model()->index(0,0).data().toString(),QStringLiteral("圆形草图"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window,"CircleSketch001"));
    window.findChild<QAction*>("actionRedo")->trigger();
    ASSERT_TRUE(hasFeature(window,"CircleSketch001"));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing circle dialog"; return; }
        dialog->reject();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewCircleSketch_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"CircleSketch002"));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing extrusion dialog"; return; }
        auto* combo=dialog->findChild<QComboBox*>("extrudeSketchCombo");
        ASSERT_NE(combo,nullptr);
        EXPECT_EQ(combo->count(),1);
        EXPECT_EQ(combo->currentData().toString(),"CircleSketch001");
        dialog->findChild<QDoubleSpinBox*>("extrudeHeight")->setValue(5);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrude_triggered",Qt::DirectConnection));
    EXPECT_TRUE(hasFeature(window,"Extrude001"));
    window.findChild<QAction*>("actionUndo")->trigger();
    EXPECT_FALSE(hasFeature(window,"Extrude001"));
    window.findChild<QAction*>("actionRedo")->trigger();
    EXPECT_TRUE(hasFeature(window,"Extrude001"));
}

TEST(ExtrudeUiTest, DirectionChoicesAndPropertyEditingFollowUndo)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing sketch dialog"; return; }
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewCircleSketch_triggered",Qt::DirectConnection));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing extrusion dialog"; return; }
        auto* direction=dialog->findChild<QComboBox*>("extrudeDirection");
        ASSERT_NE(direction,nullptr);
        EXPECT_EQ(direction->count(),3);
        EXPECT_EQ(direction->currentData().toInt(),0);
        EXPECT_TRUE(direction->itemText(1).contains(QStringLiteral("反向")));
        EXPECT_TRUE(direction->itemText(2).contains(QStringLiteral("对称")));
        direction->setCurrentIndex(2);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrude_triggered",Qt::DirectConnection));
    auto* direction=window.findChild<QComboBox*>("direction");
    ASSERT_NE(direction,nullptr);
    EXPECT_EQ(direction->currentData().toInt(),2);
    direction->setCurrentIndex(1);
    EXPECT_EQ(direction->currentData().toInt(),1);
    window.findChild<QAction*>("actionUndo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    direction=window.findChild<QComboBox*>("direction");
    ASSERT_NE(direction,nullptr); EXPECT_EQ(direction->currentData().toInt(),2);
    window.findChild<QAction*>("actionRedo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    direction=window.findChild<QComboBox*>("direction");
    ASSERT_NE(direction,nullptr); EXPECT_EQ(direction->currentData().toInt(),1);
}

TEST(ExtrudeCutUiTest, SlotFiltersInputsCreatesHoleAndSupportsCancelAndHistory)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing box dialog"; return; }
        dialog->findChild<QDoubleSpinBox*>("length")->setValue(20);
        dialog->findChild<QDoubleSpinBox*>("width")->setValue(20);
        dialog->findChild<QDoubleSpinBox*>("height")->setValue(10);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewBox_triggered",Qt::DirectConnection));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing sketch dialog"; return; }
        dialog->findChild<QDoubleSpinBox*>("radius")->setValue(2);
        dialog->findChild<QDoubleSpinBox*>("x")->setValue(10);
        dialog->findChild<QDoubleSpinBox*>("y")->setValue(10);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewCircleSketch_triggered",Qt::DirectConnection));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing cut dialog"; return; }
        EXPECT_EQ(dialog->windowTitle(),QStringLiteral("拉伸切除"));
        auto* base=dialog->findChild<QComboBox*>("extrudeCutBase");
        auto* sketch=dialog->findChild<QComboBox*>("extrudeCutSketch");
        ASSERT_NE(base,nullptr); ASSERT_NE(sketch,nullptr);
        EXPECT_EQ(base->count(),1); EXPECT_EQ(sketch->count(),1);
        EXPECT_EQ(base->currentData().toString(),"Box001");
        EXPECT_EQ(sketch->currentData().toString(),"CircleSketch001");
        dialog->findChild<QDoubleSpinBox*>("extrudeCutHeight")->setValue(10);
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrudeCut_triggered",Qt::DirectConnection));
    EXPECT_TRUE(hasFeature(window,"ExtrudeCut001"));
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    ASSERT_NE(window.findChild<QComboBox*>("direction"),nullptr);
    window.findChild<QAction*>("actionUndo")->trigger(); EXPECT_FALSE(hasFeature(window,"ExtrudeCut001"));
    window.findChild<QAction*>("actionRedo")->trigger(); EXPECT_TRUE(hasFeature(window,"ExtrudeCut001"));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing cut dialog"; return; }
        EXPECT_EQ(dialog->findChild<QComboBox*>("extrudeCutBase")->currentData().toString(),"ExtrudeCut001");
        dialog->reject();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrudeCut_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"ExtrudeCut002"));
}

TEST(ExtrudeCutUiTest, MissingInputsShowRequirement)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE()<<"Missing cut requirement"; return; }
        EXPECT_TRUE(message->text().contains(QStringLiteral("实体")));
        message->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionExtrudeCut_triggered",Qt::DirectConnection));
    EXPECT_FALSE(hasFeature(window,"ExtrudeCut001"));
}

TEST(PointPlacementUiTest, ButtonCommitsXYZAsOneUndoAndEscapeCancels)
{
    ensureApplication(); MainWindow window;
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing sketch dialog"; return; }
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window,"on_actionNewCircleSketch_triggered",Qt::DirectConnection));
    auto* viewport=window.findChild<forge::ui::Viewport3D*>();
    ASSERT_NE(viewport,nullptr);
    auto* button=window.findChild<QPushButton*>("pickPositionButton");
    ASSERT_NE(button,nullptr);
    button->click(); EXPECT_TRUE(viewport->isPickingPoint());
    // 不初始化 OpenGL，模拟视口拾取结果来验证文档、属性和历史接线。
    emit viewport->pointPicked(10,12,20);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("x")->value(),10);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("y")->value(),12);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),20);
    window.findChild<QAction*>("actionUndo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("x")->value(),0);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("y")->value(),0);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),0);
    window.findChild<QAction*>("actionRedo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),20);
    window.findChild<QPushButton*>("pickPositionButton")->click();
    EXPECT_TRUE(viewport->isPickingPoint());
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QCoreApplication::sendEvent(viewport,&escape);
    EXPECT_FALSE(viewport->isPickingPoint());
    emit viewport->pointPicked(1,2,3);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),20);
}

TEST(RevolveUiTest, MenuCreatesRingWithDegreePropertiesAndSupportsHistoryAndCancel)
{
    ensureApplication(); MainWindow window;
    auto* action=window.findChild<QAction*>("actionRevolve");
    ASSERT_NE(action,nullptr);
    EXPECT_EQ(action->text(),QStringLiteral("旋转"));
    auto* menu=window.findChild<QMenu*>("menu_3");
    ASSERT_NE(menu,nullptr);
    EXPECT_EQ(menu->title(),QStringLiteral("建模"));
    EXPECT_TRUE(menu->actions().contains(action));
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing circle dialog"; return; }
        dialog->findChild<QDoubleSpinBox*>("radius")->setValue(2);
        dialog->findChild<QDoubleSpinBox*>("x")->setValue(10); dialog->accept();
    });
    window.findChild<QAction*>("actionNewCircleSketch")->trigger();
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing revolve dialog"; return; }
        EXPECT_EQ(dialog->objectName(),"revolveDialog");
        EXPECT_EQ(dialog->findChild<QComboBox*>("revolveSketch")->currentData().toString(),"CircleSketch001");
        EXPECT_EQ(dialog->findChild<QComboBox*>("revolveAxis")->currentData().toInt(),1);
        auto* angle=dialog->findChild<QDoubleSpinBox*>("revolveAngle");
        ASSERT_NE(angle,nullptr); EXPECT_EQ(angle->suffix(),QStringLiteral(" °"));
        EXPECT_DOUBLE_EQ(angle->value(),360); dialog->accept();
    });
    // 从 Designer QAction 触发，验证 Qt 自动连接，而不是绕过菜单直接调用槽。
    action->trigger();
    ASSERT_TRUE(hasFeature(window,"Revolve001"));
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    auto verifySavedVolume=[&](const char* name,double expectedVolume) {
        const auto path=dir.filePath(QString::fromUtf8(name));
        QTimer::singleShot(0,[&path]() {
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) { ADD_FAILURE()<<"Missing native save dialog"; return; }
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
        });
        window.findChild<QAction*>("actionSaveDocumentAs")->trigger();
        ASSERT_TRUE(QFile::exists(path));
        auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
        EXPECT_EQ(restored->visibleFeatureIds(),(std::vector<std::string>{"Revolve001"}));
        const auto report=restored->rebuildReport();
        const auto& result=report.at("Revolve001");
        ASSERT_EQ(result.status,forge::core::RebuildStatus::Ready);
        EXPECT_EQ(result.shape.ShapeType(),TopAbs_SOLID);
        GProp_GProps properties;
        BRepGProp::VolumeProperties(result.shape,properties);
        EXPECT_NEAR(properties.Mass(),expectedVolume,1e-6);
    };
    verifySavedVolume("ring.forgecad",80*std::numbers::pi*std::numbers::pi);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* angle=window.findChild<QDoubleSpinBox*>("angle"); ASSERT_NE(angle,nullptr);
    EXPECT_EQ(angle->suffix(),QStringLiteral(" °")); EXPECT_DOUBLE_EQ(angle->value(),360);
    ASSERT_NE(window.findChild<QComboBox*>("axis"),nullptr);
    angle->setValue(180);
    verifySavedVolume("half-ring.forgecad",40*std::numbers::pi*std::numbers::pi);
    window.findChild<QAction*>("actionUndo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("angle")->value(),360);
    window.findChild<QAction*>("actionRedo")->trigger();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("angle")->value(),180);
    QTimer::singleShot(0,[]() {
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) { ADD_FAILURE()<<"Missing revolve dialog"; return; } dialog->reject();
    });
    action->trigger();
    EXPECT_FALSE(hasFeature(window,"Revolve002"));
}

TEST(RevolveUiTest, AiTranslationUpdatesPropertiesSupportsEditingAndNativeSave)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("set_position",{{"feature_id","Revolve001"},{"x",35},{"y",0},{"z",120}}).value("success").toBool());
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* x=window.findChild<QDoubleSpinBox*>("x");
    auto* z=window.findChild<QDoubleSpinBox*>("z");
    ASSERT_NE(x,nullptr); ASSERT_NE(z,nullptr);
    EXPECT_DOUBLE_EQ(x->value(),35); EXPECT_DOUBLE_EQ(z->value(),120);
    EXPECT_DOUBLE_EQ(z->minimum(),-1000000); EXPECT_DOUBLE_EQ(z->maximum(),1000000);
    auto* info=window.findChild<QLabel*>("revolvePositionInfo"); ASSERT_NE(info,nullptr);
    EXPECT_TRUE(info->text().contains(QStringLiteral("旋转完成后")));
    z->setValue(121);
    auto measured=agent->executeTool("analyze_geometry",{{"feature_id","Revolve001"}}).value("geometry").toObject();
    EXPECT_NEAR(measured.value("volume_centroid_mm").toObject().value("z").toDouble(),121,1e-6);
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),120);
    ASSERT_TRUE(agent->executeTool("redo",{}).value("success").toBool());
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    EXPECT_DOUBLE_EQ(window.findChild<QDoubleSpinBox*>("z")->value(),121);
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid()); const auto path=dir.filePath("translated.forgecad");
    ASSERT_TRUE(agent->executeTool("save_document_as",{{"path",path}}).value("success").toBool());
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    const auto analysis=forge::geometry::ShapeAnalyzer::analyze(restored->rebuildReport().at("Revolve001").shape);
    ASSERT_TRUE(analysis.volumeCentroid);
    EXPECT_NEAR((*analysis.volumeCentroid)[0],35,1e-6); EXPECT_NEAR((*analysis.volumeCentroid)[2],121,1e-6);
    EXPECT_FALSE(window.isWindowModified());
}

TEST(AssistantUiTest, RingWorkflowSavesOpensExportsImportsAndUsesHistory)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360}}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"Revolve001")); EXPECT_TRUE(window.isWindowModified());
    ASSERT_TRUE(agent->executeTool("set_parameter",{{"feature_id","Revolve001"},{"parameter_name","angle"},{"value",180}}).value("success").toBool());
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto nativePath=dir.filePath(QStringLiteral("半圈"));
    const auto saved=agent->executeTool("save_document_as",{{"path",nativePath}});
    ASSERT_TRUE(saved.value("success").toBool());
    EXPECT_EQ(saved.value("path").toString(),nativePath+".forgecad");
    EXPECT_FALSE(window.isWindowModified());
    const auto stepPath=dir.filePath("ring-export");
    ASSERT_TRUE(agent->executeTool("export_step",{{"path",stepPath}}).value("success").toBool());
    const auto imported=forge::infrastructure::StepIO::importShape(stepPath+".step");
    ASSERT_TRUE(imported.success);
    GProp_GProps properties; BRepGProp::VolumeProperties(imported.shape,properties);
    EXPECT_NEAR(properties.Mass(),40*std::numbers::pi*std::numbers::pi,1e-5);
    // 导出不占历史；撤销直接恢复角度，重做回到原生保存点。
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_TRUE(window.isWindowModified());
    ASSERT_TRUE(agent->executeTool("redo",{}).value("success").toBool());
    EXPECT_FALSE(window.isWindowModified());
    ASSERT_TRUE(agent->executeTool("new_document",{}).value("success").toBool());
    EXPECT_FALSE(hasFeature(window,"Revolve001"));
    ASSERT_TRUE(agent->executeTool("open_document",{{"path",nativePath+".forgecad"}}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"Revolve001"));
    ASSERT_TRUE(agent->executeTool("select_feature",{{"feature_id","Revolve001"}}).value("success").toBool());
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* angle=window.findChild<QDoubleSpinBox*>("angle"); ASSERT_NE(angle,nullptr);
    EXPECT_DOUBLE_EQ(angle->value(),180);
    const auto status=agent->executeTool("get_document_status",{});
    EXPECT_FALSE(status.value("can_undo").toBool());
    EXPECT_EQ(status.value("ui").toObject().value("path").toString(),nativePath+".forgecad");
    ASSERT_TRUE(agent->executeTool("import_step",{{"path",stepPath+".step"}}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"Imported001"));
    ASSERT_TRUE(agent->executeTool("select_feature",{{"feature_id","CircleSketch001"}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_FALSE(hasFeature(window,"Imported001"));
    EXPECT_EQ(agent->executeTool("get_document_status",{}).value("ui").toObject().value("selected_feature_id").toString(),"CircleSketch001");
    ASSERT_TRUE(agent->executeTool("redo",{}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"Imported001"));
    ASSERT_TRUE(agent->executeTool("save_document",{}).value("success").toBool());
    EXPECT_FALSE(window.isWindowModified());
}

TEST(AssistantUiTest, CancellationPreservesDirtyModelAndExistingFiles)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",2}}}}).value("success").toBool());
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid()); const auto path=dir.filePath("sphere.forgecad");
    ASSERT_TRUE(agent->executeTool("save_document_as",{{"path",path}}).value("success").toBool());
    QFile original(path); ASSERT_TRUE(original.open(QIODevice::ReadOnly)); const auto originalBytes=original.readAll(); original.close();
    ASSERT_TRUE(agent->executeTool("set_parameter",{{"feature_id","Sphere001"},{"parameter_name","radius"},{"value",3}}).value("success").toBool());
    const auto decline=[]() {
        auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE()<<"Missing confirmation"; return; }
        message->button(QMessageBox::No)->click();
    };
    QTimer::singleShot(0,decline);
    EXPECT_TRUE(agent->executeTool("save_document_as",{{"path",path}}).value("cancelled").toBool());
    ASSERT_TRUE(original.open(QIODevice::ReadOnly)); EXPECT_EQ(original.readAll(),originalBytes); original.close();
    for (const char* tool : {"new_document","open_document"}) {
        QTimer::singleShot(0,[]() {
            auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!message) { ADD_FAILURE()<<"Missing dirty document prompt"; return; }
            message->button(QMessageBox::Cancel)->click();
        });
        const auto result=agent->executeTool(tool,std::string(tool)=="open_document" ? QJsonObject{{"path",path}} : QJsonObject{});
        EXPECT_TRUE(result.value("cancelled").toBool());
        EXPECT_TRUE(hasFeature(window,"Sphere001")); EXPECT_TRUE(window.isWindowModified());
    }
    const auto stepPath=dir.filePath("keep.step");
    QFile step(stepPath); ASSERT_TRUE(step.open(QIODevice::WriteOnly)); step.write("keep me"); step.close();
    QTimer::singleShot(0,decline);
    EXPECT_TRUE(agent->executeTool("export_step",{{"path",stepPath}}).value("cancelled").toBool());
    ASSERT_TRUE(step.open(QIODevice::ReadOnly)); EXPECT_EQ(step.readAll(),"keep me"); step.close();
    EXPECT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_FALSE(window.isWindowModified());
}

TEST(AssistantUiTest, FailedFilesAndCancelledDialogsDoNotReplaceModelOrHistory)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","Sphere"},{"parameters",QJsonObject{{"radius",2}}}}).value("success").toBool());
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("bad.forgecad");
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("invalid archive"); file.close();
    QTimer::singleShot(0,[]() {
        auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) { ADD_FAILURE()<<"Missing dirty document prompt"; return; }
        message->button(QMessageBox::Discard)->click();
    });
    EXPECT_FALSE(agent->executeTool("open_document",{{"path",path}}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"Sphere001")); EXPECT_TRUE(window.isWindowModified());
    EXPECT_FALSE(agent->executeTool("save_document_as",{{"path",dir.filePath("missing/fail.forgecad")}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("import_step",{{"path",path}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("export_step",{{"path",dir.filePath("missing/fail.step")}}).value("success").toBool());
    for (const char* name : {"save_document_as","open_document","import_step","export_step"}) {
        QTimer::singleShot(0,[]() {
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) { ADD_FAILURE()<<"Missing file chooser"; return; }
            dialog->reject();
        });
        EXPECT_TRUE(agent->executeTool(name,{}).value("cancelled").toBool());
    }
    EXPECT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_FALSE(hasFeature(window,"Sphere001"));
    EXPECT_FALSE(agent->executeTool("export_step",{{"path",dir.filePath("empty.step")}}).value("success").toBool());
    EXPECT_FALSE(QFile::exists(dir.filePath("empty.step")));
}

TEST(AssistantUiTest, DeleteRequiresConfirmationAndCascadeCanBeUndone)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360}}).value("success").toBool());
    for (const bool confirm : {false,true}) {
        QTimer::singleShot(0,[confirm]() {
            auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!message) { ADD_FAILURE()<<"Missing cascade confirmation"; return; }
            EXPECT_TRUE(message->text().contains("Revolve001"));
            message->button(confirm ? QMessageBox::Yes : QMessageBox::No)->click();
        });
        const auto result=agent->executeTool("delete_feature",{{"feature_id","CircleSketch001"}});
        EXPECT_EQ(result.value("success").toBool(),confirm);
        EXPECT_EQ(hasFeature(window,"Revolve001"),!confirm);
        if (!confirm) EXPECT_TRUE(result.value("cancelled").toBool());
        else EXPECT_EQ(result.value("deleted_feature_ids").toArray(),(QJsonArray{"Revolve001","CircleSketch001"}));
    }
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_TRUE(hasFeature(window,"CircleSketch001")); EXPECT_TRUE(hasFeature(window,"Revolve001"));
    EXPECT_FALSE(agent->executeTool("delete_feature",{{"feature_id","missing"}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("delete_feature",{{"feature_id","CircleSketch001"},{"extra",1}}).value("success").toBool());
}

TEST(AssistantUiTest, SelectionAndClickPlacementReturnPendingUntilUserCommits)
{
    ensureApplication(); MainWindow window;
    auto* agent=window.findChild<forge::assistant::AgentController*>(); ASSERT_NE(agent,nullptr);
    auto* viewport=window.findChild<forge::ui::Viewport3D*>(); ASSERT_NE(viewport,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}).value("success").toBool());
    auto result=agent->executeTool("begin_point_placement",{{"feature_id","CircleSketch001"}});
    EXPECT_TRUE(result.value("pending_user_input").toBool()); EXPECT_TRUE(viewport->isPickingPoint());
    EXPECT_FALSE(result.value("model_changed").toBool());
    EXPECT_TRUE(agent->executeTool("cancel_point_placement",{}).value("success").toBool());
    EXPECT_FALSE(viewport->isPickingPoint());
    EXPECT_DOUBLE_EQ(agent->executeTool("get_feature",{{"feature_id","CircleSketch001"}}).value("parameters").toObject().value("x").toDouble(),10);
    ASSERT_TRUE(agent->executeTool("begin_point_placement",{{"feature_id","CircleSketch001"}}).value("success").toBool());
    emit viewport->pointPicked(20,3,4);
    EXPECT_FALSE(agent->executeTool("get_document_status",{}).value("ui").toObject().value("point_placement_active").toBool());
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    const auto parameters=agent->executeTool("get_feature",{{"feature_id","CircleSketch001"}}).value("parameters").toObject();
    EXPECT_DOUBLE_EQ(parameters.value("x").toDouble(),10); EXPECT_DOUBLE_EQ(parameters.value("y").toDouble(),0); EXPECT_DOUBLE_EQ(parameters.value("z").toDouble(),0);
    ASSERT_TRUE(agent->executeTool("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("begin_point_placement",{{"feature_id","Revolve001"}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("select_feature",{{"feature_id","missing"}}).value("success").toBool());
    EXPECT_FALSE(agent->executeTool("control_view",{{"operation","fit"}}).value("success").toBool()); // offscreen 主窗口没有初始化 OCCT 视图。
    ASSERT_TRUE(agent->executeTool("begin_point_placement",{{"feature_id","CircleSketch001"}}).value("success").toBool());
    ASSERT_TRUE(agent->executeTool("undo",{}).value("success").toBool());
    EXPECT_FALSE(viewport->isPickingPoint());
}

TEST(AdvancedUiTest, ActionNamesTextsAndMenuMembershipMatchDesignerContract) {
    ensureApplication();MainWindow window;
    struct Entry {const char* action;const char* menu;const char* text;};
    const Entry entries[]={
        {"actionNewCone","menu","圆锥／圆台"},
        {"actionNewProfileSketch","menu_4","自定义轮廓"},
        {"actionNewPath3D","menu_4","空间路径"},
        {"actionSweep","menu_3","扫掠"},
        {"actionLoft","menu_3","放样"},
        {"actionFillet","menu_3","圆角"},
        {"actionChamfer","menu_3","倒角"},
        {"actionTransform","menu_3","变换"}
    };
    for(const auto& e:entries) {
        auto* action=window.findChild<QAction*>(e.action);ASSERT_NE(action,nullptr)<<e.action;
        EXPECT_EQ(window.findChildren<QAction*>(e.action).size(),1);
        EXPECT_EQ(action->text(),QString::fromUtf8(e.text));EXPECT_TRUE(action->isEnabled());
        auto* menu=window.findChild<QMenu*>(e.menu);ASSERT_NE(menu,nullptr)<<e.menu;
        EXPECT_TRUE(menu->actions().contains(action))<<e.action;
    }
    EXPECT_EQ(window.findChild<QMenu*>("menu")->title(),QStringLiteral("新建"));
    EXPECT_EQ(window.findChild<QMenu*>("menu_4")->title(),QStringLiteral("草图"));
    EXPECT_EQ(window.findChild<QMenu*>("menu_3")->title(),QStringLiteral("建模"));
}
TEST(AdvancedUiTest, MenusCreateConeProfileZRevolveAndEditDefinition) {
    ensureApplication();MainWindow window;
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);dialog->findChild<QDoubleSpinBox*>("radius_bottom")->setValue(10);dialog->findChild<QDoubleSpinBox*>("radius_top")->setValue(20);dialog->accept();});
    ASSERT_NE(window.findChild<QAction*>("actionNewCone"),nullptr);window.findChild<QAction*>("actionNewCone")->trigger();ASSERT_TRUE(hasFeature(window,"Cone001"));
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* table=dialog->findChild<QTableWidget*>("definitionTable");ASSERT_NE(table,nullptr);EXPECT_EQ(table->rowCount(),4);dialog->findChild<QComboBox*>("plane")->setCurrentIndex(1);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionNewProfileSketch"),nullptr);window.findChild<QAction*>("actionNewProfileSketch")->trigger();ASSERT_TRUE(hasFeature(window,"ProfileSketch001"));
    auto* agent=window.findChild<forge::assistant::AgentController*>();ASSERT_NE(agent,nullptr);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    auto* edit=window.findChild<QPushButton*>("editDefinitionButton");ASSERT_NE(edit,nullptr);
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);dialog->findChild<QTableWidget*>()->item(2,1)->setText("50");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});edit->click();
    auto f=agent->executeTool("get_feature",{{"feature_id","ProfileSketch001"}});EXPECT_EQ(f["definition"].toObject()["vertices"].toArray()[2].toObject()["v"].toDouble(),50);
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* sketch=dialog->findChild<QComboBox*>("revolveSketch");sketch->setCurrentIndex(sketch->findData("ProfileSketch001"));auto* axis=dialog->findChild<QComboBox*>("revolveAxis");EXPECT_EQ(axis->count(),3);axis->setCurrentIndex(2);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionRevolve"),nullptr);window.findChild<QAction*>("actionRevolve")->trigger();ASSERT_TRUE(hasFeature(window,"Revolve001"));
    auto a=agent->executeTool("analyze_geometry",{{"feature_id","Revolve001"}});EXPECT_TRUE(a["geometry"].toObject()["single_solid"].toBool());
}
TEST(AdvancedUiTest, FilletEdgeSelectionTransformAndCancellation) {
    ensureApplication();MainWindow window;createBox(window,30);
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* list=dialog->findChild<QListWidget*>("edgeSelection");ASSERT_NE(list,nullptr);EXPECT_EQ(list->count(),12);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionFillet"),nullptr);window.findChild<QAction*>("actionFillet")->trigger();ASSERT_TRUE(hasFeature(window,"Fillet001"));
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);dialog->findChild<QDoubleSpinBox*>("rz")->setValue(90);dialog->findChild<QDoubleSpinBox*>("x")->setValue(100);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionTransform"),nullptr);window.findChild<QAction*>("actionTransform")->trigger();ASSERT_TRUE(hasFeature(window,"Transform001"));
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);auto* angle=window.findChild<QDoubleSpinBox*>("rz");ASSERT_NE(angle,nullptr);EXPECT_DOUBLE_EQ(angle->value(),90);EXPECT_EQ(angle->suffix(),QStringLiteral(" °"));
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);dialog->reject();});
    ASSERT_NE(window.findChild<QAction*>("actionChamfer"),nullptr);window.findChild<QAction*>("actionChamfer")->trigger();EXPECT_FALSE(hasFeature(window,"Chamfer001"));
}
TEST(AdvancedUiTest, PathSweepAndOrderedLoftDialogs) {
    ensureApplication();MainWindow window;auto* agent=window.findChild<forge::assistant::AgentController*>();ASSERT_NE(agent,nullptr);
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2}}}})["success"].toBool());
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* table=dialog->findChild<QTableWidget*>("definitionTable");ASSERT_NE(table,nullptr);EXPECT_EQ(table->columnCount(),6);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionNewPath3D"),nullptr);window.findChild<QAction*>("actionNewPath3D")->trigger();ASSERT_TRUE(hasFeature(window,"Path3D001"));
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* path=dialog->findChild<QComboBox*>("advancedPath");path->setCurrentIndex(path->findData("Path3D001"));EXPECT_TRUE(dialog->findChild<QCheckBox*>("align_profile")->isChecked());dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionSweep"),nullptr);window.findChild<QAction*>("actionSweep")->trigger();ASSERT_TRUE(hasFeature(window,"Sweep001"));
    ASSERT_TRUE(agent->executeTool("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",10},{"z",60}}}})["success"].toBool());
    QTimer::singleShot(0,[]{auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);auto* sections=dialog->findChild<QListWidget*>("loftSections");ASSERT_NE(sections,nullptr);sections->addItems({"CircleSketch001","CircleSketch002"});dialog->findChild<QCheckBox*>("ruled")->setChecked(true);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
    ASSERT_NE(window.findChild<QAction*>("actionLoft"),nullptr);window.findChild<QAction*>("actionLoft")->trigger();ASSERT_TRUE(hasFeature(window,"Loft001"));
    auto f=agent->executeTool("get_feature",{{"feature_id","Loft001"}});EXPECT_EQ(f["dependencies"].toArray(),(QJsonArray{"CircleSketch001","CircleSketch002"}));
}
