#include "StepIO.h"
#include "geometry/ShapeFactory.h"

#include <QFile>
#include <QFileInfo>
#include <STEPControl_Reader.hxx>
#include <QSaveFile>
#include <STEPControl_Writer.hxx>
#include <StepData_StepModel.hxx>
#include <DESTEP_Parameters.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Standard_Failure.hxx>

#include <exception>
#include <sstream>

namespace forge::infrastructure {

StepImportResult StepIO::importShape(const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) return {false, {}, QStringLiteral("导入路径不能为空")};
    if (!QFileInfo(filePath).isFile()) return {false, {}, QStringLiteral("STEP 文件不存在或不是普通文件")};
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return {false, {}, file.errorString()};
        // 当前同步读取使用内存缓冲；限制大小，后续大模型改用流和异步任务。
        constexpr qint64 maximumBytes = 256 * 1024 * 1024;
        if (file.size() > maximumBytes) return {false, {}, QStringLiteral("STEP 文件超过当前 256 MB 导入限制")};
        const QByteArray bytes = file.read(maximumBytes + 1);
        if (file.error() != QFileDevice::NoError) return {false, {}, file.errorString()};
        if (bytes.size() > maximumBytes) return {false, {}, QStringLiteral("STEP 文件超过当前导入限制")};
        if (bytes.isEmpty()) return {false, {}, QStringLiteral("STEP 文件为空")};

        // Qt 处理 Unicode 路径，OCCT 处理文件内容，避免窄字符路径造成中文读取失败。
        std::istringstream input(std::string(bytes.constData(), bytes.size()));
        STEPControl_Reader reader;
        DESTEP_Parameters parameters;
        if (reader.ReadStream("import.step", parameters, input) != IFSelect_RetDone) {
            return {false, {}, QStringLiteral("文件不是可读取的 STEP，或数据已损坏")};
        }
        reader.SetSystemLengthUnit(1.0); // 文件单位可以是英寸等，文档坐标统一转换成毫米。
        const int roots = reader.NbRootsForTransfer();
        if (roots <= 0) return {false, {}, QStringLiteral("STEP 文件没有可导入的几何")};
        if (reader.TransferRoots() != roots) {
            return {false, {}, QStringLiteral("部分 STEP 几何转换失败，已取消整个导入")};
        }
        const auto result = geometry::ShapeFactory::inspectShape(reader.OneShape());
        if (!result.usable()) return {false, {}, QString::fromStdString(result.message)};
        if (result.status == core::RebuildStatus::Empty) return {false, {}, QStringLiteral("STEP 几何为空")};
        return {true, result.shape, {}};
    } catch (const Standard_Failure& error) {
        return {false, {}, QStringLiteral("STEP 内核异常：%1").arg(QString::fromUtf8(
            error.GetMessageString() ? error.GetMessageString() : "未知原因"))};
    } catch (const std::exception& error) {
        return {false, {}, QStringLiteral("STEP 导入异常：%1").arg(QString::fromUtf8(error.what()))};
    }
}

StepExportResult StepIO::exportShape(
    const TopoDS_Shape& shape,
    const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        return {false, QStringLiteral("导出路径不能为空")};
    }

    try {
        // 复用已有几何检查，拒绝失败形状和合法空结果。
        const auto checked = geometry::ShapeFactory::inspectShape(shape);
        if (!checked.usable()) {
            return {false, QString::fromStdString(checked.message)};
        }
        if (checked.status == core::RebuildStatus::Empty) {
            return {false, QStringLiteral("结果为空，没有可导出的几何")};
        }

        STEPControl_Writer writer;

        // 当前模型坐标以毫米计，输出文件也明确使用毫米。
        writer.Model()->SetLocalLengthUnit(1.0);
        DESTEP_Parameters parameters;
        parameters.WriteUnit = UnitsMethods_LengthUnit_Millimeter;

        // Transfer 只转换几何，还没有写入文件。
        if (writer.Transfer(shape, STEPControl_AsIs, parameters)
            != IFSelect_RetDone) {
            return {false, QStringLiteral("几何转换为 STEP 失败")};
        }

        // 先生成完整数据，转换失败不会碰目标文件。
        std::ostringstream stream;
        if (writer.WriteStream(stream) != IFSelect_RetDone
            || !stream.good()) {
            return {false, QStringLiteral("生成 STEP 数据失败")};
        }

        const std::string data = stream.str();
        if (data.empty()) {
            return {false, QStringLiteral("生成的 STEP 数据为空")};
        }

        // QSaveFile 写入临时文件，成功提交后才替换目标文件。
        // 禁止退回直接覆盖，保证写入失败时旧文件仍然存在。
        QSaveFile file(filePath);
        file.setDirectWriteFallback(false);

        if (!file.open(QIODevice::WriteOnly)) {
            return {false, file.errorString()};
        }

        const auto size = static_cast<qint64>(data.size());
        if (file.write(data.data(), size) != size) {
            const QString error = file.errorString();
            file.cancelWriting();
            return {false, error};
        }

        if (!file.commit()) {
            return {false, file.errorString()};
        }

        return {true, {}};
    } catch (const Standard_Failure& error) {
        return {
            false,
            QStringLiteral("STEP 内核异常：%1").arg(
                QString::fromUtf8(error.GetMessageString()
                    ? error.GetMessageString() : "未知原因"))
        };
    } catch (const std::exception& error) {
        return {
            false,
            QStringLiteral("STEP 导出异常：%1")
                .arg(QString::fromUtf8(error.what()))
        };
    }
}

} // namespace forge::infrastructure