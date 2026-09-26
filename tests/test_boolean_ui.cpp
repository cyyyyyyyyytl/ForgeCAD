#include <gtest/gtest.h>

#include "infrastructure/StepIO.h"
#include "geometry/ShapeFactory.h"
#include "ui/mainwindow.h"
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
#include <QPushButton>
#include <QTimer>
#include <QTreeView>

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
