#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace manual_maintain_logic {

struct ParsedStateRow {
    QString name;
    QStringList values;
};

QString plcModeText(int mode);
QPair<QString, int> parseCylinderSelector(const QString &rawData);
QVector<ParsedStateRow> parseStateRows(const QString &text, int valueCount);

} // namespace manual_maintain_logic
