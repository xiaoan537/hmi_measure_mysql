#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <QVariantMap>

#include "core/config.hpp"

namespace Ui { class ProductionWidget; }


struct SlotMeasureSummary
{
    QChar part_type = QChar('A');   // 'A' or 'B'
    bool valid = false;             // 是否已有计算结果

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
    void setTrayMasks(quint16 present_mask, quint16 ok_mask, quint16 ng_mask);
    void setInterlockMask(quint32 mask);

    // slot_id_ascii[16][32]（QString 已去掉尾部 \0）
    void setSlotIds(const QVector<QString> &slot_ids);

    // Mailbox header preview（生产页只展示“选中槽位”概要；详细请到诊断页）
    void setSlotMeasureSummary(int slot, const SlotMeasureSummary &s);

    void setMailboxPreview(quint32 meas_seq,
                           QChar part_type,
                           quint16 slot0, quint16 slot1,
                           const QString &part_id0,
                           const QString &part_id1,
                           bool ok0, bool ok1,
                           quint16 fail0, quint16 fail1,
                           float total_len0_mm,
                           float total_len1_mm);

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);

    void requestWriteSlotIds(const QVector<QString> &slot_ids); // PC->PLC
    void requestReloadSlotIds();                                // PLC->PC

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
    void updateSlotCard(int slot); // 使用 tray masks + slot ids 更新卡片文本/样式
    QString stepText(quint16 step) const;

private:
    core::AppConfig cfg_;
    Ui::ProductionWidget *ui_ = nullptr;

    quint16 step_state_ = 0;
    quint16 tray_present_ = 0;
    quint16 tray_ok_ = 0;
    quint16 tray_ng_ = 0;
    bool plc_connected_ = false;

    QVector<QString> slot_ids_;
    QVector<SlotMeasureSummary> slot_meas_;
    int selected_slot_ = 0;

    // mailbox preview cache（仅用于展示）
    quint32 mb_meas_seq_ = 0;
    QChar mb_part_type_ = QChar('A');
    quint16 mb_slot0_ = 0;
    quint16 mb_slot1_ = 1;
    QString mb_part_id0_;
    QString mb_part_id1_;
    bool mb_ok0_ = false;
    bool mb_ok1_ = false;
    quint16 mb_fail0_ = 0;
    quint16 mb_fail1_ = 0;
};
