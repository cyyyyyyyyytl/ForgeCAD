#pragma once

#include <TopoDS_Shape.hxx>
#include <QString>

namespace forge::infrastructure {

    // success 表示文件已经成功写入；失败时 error 保存原因。
    struct StepExportResult {
        bool success = false;
        QString error;
    };

    struct StepImportResult {
        bool success = false;
        TopoDS_Shape shape;
        QString error;
    };

    class StepIO {
    public:
        // 整个 STEP 文件作为一组几何读取，统一转换为毫米；失败不返回部分模型。
        static StepImportResult importShape(const QString& filePath);
        // 只负责导出几何，不修改文档，也不产生撤销记录。
        static StepExportResult exportShape(
            const TopoDS_Shape& shape,
            const QString& filePath);
    };

} // namespace forge::infrastructure