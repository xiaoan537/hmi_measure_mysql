#include "production_widget_logic.hpp"
#include "plc_step_rules_v26.hpp"

namespace production_widget_logic {

QString formatFloat(float v, int prec)
{
    if (qIsNaN(v) || qIsInf(v)) return QStringLiteral("--");
    return QString::number(v, 'f', prec);
}

QString shortId(const QString &id32)
{
    if (id32.isEmpty()) return QStringLiteral("—");
    if (id32.size() <= 10) return id32;
    return id32.left(6) + QStringLiteral("…") + id32.right(3);
}

QString machineStateMaskText(quint16 mask)
{
    QStringList parts;
    if (mask & (1u << 0)) parts << QStringLiteral("空闲");
    if (mask & (1u << 1)) parts << QStringLiteral("自动");
    if (mask & (1u << 2)) parts << QStringLiteral("手动");
    if (mask & (1u << 3)) parts << QStringLiteral("暂停");
    if (mask & (1u << 4)) parts << QStringLiteral("错误");
    if (mask & (1u << 5)) parts << QStringLiteral("急停");
    if (!parts.isEmpty()) return parts.join(QStringLiteral(" | "));
    return QStringLiteral("状态(%1)").arg(mask);
}

SlotMeasureSummary toWidgetSummary(const core::ProductionSlotSummary &src)
{
    SlotMeasureSummary out;
    out.part_type = src.part_type;
    out.valid = src.valid || src.compute.valid;
    out.judgement_known = src.judgement_known;
    out.judgement_ok = src.judgement_ok;
    out.fail_reason_text = !src.fail_reason_text.isEmpty() ? src.fail_reason_text
                                                           : src.compute.fail_reason_text;

    out.a_total_len_mm = src.compute.values.total_len_mm;
    out.a_id_left_mm = src.compute.values.id_left_mm;
    out.a_od_left_mm = src.compute.values.od_left_mm;
    out.a_id_right_mm = src.compute.values.id_right_mm;
    out.a_od_right_mm = src.compute.values.od_right_mm;

    out.b_ad_len_mm = src.compute.values.ad_len_mm;
    out.b_bc_len_mm = src.compute.values.bc_len_mm;
    out.b_runout_left_mm = src.compute.values.runout_left_mm;
    out.b_runout_right_mm = src.compute.values.runout_right_mm;
    return out;
}

RuntimeSlotDecision decideRuntimeSlotState(int slot,
                                           int reservedCalibrationSlot,
                                           bool calibrationMode,
                                           quint16 stepState,
                                           quint16 scanDone,
                                           quint16 mailboxReady,
                                           quint16 trayPresentMask,
                                           quint16 activeSlotMask)
{
    RuntimeSlotDecision out;
    const bool isActive = ((activeSlotMask >> slot) & 0x1u) != 0;
    const bool present = ((trayPresentMask >> slot) & 0x1u) != 0;
    if (!present && !isActive) {
        out.state = SlotRuntimeState::Empty;
        return out;
    }

    out.state = SlotRuntimeState::Loaded;
    if (calibrationMode && slot == reservedCalibrationSlot) {
        out.state = SlotRuntimeState::Calibration;
        return out;
    }

    if (plc_step_rules_v26::isScanStep(stepState)) {
        if (scanDone != 0) {
            out.state = SlotRuntimeState::WaitingIdCheck;
            out.note = QStringLiteral("等待 PC 核对 ID");
        } else {
            out.state = SlotRuntimeState::Loaded;
            out.note = QStringLiteral("PLC 扫码中");
        }
    }

    if (!isActive) return out;
    if (plc_step_rules_v26::isMailboxArchiveStep(stepState)) {
        if (mailboxReady != 0) {
            out.state = SlotRuntimeState::WaitingPcRead;
            out.note = QStringLiteral("已测完成，等待 PC 读取并 ACK");
        } else {
            out.state = SlotRuntimeState::Loaded;
            out.note = QStringLiteral("等待数据归档");
        }
        return out;
    }
    if (plc_step_rules_v26::isProductionProcessingStep(stepState)) {
        out.state = SlotRuntimeState::Loaded;
        out.note.clear();
    }
    return out;
}

QString runtimeStateText(SlotRuntimeState state)
{
    switch (state) {
    case SlotRuntimeState::Empty: return QStringLiteral("空");
    case SlotRuntimeState::Loaded: return QStringLiteral("已上料");
    case SlotRuntimeState::WaitingIdCheck: return QStringLiteral("待核对ID");
    case SlotRuntimeState::ScanMismatch: return QStringLiteral("ID不一致");
    case SlotRuntimeState::Measuring: return QStringLiteral("已上料");
    case SlotRuntimeState::WaitingPcRead: return QStringLiteral("待读取");
    case SlotRuntimeState::Ok: return QStringLiteral("OK");
    case SlotRuntimeState::Ng: return QStringLiteral("NG");
    case SlotRuntimeState::Calibration: return QStringLiteral("标定槽");
    case SlotRuntimeState::Unknown:
    default: return QStringLiteral("未知");
    }
}

int runtimeStateStyleCode(SlotRuntimeState state)
{
    switch (state) {
    case SlotRuntimeState::Ok: return 1;
    case SlotRuntimeState::Ng:
    case SlotRuntimeState::ScanMismatch: return 2;
    case SlotRuntimeState::Loaded:
    case SlotRuntimeState::WaitingIdCheck:
    case SlotRuntimeState::Measuring:
    case SlotRuntimeState::Unknown: return 3;
    case SlotRuntimeState::Calibration: return 4;
    case SlotRuntimeState::WaitingPcRead: return 5;
    case SlotRuntimeState::Empty:
    default: return 0;
    }
}

bool isPartIdEditableStep(quint16 stepState)
{
    // 扫码流程阶段可编辑，具体是否允许由 scan_done 决定
    return plc_step_rules_v26::isScanStep(stepState);
}

QString stepText(quint16 step)
{
    return plc_step_rules_v26::productionStepText(step);
}

QString partTypeTextFromData(const QString &rawValue)
{
    const QString t = rawValue.trimmed().toUpper();
    return (t == QStringLiteral("B")) ? QStringLiteral("B") : QStringLiteral("A");
}

QString measureModeText(ProductionMeasureMode mode)
{
    switch (mode) {
    case ProductionMeasureMode::Second: return QStringLiteral("SECOND");
    case ProductionMeasureMode::Third: return QStringLiteral("THIRD");
    case ProductionMeasureMode::Mil: return QStringLiteral("MIL");
    case ProductionMeasureMode::Normal:
    default: return QStringLiteral("NORMAL");
    }
}

quint32 measureModeCommandArg(ProductionMeasureMode mode)
{
    switch (mode) {
    case ProductionMeasureMode::Second: return 2;
    case ProductionMeasureMode::Third: return 3;
    case ProductionMeasureMode::Mil: return 9;
    case ProductionMeasureMode::Normal:
    default: return 1;
    }
}

bool shouldInvalidateResultOnIdChange(quint16 stepState, quint16 /*scanDone*/)
{
    // 仅在扫码阶段允许 ID 变化导致本槽结果失效，避免低频轮询在后续步骤误清结果。
    return plc_step_rules_v26::isScanStep(stepState);
}

bool shouldShowComputedResult(const SlotMeasureSummary &summary,
                              SlotRuntimeState runtimeState,
                              quint32 slotResultToken,
                              quint32 currentCycleToken)
{
    if (!summary.valid || !summary.judgement_known) return false;
    if (slotResultToken == 0) return false;
    // 不再要求 token 与当前周期严格相等：
    // PLC 步骤切换很快时，compute 结果可能在 token 变化后被立即隐藏。
    // 新周期清理仍会把 token 清零，因此不会长期残留旧结果。
    Q_UNUSED(currentCycleToken);
    return runtimeState != SlotRuntimeState::Empty;
}

} // namespace production_widget_logic
