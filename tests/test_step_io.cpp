#include <gtest/gtest.h>
#include "infrastructure/StepIO.h"
#include "application/ModelDocument.h"
#include "domain/BooleanFeature.h"
#include "domain/Feature.h"
#include "geometry/ShapeFactory.h"
#include <QFile>
#include <QTemporaryDir>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include "domain/ImportedFeature.h"
#include <STEPControl_Reader.hxx>
#include <DESTEP_Parameters.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <cmath>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using forge::infrastructure::StepIO;
using forge::geometry::ShapeFactory;
namespace {
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void seedFile(const QString& path)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("original model"), 14);
}
TopoDS_Shape readStep(const QString& path)
{
    // 通过 Qt 读中文路径，OCCT 从内存流解析，避免本地窄字符路径编码差异。
    const auto bytes = readBytes(path);
    std::istringstream input(std::string(bytes.constData(), bytes.size()));
    STEPControl_Reader reader;
    DESTEP_Parameters parameters;
    if (reader.ReadStream("roundtrip.step", parameters, input) != IFSelect_RetDone) {
        ADD_FAILURE() << "Unable to read exported STEP";
        return {};
    }
    reader.SetSystemLengthUnit(1.0); // 回读到毫米，验证长度单位而非依赖系统默认值。
    if (reader.TransferRoots() <= 0) {
        ADD_FAILURE() << "No transferred roots";
        return {};
    }
    return reader.OneShape();
}
void expectEquivalent(const TopoDS_Shape& before, const TopoDS_Shape& after)
{
    ASSERT_FALSE(after.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(after).IsValid());
    GProp_GProps first, second;
    BRepGProp::VolumeProperties(before, first);
    BRepGProp::VolumeProperties(after, second);
    EXPECT_NEAR(first.Mass(), second.Mass(), 1e-5);
    EXPECT_NEAR(first.CentreOfMass().X(), second.CentreOfMass().X(), 1e-5);
    EXPECT_NEAR(first.CentreOfMass().Y(), second.CentreOfMass().Y(), 1e-5);
    EXPECT_NEAR(first.CentreOfMass().Z(), second.CentreOfMass().Z(), 1e-5);
    int firstCount = 0, secondCount = 0;
    for (TopExp_Explorer it(before, TopAbs_SOLID); it.More(); it.Next()) ++firstCount;
    for (TopExp_Explorer it(after, TopAbs_SOLID); it.More(); it.Next()) ++secondCount;
    EXPECT_EQ(firstCount, secondCount);
}
}

TEST(StepIOTest, ChinesePathAndMillimeterBoxRoundTripReplaceExistingFile)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("偏移零件.step"));
    seedFile(path);
    const auto box = ShapeFactory::translate(ShapeFactory::makeBox(10, 20, 30), -15, 2.125, -7);
    const auto result = StepIO::exportShape(box, path);
    ASSERT_TRUE(result.success) << result.error.toStdString();
    EXPECT_TRUE(result.error.isEmpty());
    const auto bytes = readBytes(path);
    EXPECT_TRUE(bytes.startsWith("ISO-10303-21;"));
    EXPECT_TRUE(bytes.contains("SI_UNIT(.MILLI.,.METRE.)"));
    expectEquivalent(box, readStep(path));
}

TEST(StepIOTest, MultipleSolidsAndCurvedGeometryRoundTrip)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, ShapeFactory::translate(ShapeFactory::makeCylinder(2, 12), -5, 7, -1));
    builder.Add(compound, ShapeFactory::translate(ShapeFactory::makeSphere(3), 15, -4, 3));
    const auto path = temp.filePath("parts.stp");
    const auto result = StepIO::exportShape(compound, path);
    ASSERT_TRUE(result.success) << result.error.toStdString();
    expectEquivalent(compound, readStep(path));
}

TEST(StepIOTest, OffsetHoleRoundTripKeepsBooleanResult)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto box = ShapeFactory::makeBox(10, 10, 10);
    const auto drill = ShapeFactory::translate(ShapeFactory::makeCylinder(1, 12), 3, 4, -1);
    const auto cut = ShapeFactory::booleanDifference(box, drill);
    const auto path = temp.filePath("hole.step");
    const auto result = StepIO::exportShape(cut, path);
    ASSERT_TRUE(result.success) << result.error.toStdString();
    const auto restored = readStep(path);
    expectEquivalent(cut, restored);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(restored, properties);
    EXPECT_NEAR(properties.Mass(), 1000 - 10 * std::acos(-1.0), 1e-5);
}

