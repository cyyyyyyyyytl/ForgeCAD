#pragma once
#include "application/DocumentData.h"
#include <QString>
#include <memory>
namespace forge::application { class ModelDocument; }
namespace forge::infrastructure {
// 失败抛异常；调用方显示错误。保存使用原子替换，读取不修改当前模型。
class NativeDocumentIO {
public:
    static void save(const application::DocumentData& data, const QString& path);
    static application::DocumentData load(const QString& path);
    static std::unique_ptr<application::ModelDocument> prepare(const QString& path);
};
}
