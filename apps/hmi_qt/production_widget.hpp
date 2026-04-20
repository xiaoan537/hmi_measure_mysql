#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <QVariantMap>
#include <QTimer>

#include "core/config.hpp"
#include "core/measurement_pipeline.hpp"
#include "production_widget_model.hpp"

namespace Ui { class ProductionWidget; }

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
    void setScanDone(quint16 scan_done);
    void applyPlcRuntimeSnapshot(quint16 step_state,
                                 quint16 scan_done,
                                 quint16 tray_present_mask,
                                 quint16 active_slot_mask,
                                 bool calibration_mode,
                                 quint16 mailbox_ready);

    // v2 推荐入口：Production 页默认只展示实时态 + 上位机计算结果
    void setTrayPresentMask(quint16 present_mask);
    void setScannedPartIds(const QVector<QString> &part_ids);
    void markSlotScanMismatch(int slot, const QString &note = {});
    void setSlotComputedResult(int slot, const SlotMeasureSummary &s);
    void setSlotSummary(int slot, const core::ProductionSlotSummary &s);
    void setSlotSummaries(const QVector<core::ProductionSlotSummary> &summaries);
    void clearCurrentBatch();
    void setReservedCalibrationSlot(int slot = 15);
    void setCalibrationMode(bool enabled);

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

    QString selectedPartTypeText() const;
    quint32 selectedPartTypeArg() const;
    int selectedPlcModeValue() const;
    void appendPlcLogMessage(const QString &text);
    void setCurrentPlcMode(int mode);

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);

    void requestWriteSlotId(int slot_index, const QString &part_id); // 实际语义：PC->PLC 写回单槽位工件ID
    void requestReloadSlotIds();                                // 实际语义：PLC->PC 读取扫码工件ID

    void requestReadMailbox();
    void requestAckMailbox();
    void requestReconnectPlc();
    void requestSetPlcMode(int mode);
    void requestWriteCategoryMode(int partTypeArg);
    void requestContinueAfterIdCheck();

private slots:
    void onBtnWriteSlotIds();
    void onBtnReloadSlotIds();
    void onBtnReadMailbox();
    void onBtnAckMailbox();

private:
    void initSlotCards();
    void selectSlot(int slot);
    void refreshSelectedDetail();
    void updateSlotEditability();
    void updateDecisionButtonsVisibility();
    void updateSlotCard(int slot);
    QString stepText(quint16 step) const;
    QString measureModeText() const;
    quint32 measureModeCommandArg() const;
    QString selectedPartTypeTextInternal() const;
    QString runtimeStateText(int slot) const;
    int runtimeStateStyleCode(int slot) const;
    bool isPartIdEditableStep() const;
    bool shouldShowComputedResult(int slot) const;
    void clearComputedCacheForNewCycle();
    void clearSlotRuntimeData(int slot);
    void setSlotRuntimeState(int slot, SlotRuntimeState state, const QString &note = {});
    void setActionSlotMask(quint16 action_slot_mask);

private:
    core::AppConfig cfg_;
    Ui::ProductionWidget *ui_ = nullptr;

    quint16 step_state_ = 0;
    quint16 scan_done_ = 0;
    quint16 mailbox_ready_ = 0;
    quint16 tray_present_ = 0;
    bool plc_connected_ = false;
    bool calibration_mode_ = false;
    int reserved_cal_slot_ = 15;

    QVector<QString> slot_part_ids_;       // 当前槽位工件ID（来自 PLC 扫码或人工修正）
    QVector<SlotRuntimeState> slot_states_;
    QVector<QString> slot_notes_;          // mismatch / fail / 提示
    QVector<SlotMeasureSummary> slot_meas_;
    QVector<quint32> slot_result_tokens_;
    quint32 cycle_token_ = 1;
    int selected_slot_ = 0;
    quint16 action_slot_mask_ = 0;
    QTimer blink_timer_;
    bool blink_on_ = false;

    // mailbox preview cache（仅用于展示）
    QString last_machine_state_text_;
    QChar mb_part_type_ = QChar('A');
    quint16 mb_slot0_ = 0;
    quint16 mb_slot1_ = 0xFFFF;
    QString mb_part_id0_;
    QString mb_part_id1_;

    class QLabel *lbRuntimeConn_ = nullptr;
    class QLabel *lbRuntimeMachine_ = nullptr;
    class QLabel *lbRuntimeStep_ = nullptr;
    class QLabel *lbRuntimeMode_ = nullptr;
    class QComboBox *measureModeCombo_ = nullptr;
    class QComboBox *partTypeCombo_ = nullptr;
    class QComboBox *plcModeCombo_ = nullptr;
    QString batch_part_type_ = QStringLiteral("A");
};
