#include "production_widget_logic.hpp"

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

QString runtimeStateText(SlotRuntimeState state)
{
    switch (state) {
    case SlotRuntimeState::Empty: return QStringLiteral("空");
    case SlotRuntimeState::Loaded: return QStringLiteral("已上料");
    case SlotRuntimeState::WaitingIdCheck: return QStringLiteral("待核对ID");
    case SlotRuntimeState::ScanMismatch: return QStringLiteral("ID不一致");
    case SlotRuntimeState::Measuring: return QStringLiteral("测量中");
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
    // 仅在“扫码完成/等待PC核对ID”步骤允许人工修正并写回
    return (stepState == 11);
}

QString stepText(quint16 step)
{
    switch (step) {
    case 1: return QStringLiteral("数据清零");
    case 2:
    case 3: return QStringLiteral("初始化中");
    case 9: return QStringLiteral("初始化完成");
    case 0: return QStringLiteral("待机");
    case 10: return QStringLiteral("扫码中");
    case 11: return QStringLiteral("等待PC核对ID");
    case 12: return QStringLiteral("判断开始抓料位置");
    case 20: return QStringLiteral("PLC扫码中");
    case 30: return QStringLiteral("等待PC核对ID");
    case 40: return QStringLiteral("抓取工件");
    case 50: return QStringLiteral("移载到工位");
    case 60: return QStringLiteral("夹紧/准备");
    case 70: return QStringLiteral("测量中");
    case 80: return QStringLiteral("整理测量包");
    case 90: return QStringLiteral("等待PC读取");
    case 100:
    case 101: return QStringLiteral("从料架抓料");
    case 110: return QStringLiteral("测量工位上料");
    case 120:
    case 130: return QStringLiteral("测量中");
    case 131: return QStringLiteral("等待测量工位完成");
    case 140:
    case 150: return QStringLiteral("物料交换工位测量");
    case 160:
    case 170: return QStringLiteral("交换后再次上料");
    case 180:
    case 190: return QStringLiteral("交换后测量中");
    case 200:
    case 210: return QStringLiteral("取料中");
    case 220: return QStringLiteral("测量完成数据归档");
    case 230: return QStringLiteral("下料回料架");
    case 900: return QStringLiteral("报警");
    case 910: return QStringLiteral("急停");
    default: return QStringLiteral("运行(%1)").arg(step);
    }
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

} // namespace production_widget_logic
