#include "calibration_widget_logic.hpp"
#include "plc_step_rules_v26.hpp"

namespace calibration_widget_logic {

QString plcConnStyle(bool connected)
{
    return connected
               ? QStringLiteral("background:#22c55e;color:white;border-radius:10px;padding:4px 10px;font-weight:600;")
               : QStringLiteral("background:#9ca3af;color:white;border-radius:10px;padding:4px 10px;font-weight:600;");
}

QString stepText(quint16 step)
{
    return plc_step_rules_v26::calibrationStepText(step);
}

QString slot15StateText(quint16 trayPresentMask)
{
    const bool loaded = ((trayPresentMask >> 15) & 0x1) != 0;
    return loaded ? QStringLiteral("16 号槽位: 有料") : QStringLiteral("16 号槽位: 空");
}

QString selectedMasterTypeText(bool bChecked)
{
    return bChecked ? QStringLiteral("B") : QStringLiteral("A");
}

quint32 selectedMasterTypeArg(bool bChecked)
{
    return bChecked ? 1u : 2u;
}

QString buildSummaryText(const core::CalibrationSlotSummary &summary)
{
    const QString who = summary.calibration_master_part_id.isEmpty()
                            ? QStringLiteral("未配置标定件")
                            : summary.calibration_master_part_id;
    const QString measured = summary.measured_part_id.isEmpty() ? QStringLiteral("--")
                                                                 : summary.measured_part_id;
    QString text = QStringLiteral("结果摘要: 类型=%1, 主数据=%2, MailboxID=%3")
                       .arg(summary.calibration_type.isEmpty() ? QStringLiteral("--") : summary.calibration_type)
                       .arg(who)
                       .arg(measured);
    if (summary.valid) {
        if (summary.compute.judgement == core::MeasurementJudgement::Ok) {
            text += QStringLiteral("，判定=OK");
        } else if (summary.compute.judgement == core::MeasurementJudgement::Ng) {
            text += QStringLiteral("，判定=NG");
        } else {
            text += QStringLiteral("，判定=待算法");
        }
        if (!summary.fail_reason_text.isEmpty()) {
            text += QStringLiteral("，原因=%1").arg(summary.fail_reason_text);
        }
    }
    return text;
}

} // namespace calibration_widget_logic
