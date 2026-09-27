#include <gtest/gtest.h>
#include "infrastructure/ZipArchive.h"
#include <miniz.h>
#include <memory>
using forge::infrastructure::ZipArchive;
using forge::infrastructure::ZipEntries;
namespace {
QByteArray externalArchive(const std::vector<std::pair<const char*,QByteArray>>& entries) {
    mz_zip_archive zip{};
    EXPECT_TRUE(mz_zip_writer_init_heap(&zip,0,0));
    for (const auto& [name,payload] : entries)
        EXPECT_TRUE(mz_zip_writer_add_mem(&zip,name,payload.constData(),payload.size(),MZ_BEST_SPEED));
    void* bytes=nullptr; size_t size=0;
    EXPECT_TRUE(mz_zip_writer_finalize_heap_archive(&zip,&bytes,&size));
    const std::unique_ptr<void,decltype(&mz_free)> owner(bytes,&mz_free);
    QByteArray result(static_cast<const char*>(bytes),static_cast<qsizetype>(size));
    mz_zip_writer_end(&zip); return result;
}
}
TEST(ZipArchiveTest, CompressedArchivePreservesBinaryData) {
    const ZipEntries entries{{"document.json",QByteArray(10000,'x')},{"shapes/A.brep",QByteArray("a\0b",3)}};
    const auto bytes=ZipArchive::encode(entries);
    EXPECT_LT(bytes.size(),1000);
    EXPECT_EQ(ZipArchive::decode(bytes),entries);
}
TEST(ZipArchiveTest, ReadsDeflatedArchiveFromDotNetIndependentWriter) {
    // 用 .NET System.IO.Compression 生成，避免只验证同库自写自读。
    const auto bytes=QByteArray::fromBase64("UEsDBBQAAAAIAOxCO10m8NsAHAAAAIgTAAANAAAAZG9jdW1lbnQuanNvbuzBMQEAAADCoNqLbwo/oAAAAACAzgYAAP//AwBQSwECFAAUAAAACADsQjtdJvDbABwAAACIEwAADQAAAAAAAAAAAAAAAAAAAAAAZG9jdW1lbnQuanNvblBLBQYAAAAAAQABADsAAABHAAAAAAA=");
    EXPECT_EQ(ZipArchive::decode(bytes).at("document.json"),QByteArray(5000,'x'));
}
TEST(ZipArchiveTest, RejectsTraversalAbsoluteAndDuplicateNames) {
    for (const auto* name : {"../escape","/absolute","C:/drive","shapes\\bad","shapes/../escape"}) {
        EXPECT_THROW(ZipArchive::encode({{name,"x"}}),std::exception);
        // miniz 写入器本身拒绝绝对路径；等长替换名字构造外部恶意包。
        auto malformed = name[0]=='/' ? externalArchive({{"xabsolute","x"}}).replace("xabsolute","/absolute")
                                     : externalArchive({{name,"x"}});
        EXPECT_THROW(ZipArchive::decode(malformed),std::exception);
    }
    EXPECT_THROW(ZipArchive::decode(externalArchive({{"document.json","a"},{"document.json","b"}})),std::exception);
}
TEST(ZipArchiveTest, RejectsTruncatedAndCorruptPayload) {
    const auto bytes=ZipArchive::encode({{"document.json",QByteArray(5000,'x')}});
    EXPECT_THROW(ZipArchive::decode(bytes.left(20)),std::exception);
    auto corrupt=bytes;
    // 找到压缩内容起点，不修改目录 CRC。
    const auto nameLength=quint8(bytes[26]) | (quint16(quint8(bytes[27]))<<8);
    const auto extraLength=quint8(bytes[28]) | (quint16(quint8(bytes[29]))<<8);
    const auto offset=30+nameLength+extraLength;
    corrupt[offset]=char(corrupt[offset]^0x55);
    EXPECT_THROW(ZipArchive::decode(corrupt),std::exception);
}
TEST(ZipArchiveTest, RejectsCompressedBombBeforeAllocation) {
    const auto bytes=externalArchive({{"large.brep",QByteArray(ZipArchive::maximumBytes+1,'x')}});
    EXPECT_LT(bytes.size(),1024*1024);
    EXPECT_THROW(ZipArchive::decode(bytes),std::exception);
}