TEST(StepIOTest, InvalidAndEmptyGeometryNeverOverwriteExistingFile)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = temp.filePath("keep.step");
    seedFile(path);
    BRep_Builder builder;
    TopoDS_Compound empty;
    builder.MakeCompound(empty);
    TopoDS_Edge broken;
    builder.MakeEdge(broken);
    for (const TopoDS_Shape& shape : {TopoDS_Shape{}, TopoDS_Shape(empty), TopoDS_Shape(broken)}) {
        const auto result = StepIO::exportShape(shape, path);
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.error.isEmpty());
        EXPECT_EQ(readBytes(path), "original model");
    }
    const auto box = ShapeFactory::makeBox(10, 10, 10);
    const auto result = StepIO::exportShape(box, QStringLiteral("   "));
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.isEmpty());
}

TEST(StepIOTest, InvalidDestinationReportsFailure)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto shape = ShapeFactory::makeBox(10, 10, 10);
    for (const auto& path : {temp.filePath("missing/model.step"), temp.path()}) {
        const auto result = StepIO::exportShape(shape, path);
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.error.isEmpty());
    }
    EXPECT_FALSE(QFile::exists(temp.filePath("missing/model.step")));
}

#ifdef _WIN32
TEST(StepIOTest, FailedAtomicCommitPreservesLockedDestination)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = temp.filePath("locked.step");
    seedFile(path);
    // 允许读写但禁止删除/重命名，确定性模拟最终替换失败。
    const auto widePath = path.toStdWString();
    const HANDLE handle = CreateFileW(widePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    struct HandleGuard { HANDLE value; ~HandleGuard() { CloseHandle(value); } } guard{handle};
    const auto result = StepIO::exportShape(ShapeFactory::makeBox(10, 10, 10), path);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_EQ(readBytes(path), "original model");
}
#endif


TEST(StepExportDocumentTest, OnlyFinalResultsExportAndHistoryRemainsIntact)
{
    forge::application::ModelDocument document;
    document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
    document.createFeature("Box", {{"length", 5}, {"width", 5}, {"height", 5}});
    document.createBooleanFeature(forge::domain::BooleanOperation::Difference, "Box001", "Box002");
    const auto result = document.shapeForExport();
    ASSERT_EQ(result.status, forge::core::RebuildStatus::Ready);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(result.shape, properties);
    EXPECT_NEAR(properties.Mass(), 875, 1e-5); // 不能混入原箱体和工具箱体。
    document.undo();
    EXPECT_EQ(document.findFeature("Cut001"), nullptr); // 导出没有增加 Undo 记录。
    EXPECT_TRUE(document.canRedo());
    EXPECT_EQ(document.shapeForExport().status, forge::core::RebuildStatus::Ready);
    EXPECT_TRUE(document.canRedo()); // 导出也不能清空 Redo。
}

TEST(StepExportDocumentTest, EmptyResultsSkippedButFailedBranchesRejectExport)
{
    forge::application::ModelDocument document;
    EXPECT_EQ(document.shapeForExport().status, forge::core::RebuildStatus::Empty);
    document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
    document.createFeature("Box", {{"length", 5}, {"width", 5}, {"height", 5}, {"x", 20}});
    document.createBooleanFeature(forge::domain::BooleanOperation::Intersection, "Box001", "Box002");
    EXPECT_EQ(document.shapeForExport().status, forge::core::RebuildStatus::Empty);
    document.createFeature("Sphere", {{"radius", 3}});
    const auto mixed = document.shapeForExport();
    EXPECT_EQ(mixed.status, forge::core::RebuildStatus::Ready);
    EXPECT_NE(mixed.message.find("1 个空结果"), std::string::npos);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(mixed.shape, properties);
    EXPECT_NEAR(properties.Mass(), 36 * std::acos(-1.0), 1e-5);
    document.findFeature("Box002")->setParameter("length", -1.0); // 注入上游故障。
    const auto failed = document.shapeForExport();
    EXPECT_EQ(failed.status, forge::core::RebuildStatus::Failed);
    EXPECT_TRUE(failed.shape.IsNull());
    EXPECT_NE(failed.message.find("Box002"), std::string::npos);
}


TEST(StepImportTest, ChineseFileRoundTripIncludesMultipleSolidsAndPositions)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    BRep_Builder builder;
    TopoDS_Compound source;
    builder.MakeCompound(source);
    builder.Add(source, ShapeFactory::translate(ShapeFactory::makeBox(10, 20, 30), -15, 2.125, -7));
    builder.Add(source, ShapeFactory::translate(ShapeFactory::makeSphere(3), 25, -4, 3));
    const auto path = temp.filePath(QStringLiteral("导入零件.stp"));
    ASSERT_TRUE(StepIO::exportShape(source, path).success);
    const auto imported = StepIO::importShape(path);
    ASSERT_TRUE(imported.success) << imported.error.toStdString();
    EXPECT_TRUE(imported.error.isEmpty());
    expectEquivalent(source, imported.shape);
}

