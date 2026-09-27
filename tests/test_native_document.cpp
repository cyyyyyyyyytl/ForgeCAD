#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/BooleanFeature.h"
#include "geometry/ShapeFactory.h"
#include "infrastructure/DocumentJson.h"
#include "infrastructure/NativeDocumentIO.h"
#include <QTemporaryDir>
#include <QFile>
#include <QUuid>
#include <limits>
using forge::application::ModelDocument;
using forge::infrastructure::NativeDocumentIO;
using forge::infrastructure::DocumentJson;

TEST(NativeDocumentTest, RoundTripRestoresIdentityDependenciesAndIndependentGeometry) {
    ModelDocument original;
    original.createFeature("Box",{{"length",10},{"width",10},{"height",10},{"x",-3}});
    original.createImportedFeature(forge::geometry::ShapeFactory::makeCylinder(2,12),"零件.step");
    original.setParameter("Imported001","y",4);
    original.createBooleanFeature(forge::domain::BooleanOperation::Difference,"Box001","Imported001");
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath(QStringLiteral("模型.forgecad"));
    const auto before=original.exportData();
    NativeDocumentIO::save(before,path);
    ModelDocument opened; opened.replaceData(NativeDocumentIO::load(path));
    const auto after=opened.exportData();
    EXPECT_EQ(after.documentId,before.documentId);
    EXPECT_EQ(after.sequences,before.sequences);
    EXPECT_EQ(DocumentJson::encode(after),DocumentJson::encode(before));
    EXPECT_FALSE(opened.canUndo()); EXPECT_FALSE(opened.canRedo());
    EXPECT_EQ(opened.rebuildReport().at("Cut001").status,original.rebuildReport().at("Cut001").status);
    opened.createFeature("Box",{{"length",1},{"width",1},{"height",1}});
    EXPECT_NE(opened.findFeature("Box002"),nullptr);
    EXPECT_EQ(original.exportData().documentId,before.documentId);
}
TEST(NativeDocumentTest, InvalidDefinitionsLeaveCurrentModelAndHistoryIntact) {
    ModelDocument document;
    document.createFeature("Box",{{"length",2},{"width",2},{"height",2}});
    document.setParameter("Box001","x",3); document.undo();
    const auto before=document.exportData();
    auto check=[&](const auto& bad) {
        EXPECT_THROW(document.replaceData(bad),std::exception);
        EXPECT_EQ(DocumentJson::encode(document.exportData()),DocumentJson::encode(before));
        EXPECT_TRUE(document.canRedo());
    };
    auto bad=before; bad.features.push_back(bad.features[0]); check(bad);
    bad=before; bad.features[0].dependencies={"missing"}; check(bad);
    bad=before; bad.features[0].dependencies={"Box001"}; check(bad);
    bad=before; bad.sequences["Box"]=0; check(bad);
    bad=before; bad.features[0].version=2; check(bad);
    bad=before; bad.features[0].parameters["x"]=std::numeric_limits<double>::infinity(); check(bad);
    bad=before; bad.geometryAssets["../outside.brep"]="bad"; check(bad);
    bad=before; bad.documentId="bad"; check(bad);
}
TEST(NativeDocumentTest, MissingOrCorruptBrepIsRejected) {
    ModelDocument document;
    document.createImportedFeature(forge::geometry::ShapeFactory::makeBox(2,3,4),"source.step");
    auto data=document.exportData();
    data.geometryAssets.clear(); EXPECT_THROW(document.replaceData(data),std::exception);
    data=document.exportData(); data.geometryAssets.begin()->second="not a BRep";
    EXPECT_THROW(document.replaceData(data),std::exception);
}
TEST(NativeDocumentTest, CorruptionAndUnsupportedJsonFailWithoutOverwritingSavedFile) {
    ModelDocument document;
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("part.forgecad");
    auto data=document.exportData(); NativeDocumentIO::save(data,path);
    QFile file(path); ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto bytes=file.readAll(); file.close();
    data.version=2; EXPECT_THROW(NativeDocumentIO::save(data,path),std::exception);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)); EXPECT_EQ(file.readAll(),bytes); file.close();
    auto corrupt=bytes;
    const auto nameLength=quint8(bytes[26]) | (quint16(quint8(bytes[27]))<<8);
    const auto extraLength=quint8(bytes[28]) | (quint16(quint8(bytes[29]))<<8);
    const auto content=30+nameLength+extraLength;
    corrupt[content]=char(corrupt[content]^0x55);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write(corrupt); file.close();
    EXPECT_THROW(NativeDocumentIO::load(path),std::exception);
    EXPECT_THROW(DocumentJson::decode("{}"),std::exception);
    EXPECT_THROW(DocumentJson::decode("{broken"),std::exception);
}
TEST(NativeDocumentTest, EmptyBooleanDefinitionCanBeSavedAndRebuilt) {
    ModelDocument document;
    document.createFeature("Box",{{"length",2},{"width",2},{"height",2}});
    document.createFeature("Box",{{"length",2},{"width",2},{"height",2},{"x",10}});
    document.createBooleanFeature(forge::domain::BooleanOperation::Intersection,"Box001","Box002");
    QTemporaryDir dir;
    NativeDocumentIO::save(document.exportData(),dir.filePath("empty.forgecad"));
    ModelDocument restored; restored.replaceData(NativeDocumentIO::load(dir.filePath("empty.forgecad")));
    EXPECT_EQ(restored.rebuildReport().at("Intersection001").status,forge::core::RebuildStatus::Empty);
}
TEST(NativeDocumentTest, DocumentIdentitySurvivesHistoryAndDiffersForNewDocument) {
    ModelDocument a,b;
    const auto identity=a.exportData().documentId;
    EXPECT_FALSE(QUuid(QString::fromStdString(identity)).isNull());
    EXPECT_NE(identity,b.exportData().documentId);
    a.createFeature("Sphere",{{"radius",2}}); a.undo(); a.redo();
    EXPECT_EQ(a.exportData().documentId,identity);
}

TEST(NativeDocumentTest, SavePointTracksUndoRedoAndBranchingWithoutSerialization) {
    ModelDocument document;
    EXPECT_FALSE(document.isModified());
    document.createFeature("Sphere",{{"radius",2}});
    EXPECT_TRUE(document.isModified());
    document.markSaved();
    document.setParameter("Sphere001","radius",2);
    EXPECT_FALSE(document.isModified());
    document.setParameter("Sphere001","radius",3);
    EXPECT_TRUE(document.isModified());
    document.undo(); EXPECT_FALSE(document.isModified());
    document.redo(); EXPECT_TRUE(document.isModified());
    document.markSaved();
    document.undo(); EXPECT_TRUE(document.isModified());
    document.setParameter("Sphere001","radius",4);
    EXPECT_TRUE(document.isModified()); EXPECT_FALSE(document.canRedo());
}

TEST(NativeDocumentTest, PreparedDocumentCommitsOnceAndIsInitiallyClean) {
    ModelDocument document;
    document.createFeature("Sphere",{{"radius",2}});
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("part.forgecad");
    NativeDocumentIO::save(document.exportData(),path);
    auto prepared=NativeDocumentIO::prepare(path);
    EXPECT_FALSE(prepared->isModified());
    ModelDocument target;
    target.swap(*prepared);
    EXPECT_NE(target.findFeature("Sphere001"),nullptr);
    EXPECT_EQ(target.exportData().documentId,document.exportData().documentId);
    EXPECT_FALSE(target.isModified()); EXPECT_FALSE(target.canUndo());
}
