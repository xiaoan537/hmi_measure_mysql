#pragma once

#include <QString>
#include <QtGlobal>

#include "core/measurement_pipeline.hpp"
#include "production_widget_model.hpp"

namespace production_widget_logic {

QString formatFloat(float v, int prec = 3);
QString shortId(const QString &id32);
QString machineStateMaskText(quint16 mask);
SlotMeasureSummary toWidgetSummary(const core::ProductionSlotSummary &src);
QString runtimeStateText(SlotRuntimeState state);
int runtimeStateStyleCode(SlotRuntimeState state);
bool isPartIdEditableStep(quint16 stepState);
QString stepText(quint16 step);
QString partTypeTextFromData(const QString &rawValue);
QString measureModeText(ProductionMeasureMode mode);
quint32 measureModeCommandArg(ProductionMeasureMode mode);

} // namespace production_widget_logic