TEST(StepImportTest, InchFileConvertsToMillimeterDocumentCoordinates)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto source = ShapeFactory::translate(ShapeFactory::makeBox(25.4, 50.8, 76.2), -25.4, 2.54, 0);
    STEPControl_Writer writer;
    writer.Model()->SetLocalLengthUnit(1.0);
    DESTEP_Parameters parameters;
    parameters.WriteUnit = UnitsMethods_LengthUnit_Inch;
    ASSERT_EQ(writer.Transfer(source, STEPControl_AsIs, parameters), IFSelect_RetDone);
    std::ostringstream stream;
    ASSERT_EQ(writer.WriteStream(stream), IFSelect_RetDone);
    const auto path = temp.filePath("inch.step");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const auto data = stream.str();
    ASSERT_EQ(file.write(data.data(), static_cast<qint64>(data.size())), static_cast<qint64>(data.size()));
    file.close();
    const auto imported = StepIO::importShape(path);
    ASSERT_TRUE(imported.success) << imported.error.toStdString();
    expectEquivalent(source, imported.shape);
}

TEST(StepImportTest, MissingEmptyAndCorruptFilesFailWithoutPartialGeometry)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto empty = temp.filePath("empty.step");
    QFile file(empty);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    const auto corrupt = temp.filePath("broken.step");
    file.setFileName(corrupt);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not a STEP file");
    file.close();
    for (const auto& path : {QString("  "), temp.path(), temp.filePath("missing.step"), empty, corrupt}) {
        const auto result = StepIO::importShape(path);
        EXPECT_FALSE(result.success);
        EXPECT_TRUE(result.shape.IsNull());
        EXPECT_FALSE(result.error.isEmpty());
    }
}

TEST(ImportedDocumentTest, HistoryRetainsGeometryAfterSourceFileIsRemoved)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto source = ShapeFactory::translate(ShapeFactory::makeBox(10, 20, 30), -15, 2, -7);
    const auto path = temp.filePath("source.step");
    ASSERT_TRUE(StepIO::exportShape(source, path).success);
    const auto imported = StepIO::importShape(path);
    ASSERT_TRUE(imported.success);
    forge::application::ModelDocument document;
    document.createFeature("Sphere", {{"radius", 3}});
    EXPECT_EQ(document.createImportedFeature(imported.shape, "source.step").id(), "Imported001");
    ASSERT_TRUE(QFile::remove(path));
    document.setParameter("Imported001", "x", -2);
    expectEquivalent(ShapeFactory::translate(source, -2, 0, 0), document.rebuildShapes().at("Imported001"));
    document.undo();
    expectEquivalent(source, document.rebuildShapes().at("Imported001"));
    document.undo();
    EXPECT_EQ(document.findFeature("Imported001"), nullptr);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
    document.redo();
    expectEquivalent(source, document.rebuildShapes().at("Imported001"));
    document.redo();
    expectEquivalent(ShapeFactory::translate(source, -2, 0, 0), document.rebuildShapes().at("Imported001"));
    const auto* feature = dynamic_cast<const forge::domain::ImportedFeature*>(document.findFeature("Imported001"));
    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->sourceName(), "source.step");
    document.deleteFeature("Imported001");
    document.undo();
    expectEquivalent(ShapeFactory::translate(source, -2, 0, 0), document.rebuildShapes().at("Imported001"));
}

TEST(ImportedDocumentTest, BooleanDependenciesAndCascadeHistoryUseImportedGeometry)
{
    forge::application::ModelDocument document;
    document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
    document.createImportedFeature(ShapeFactory::makeBox(5, 5, 5), "tool.step");
    document.createBooleanFeature(forge::domain::BooleanOperation::Difference, "Box001", "Imported001");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(document.shapeForExport().shape, properties);
    EXPECT_NEAR(properties.Mass(), 875, 1e-5);
    document.setParameter("Imported001", "x", 20);
    BRepGProp::VolumeProperties(document.shapeForExport().shape, properties);
    EXPECT_NEAR(properties.Mass(), 1000, 1e-5);
    document.deleteFeature("Imported001");
    EXPECT_EQ(document.findFeature("Cut001"), nullptr);
    document.undo();
    EXPECT_NE(document.findFeature("Cut001"), nullptr);
    BRepGProp::VolumeProperties(document.shapeForExport().shape, properties);
    EXPECT_NEAR(properties.Mass(), 1000, 1e-5);
}

TEST(ImportedDocumentTest, InvalidImportDoesNotChangeModelOrRedo)
{
    forge::application::ModelDocument document;
    document.createFeature("Sphere", {{"radius", 3}});
    document.undo();
    EXPECT_THROW(document.createImportedFeature({}, "broken.step"), std::invalid_argument);
    EXPECT_TRUE(document.features().empty());
    EXPECT_TRUE(document.canRedo());
    document.redo();
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
    EXPECT_THROW(document.createFeature("Imported", {}), std::invalid_argument);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}
