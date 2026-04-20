#pragma once

#include <QString>
#include <QtGlobal>

#include "core/measurement_pipeline.hpp"
#include "production_widget_model.hpp"

namespace production_widget_logic {

struct RuntimeSlotDecision {
    SlotRuntimeState state = SlotRuntimeState::Empty;
    QString note;
};

QString formatFloat(float v, int prec = 3);
QString shortId(const QString &id32);
QString machineStateMaskText(quint16 mask);
SlotMeasureSummary toWidgetSummary(const core::ProductionSlotSummary &src);
RuntimeSlotDecision decideRuntimeSlotState(int slot,
                                           int reservedCalibrationSlot,
                                           bool calibrationMode,
                                           quint16 stepState,
                                           quint16 scanDone,
                                           quint16 mailboxReady,
                                           quint16 trayPresentMask,
                                           quint16 activeSlotMask);
QString runtimeStateText(SlotRuntimeState state);
int runtimeStateStyleCode(SlotRuntimeState state);
bool isPartIdEditableStep(quint16 stepState);
QString stepText(quint16 step);
QString partTypeTextFromData(const QString &rawValue);
QString measureModeText(ProductionMeasureMode mode);
quint32 measureModeCommandArg(ProductionMeasureMode mode);
bool shouldInvalidateResultOnIdChange(quint16 stepState, quint16 scanDone);
bool shouldShowComputedResult(const SlotMeasureSummary &summary,
                              SlotRuntimeState runtimeState,
                              quint32 slotResultToken,
                              quint32 currentCycleToken);

} // namespace production_widget_logic
