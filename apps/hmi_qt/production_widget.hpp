#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QtGlobal>
#include <QVariantMap>

#include "core/config.hpp"

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
    void setTrayMasks(quint16 present_mask, quint16 ok_mask, quint16 ng_mask);
    void setInterlockMask(quint32 mask);

    // slot_id_ascii[16][32]（QString 已去掉尾部 \0）
    void setSlotIds(const QVector<QString> &slot_ids);

    // Mailbox header preview（仅用于 UI 展示；实际 RAW/DB 由后端完成）
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
    // 后续接入 Command Block 时由 MainWindow 连接到 plc_command_writer
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);

    // SlotID 读写（后续接入 SlotID Block）
    void requestWriteSlotIds(const QVector<QString> &slot_ids); // PC->PLC
    void requestReloadSlotIds();                                // PLC->PC

    // Mailbox 读 & ACK（后续接入 Mailbox Block）
    void requestReadMailbox();
    void requestAckMailbox();

private slots:
    void onBtnWriteSlotIds();
    void onBtnReloadSlotIds();
    void onBtnReadMailbox();
    void onBtnAckMailbox();

    // 仅用于 UI 演示
    void onBtnDevDemo();

private:
    void initSlotTable();
    void updateSlotEditability();
    void updateTrayRow(int slot, bool present, int result_code); // result_code: 0=unknown,1=ok,2=ng
    QString stepText(quint16 step) const;

private:
    core::AppConfig cfg_;
    Ui::ProductionWidget *ui_ = nullptr;

    quint16 step_state_ = 0;
    quint16 tray_present_ = 0;
    quint16 tray_ok_ = 0;
    quint16 tray_ng_ = 0;
    bool plc_connected_ = false;
};
