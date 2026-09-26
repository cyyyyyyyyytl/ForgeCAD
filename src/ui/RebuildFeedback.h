#pragma once
#include "core/ShapeResult.h"
#include <QColor>
#include <QStandardItem>
#include <QString>

namespace forge::ui {
inline QString rebuildStatusLabel(core::RebuildStatus status)
{
    switch (status) {
    case core::RebuildStatus::Ready: return QStringLiteral("正常");
    case core::RebuildStatus::Empty: return QStringLiteral("空结果");
    case core::RebuildStatus::Failed: return QStringLiteral("重建失败");
    case core::RebuildStatus::Blocked: return QStringLiteral("上游失败");
    }
    return {};
}
// 同一个节点恢复后必须清掉错误文字和颜色，ID 始终保存在 UserRole 中。
inline void decorateRebuildItem(QStandardItem& item, const QString& id, const core::ShapeResult& result)
{
    item.setText(result.status == core::RebuildStatus::Ready ? id : id + " · " + rebuildStatusLabel(result.status));
    item.setForeground(!result.usable() ? QBrush(QColor(190, 45, 45))
        : result.status == core::RebuildStatus::Empty ? QBrush(QColor(140, 100, 20)) : QBrush());
    item.setToolTip(rebuildStatusLabel(result.status) + "：" + QString::fromStdString(result.message));
    item.setData(static_cast<int>(result.status), Qt::UserRole + 1);
}
} // namespace forge::ui
