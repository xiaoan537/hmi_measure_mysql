#include "manual_maintain_logic.hpp"

namespace manual_maintain_logic {

QString plcModeText(int mode)
{
    switch (mode) {
    case 1: return QStringLiteral("手动");
    case 2: return QStringLiteral("自动");
    case 3: return QStringLiteral("单步");
    default: return QStringLiteral("模式(%1)").arg(mode);
    }
}

QPair<QString, int> parseCylinderSelector(const QString &rawData)
{
    const QStringList parts = rawData.split(':');
    QString group = parts.value(0).trimmed();
    if (group.isEmpty()) group = QStringLiteral("LM");

    bool ok = false;
    int index = parts.value(1).toInt(&ok);
    if (!ok) index = 0;
    return qMakePair(group, index);
}

QVector<ParsedStateRow> parseStateRows(const QString &text, int valueCount)
{
    QVector<ParsedStateRow> rows;
    const QStringList lines = text.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
    rows.reserve(lines.size());

    for (const QString &line : lines) {
        const QStringList parts = line.split('|');
        if (parts.isEmpty()) continue;

        ParsedStateRow row;
        row.name = parts.at(0).trimmed();

        QStringList values;
        if (parts.size() > 1) {
            const QStringList kvs = parts.at(1).split(' ', Qt::SkipEmptyParts);
            for (const QString &kv : kvs) {
                const int pos = kv.indexOf('=');
                values << (pos >= 0 ? kv.mid(pos + 1).trimmed() : kv.trimmed());
            }
        }

        while (values.size() < valueCount) values << QStringLiteral("-");
        if (values.size() > valueCount) values = values.mid(0, valueCount);
        row.values = values;
        rows.push_back(row);
    }

    return rows;
}

} // namespace manual_maintain_logic
