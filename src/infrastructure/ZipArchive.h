#pragma once
#include <QByteArray>
#include <map>
namespace forge::infrastructure {
using ZipEntries = std::map<QByteArray,QByteArray>;
class ZipArchive {
public:
    static constexpr qsizetype maximumBytes = 64 * 1024 * 1024;
    static QByteArray encode(const ZipEntries& entries);
    static ZipEntries decode(const QByteArray& bytes);
};
}
