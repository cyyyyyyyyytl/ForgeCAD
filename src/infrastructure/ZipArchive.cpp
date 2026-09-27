#include "infrastructure/ZipArchive.h"
#include <miniz.h>
#include <QList>
#include <set>
#include <memory>
#include <stdexcept>
namespace forge::infrastructure {
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool safe(const QByteArray& name) {
    if (name.isEmpty() || name.size()>200 || name.startsWith('/') || name.contains('\\') || name.contains(':') || name.contains('\0')) return false;
    for (const auto& part : name.split('/')) if (part.isEmpty() || part=="." || part=="..") return false;
    return true;
}
struct Archive {
    mz_zip_archive zip{};
    ~Archive() { mz_zip_end(&zip); }
};
}
QByteArray ZipArchive::encode(const ZipEntries& entries) {
    require(entries.size()<=10002,"ZIP 条目过多");
    qsizetype total=0;
    for (const auto& [name,payload] : entries) {
        require(safe(name),"ZIP 条目路径无效");
        require(payload.size()<=maximumBytes-total,"文档解压大小超过 64 MiB");
        total+=payload.size();
    }
    Archive archive;
    require(mz_zip_writer_init_heap(&archive.zip,0,0),"ZIP 初始化失败");
    for (const auto& [name,payload] : entries)
        require(mz_zip_writer_add_mem(&archive.zip,name.constData(),payload.constData(),payload.size(),MZ_BEST_SPEED),"ZIP 写入失败");
    void* memory=nullptr; size_t size=0;
    require(mz_zip_writer_finalize_heap_archive(&archive.zip,&memory,&size),"ZIP 完成失败");
    const std::unique_ptr<void,decltype(&mz_free)> owner(memory,&mz_free);
    require(size<=size_t(maximumBytes),"文档文件超过 64 MiB");
    return QByteArray(static_cast<const char*>(memory),static_cast<qsizetype>(size));
}
ZipEntries ZipArchive::decode(const QByteArray& bytes) {
    require(!bytes.isEmpty() && bytes.size()<=maximumBytes,"文档文件为空或超过 64 MiB");
    Archive archive;
    require(mz_zip_reader_init_mem(&archive.zip,bytes.constData(),bytes.size(),0),"ZIP 目录损坏");
    const auto count=mz_zip_reader_get_num_files(&archive.zip);
    require(count<=10002,"ZIP 条目过多");
    ZipEntries entries;
    qsizetype total=0;
    // 在分配/解压前检查全部条目，限制解压总量而不只限制压缩文件。
    std::set<QByteArray> names;
    for (mz_uint i=0;i<count;++i) {
        mz_zip_archive_file_stat stat{};
        require(mz_zip_reader_file_stat(&archive.zip,i,&stat),"ZIP 条目损坏");
        const auto length=mz_zip_reader_get_filename(&archive.zip,i,nullptr,0);
        require(length>1 && length<=201,"ZIP 路径过长或为空");
        QByteArray name(length,'\0');
        require(mz_zip_reader_get_filename(&archive.zip,i,name.data(),length)==length,"ZIP 文件名损坏");
        name.chop(1);
        require(safe(name) && names.insert(name).second,"ZIP 路径非法或条目重复");
        require(!stat.m_is_directory && !stat.m_is_encrypted && stat.m_is_supported,"ZIP 条目格式不支持");
        require(stat.m_uncomp_size<=mz_uint64(maximumBytes-total),"文档解压大小超过 64 MiB");
        total+=static_cast<qsizetype>(stat.m_uncomp_size);
    }
    for (mz_uint i=0;i<count;++i) {
        mz_zip_archive_file_stat stat{};
        require(mz_zip_reader_file_stat(&archive.zip,i,&stat),"ZIP 条目损坏");
        const auto length=mz_zip_reader_get_filename(&archive.zip,i,nullptr,0);
        QByteArray name(length,'\0'); mz_zip_reader_get_filename(&archive.zip,i,name.data(),length); name.chop(1);
        QByteArray payload(static_cast<qsizetype>(stat.m_uncomp_size),'\0');
        require(mz_zip_reader_extract_to_mem(&archive.zip,i,payload.data(),payload.size(),0),"ZIP 数据解压或 CRC 校验失败");
        entries.emplace(name,std::move(payload));
    }
    return entries;
}
}
