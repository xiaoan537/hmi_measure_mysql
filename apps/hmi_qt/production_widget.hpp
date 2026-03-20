#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <QVariantMap>

#include "core/config.hpp"
#include "core/measurement_pipeline.hpp"

namespace Ui { class ProductionWidget; }

enum class ProductionMeasureMode : int
{
    Normal = 0,
    Second = 1,
    Third = 2,
    Mil = 3
};

enum class SlotRuntimeState : int
{
    Empty = 0,
    Loaded,
    WaitingIdCheck,
    ScanMismatch,
    Measuring,
    Ok,
    Ng,
    Calibration,
    Unknown
};

struct SlotMeasureSummary
{
    QChar part_type = QChar('A');   // 'A' or 'B'
    bool valid = false;             // 是否已有计算结果
    bool judgement_known = false;   // 上位机是否已得出 OK/NG
    bool judgement_ok = false;      // true=OK false=NG
    QString fail_reason_text;       // 由上位机算法/业务层给出

    // A 型
    float a_total_len_mm = qQNaN();
    float a_id_left_mm  = qQNaN();
    float a_od_left_mm  = qQNaN();
    float a_id_right_mm = qQNaN();
    float a_od_right_mm = qQNaN();

    // B 型
    float b_ad_len_mm      = qQNaN();
    float b_bc_len_mm      = qQNaN();
    float b_runout_left_mm = qQNaN();
    float b_runout_right_mm= qQNaN();
};

class ProductionWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ProductionWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);
    ~ProductionWidget();

    // ---- UI update APIs (PLC 接入后由 plc_poll/model 调用) ----
    void setPlcConnected(bool ok);
    void setMachineState(quint16 machine_state, const QString &text = {});
    void setStepState(quint16 step_state);
    void setStateSeq(quint32 state_seq);
    void setAlarm(quint16 alarm_code, quint16 alarm_level);
    void setMeasureDone(bool done);
    void setInterlockMask(quint32 mask);

    // v2 推荐入口：Production 页默认只展示实时态 + 上位机计算结果
    void setTrayPresentMask(quint16 present_mask);
    void setScannedPartIds(const QVector<QString> &part_ids);
    void setSlotRuntimeState(int slot, SlotRuntimeState state, const QString &note = {});
    void setSlotComputedResult(int slot, const SlotMeasureSummary &s);
    void setSlotSummary(int slot, const core::ProductionSlotSummary &s);
    void setSlotSummaries(const QVector<core::ProductionSlotSummary> &summaries);
    void clearCurrentBatch();
    void setReservedCalibrationSlot(int slot = 15);
    void setCalibrationMode(bool enabled);

    // ---- 兼容旧接口：内部已改为桥接到 v2 语义 ----
    void setTrayMasks(quint16 present_mask, quint16 ok_mask, quint16 ng_mask);
    void setSlotIds(const QVector<QString> &slot_ids);
    void setSlotMeasureSummary(int slot, const SlotMeasureSummary &s);

    // Mailbox header preview（生产页只展示“选中槽位”概要；详细请到诊断页）
    void setMailboxPreview(quint32 meas_seq,
                           QChar part_type,
                           quint16 slot0, quint16 slot1,
                           const QString &part_id0,
                           const QString &part_id1,
                           bool ok0,
                           bool ok1,
                           quint16 fail0,
                           quint16 fail1,
                           float total_len0_mm,
                           float total_len1_mm);

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);

    void requestWriteSlotIds(const QVector<QString> &slot_ids); // 实际语义：PC->PLC 写回工件ID
    void requestReloadSlotIds();                                // 实际语义：PLC->PC 读取扫码工件ID

    void requestReadMailbox();
    void requestAckMailbox();

private slots:
    void onBtnWriteSlotIds();
    void onBtnReloadSlotIds();
    void onBtnReadMailbox();
    void onBtnAckMailbox();
    void onBtnDevDemo();

private:
    void initSlotCards();
    void selectSlot(int slot);
    void refreshSelectedDetail();
    void updateSlotEditability();
    void updateSlotCard(int slot);
    QString stepText(quint16 step) const;
    QString measureModeText() const;
    quint32 measureModeCommandArg() const;
    QString runtimeStateText(int slot) const;
    int runtimeStateStyleCode(int slot) const;
    bool isPartIdEditableStep() const;
    void clearSlotRuntimeData(int slot);

private:
    core::AppConfig cfg_;
    Ui::ProductionWidget *ui_ = nullptr;

    quint16 step_state_ = 0;
    quint16 tray_present_ = 0;
    bool plc_connected_ = false;
    bool calibration_mode_ = false;
    int reserved_cal_slot_ = 15;

    QVector<QString> slot_part_ids_;       // 当前槽位工件ID（来自 PLC 扫码或人工修正）
    QVector<SlotRuntimeState> slot_states_;
    QVector<QString> slot_notes_;          // mismatch / fail / 提示
    QVector<SlotMeasureSummary> slot_meas_;
    int selected_slot_ = 0;

    // mailbox preview cache（仅用于展示）
    quint32 mb_meas_seq_ = 0;
    QChar mb_part_type_ = QChar('A');
    quint16 mb_slot0_ = 0;
    quint16 mb_slot1_ = 0xFFFF;
    QString mb_part_id0_;
    QString mb_part_id1_;

    class QComboBox *measureModeCombo_ = nullptr;
};
