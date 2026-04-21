#pragma once

#include <QString>
#include <QtGlobal>

#include "core/measurement_pipeline.hpp"

namespace calibration_widget_logic {

QString plcConnStyle(bool connected);
QString stepText(quint16 step);
QString calibrationSlotStateText(quint16 trayPresentMask);
QString selectedMasterTypeText(bool bChecked);
quint32 selectedMasterTypeArg(bool bChecked);
QString buildSummaryText(const core::CalibrationSlotSummary &summary);

} // namespace calibration_widget_logic
