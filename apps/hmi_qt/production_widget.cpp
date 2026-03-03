#include "production_widget.hpp"
#include "ui_production_widget.h"

#include <QHeaderView>
#include <QTableWidgetItem>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

ProductionWidget::ProductionWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), cfg_(cfg), ui_(new Ui::ProductionWidget)
{
    ui_->setupUi(this);

    // interlocks: 只读
    const QList<QCheckBox*> checks = {
        ui_->ckDoorOpen, ui_->ckEstop, ui_->ckAirLow, ui_->ckAxisNotHomed, ui_->ckServoFault,
        ui_->ckGripperFault, ui_->ckTraySensorFault, ui_->ckClampScanNotOk, ui_->ckClampLenNotOk,
        ui_->ckConfocalOffline, ui_->ckGt2Offline, ui_->ckSpindleFault, ui_->ckOddCount
    };
    for (auto *ck : checks) {
        ck->setEnabled(false);
        ck->setChecked(false);
    }

    // Mode buttons: 互斥
    auto *bg = new QButtonGroup(this);
    bg->setExclusive(true);
    bg->addButton(ui_->btnModeAuto);
    bg->addButton(ui_->btnModeManual);
    ui_->btnModeAuto->setChecked(true);

    // 将 Mode/Run 控制以“命令请求”形式抛给上层（后续接 PLC Command Block）
    connect(ui_->btnModeAuto, &QToolButton::clicked, this, [this]{
        emit uiCommandRequested("SET_MODE_AUTO", {});
    });
    connect(ui_->btnModeManual, &QToolButton::clicked, this, [this]{
        emit uiCommandRequested("SET_MODE_MANUAL", {});
    });
    connect(ui_->btnStart, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("START_AUTO", {});
    });
    connect(ui_->btnPause, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("PAUSE", {});
    });
    connect(ui_->btnResume, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("RESUME", {});
    });
    connect(ui_->btnStop, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("STOP", {});
    });
    connect(ui_->btnResetAlarm, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("RESET_ALARM", {});
    });
    connect(ui_->btnHomeAll, &QPushButton::clicked, this, [this]{
        emit uiCommandRequested("HOME_ALL", {});
    });

    // 右侧功能按钮（占位：后续接 PLC）
    connect(ui_->btnWriteSlotIds, &QPushButton::clicked, this, &ProductionWidget::onBtnWriteSlotIds);
    connect(ui_->btnReloadSlotIds, &QPushButton::clicked, this, &ProductionWidget::onBtnReloadSlotIds);
    connect(ui_->btnReadMailbox, &QPushButton::clicked, this, &ProductionWidget::onBtnReadMailbox);
    connect(ui_->btnAckMailbox, &QPushButton::clicked, this, &ProductionWidget::onBtnAckMailbox);
    connect(ui_->btnDevDemo, &QPushButton::clicked, this, &ProductionWidget::onBtnDevDemo);

    // 打开最近 RAW（如果后端把路径放到 meta_json，可以后续在这里扩展）
    connect(ui_->btnOpenLastRaw, &QPushButton::clicked, this, [this]{
        // 先按约定：raw_dir 下最新文件
        QDir dir(cfg_.paths.raw_dir);
        if (!dir.exists()) return;
        dir.setNameFilters(QStringList() << "*.raw2" << "*.bin" << "*.dat" << "*.raw");
        dir.setFilter(QDir::Files);
        dir.setSorting(QDir::Time);
        const QFileInfoList files = dir.entryInfoList();
        if (files.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(files.first().absoluteFilePath()));
    });

    initSlotTable();
    setPlcConnected(false);
    setStepState(0);
    setMachineState(0, "IDLE");
    setStateSeq(0);
    setAlarm(0, 0);
    setMeasureDone(false);
    setTrayMasks(0, 0, 0);
    setInterlockMask(0);
}

ProductionWidget::~ProductionWidget()
{
    delete ui_;
}

void ProductionWidget::initSlotTable()
{
    auto *t = ui_->tableSlots;
    t->setColumnCount(4);
    t->setRowCount(16);
    t->setHorizontalHeaderLabels({"Slot", "SlotID(32)", "Present", "Result"});
    t->horizontalHeader()->setStretchLastSection(true);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    t->verticalHeader()->setVisible(false);

    for (int i = 0; i < 16; ++i) {
        auto *itemSlot = new QTableWidgetItem(QString::number(i));
        itemSlot->setFlags(itemSlot->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 0, itemSlot);

        auto *itemId = new QTableWidgetItem("");
        // 是否可编辑由 step_state 控制（updateSlotEditability）
        t->setItem(i, 1, itemId);

        auto *itemPresent = new QTableWidgetItem("—");
        itemPresent->setFlags(itemPresent->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 2, itemPresent);

        auto *itemResult = new QTableWidgetItem("—");
        itemResult->setFlags(itemResult->flags() & ~Qt::ItemIsEditable);
        t->setItem(i, 3, itemResult);
    }
    updateSlotEditability();
}

QString ProductionWidget::stepText(quint16 step) const
{
    switch (step) {
    case 0: return "WAIT_START";
    case 10: return "WAIT_TRAY_READY";
    case 20: return "PICK_2_FROM_TRAY";
    case 30: return "MOVE_TO_STATIONS";
    case 40: return "PLACE_TO_STATIONS";
    case 50: return "MEASURE_PARALLEL_1";
    case 60: return "SWAP_POSITIONS";
    case 70: return "MEASURE_PARALLEL_2";
    case 80: return "GENERATE_RESULTS";
    case 90: return "WAIT_PC_READ";
    case 100: return "RETURN_TO_TRAY";
    case 110: return "CYCLE_COMPLETE";
    case 900: return "FAULT";
    case 910: return "E_STOP";
    default: return "UNKNOWN";
    }
}

void ProductionWidget::updateSlotEditability()
{
    const bool editable = (step_state_ == 0 || step_state_ == 10);
    ui_->btnWriteSlotIds->setEnabled(editable && plc_connected_);
    // table: 仅第 1 列可编辑
    for (int i = 0; i < 16; ++i) {
        auto *item = ui_->tableSlots->item(i, 1);
        if (!item) continue;
        Qt::ItemFlags f = item->flags();
        if (editable) f |= Qt::ItemIsEditable;
        else f &= ~Qt::ItemIsEditable;
        item->setFlags(f);
    }
    ui_->lblSlotEditHint->setText(editable
        ? "提示：当前允许修改 SlotID（step_state=0/10）。"
        : "提示：测量进行中，SlotID 禁止修改（仅 step_state=0/10 可写）。");
}

void ProductionWidget::updateTrayRow(int slot, bool present, int result_code)
{
    auto *t = ui_->tableSlots;
    if (slot < 0 || slot >= 16) return;

    if (auto *item = t->item(slot, 2)) {
        item->setText(present ? "1" : "0");
    }
    if (auto *item = t->item(slot, 3)) {
        if (result_code == 1) item->setText("OK");
        else if (result_code == 2) item->setText("NG");
        else item->setText("—");
    }
}

void ProductionWidget::setPlcConnected(bool ok)
{
    plc_connected_ = ok;
    ui_->lblPlcConn->setText(ok ? "CONNECTED" : "DISCONNECTED");
    updateSlotEditability();
}

void ProductionWidget::setMachineState(quint16 /*machine_state*/, const QString &text)
{
    ui_->lblMachineState->setText(text.isEmpty() ? "—" : text);
}

void ProductionWidget::setStepState(quint16 step_state)
{
    step_state_ = step_state;
    ui_->lblStepState->setText(QString::number(step_state_));
    ui_->lblStepText->setText(stepText(step_state_));
    updateSlotEditability();
}

void ProductionWidget::setStateSeq(quint32 state_seq)
{
    ui_->lblStateSeq->setText(QString::number(state_seq));
}

void ProductionWidget::setAlarm(quint16 alarm_code, quint16 alarm_level)
{
    ui_->lblAlarmCode->setText(QString::number(alarm_code));
    ui_->lblAlarmLevel->setText(QString::number(alarm_level));
}

void ProductionWidget::setMeasureDone(bool done)
{
    ui_->lblMeasureDone->setText(done ? "1" : "0");
}

void ProductionWidget::setTrayMasks(quint16 present_mask, quint16 ok_mask, quint16 ng_mask)
{
    tray_present_ = present_mask;
    tray_ok_ = ok_mask;
    tray_ng_ = ng_mask;

    ui_->lblTrayPresent->setText(QString("0x%1").arg(tray_present_, 4, 16, QLatin1Char('0')).toUpper());
    ui_->lblTrayOk->setText(QString("0x%1").arg(tray_ok_, 4, 16, QLatin1Char('0')).toUpper());
    ui_->lblTrayNg->setText(QString("0x%1").arg(tray_ng_, 4, 16, QLatin1Char('0')).toUpper());

    for (int i = 0; i < 16; ++i) {
        const bool present = (tray_present_ >> i) & 0x1;
        int result = 0;
        if ((tray_ok_ >> i) & 0x1) result = 1;
        else if ((tray_ng_ >> i) & 0x1) result = 2;
        updateTrayRow(i, present, result);
    }
}

void ProductionWidget::setInterlockMask(quint32 mask)
{
    ui_->ckDoorOpen->setChecked(mask & (1u << 0));
    ui_->ckEstop->setChecked(mask & (1u << 1));
    ui_->ckAirLow->setChecked(mask & (1u << 2));
    ui_->ckAxisNotHomed->setChecked(mask & (1u << 3));
    ui_->ckServoFault->setChecked(mask & (1u << 4));
    ui_->ckGripperFault->setChecked(mask & (1u << 5));
    ui_->ckTraySensorFault->setChecked(mask & (1u << 6));
    ui_->ckClampScanNotOk->setChecked(mask & (1u << 7));
    ui_->ckClampLenNotOk->setChecked(mask & (1u << 8));
    ui_->ckConfocalOffline->setChecked(mask & (1u << 9));
    ui_->ckGt2Offline->setChecked(mask & (1u << 10));
    ui_->ckSpindleFault->setChecked(mask & (1u << 11));
    ui_->ckOddCount->setChecked(mask & (1u << 12));
}

void ProductionWidget::setSlotIds(const QVector<QString> &slot_ids)
{
    auto *t = ui_->tableSlots;
    for (int i = 0; i < 16; ++i) {
        QString v;
        if (i < slot_ids.size()) v = slot_ids[i];
        if (auto *item = t->item(i, 1)) item->setText(v);
    }
}

void ProductionWidget::setMailboxPreview(quint32 meas_seq,
                                        QChar part_type,
                                        quint16 slot0, quint16 slot1,
                                        const QString &part_id0,
                                        const QString &part_id1,
                                        bool ok0, bool ok1,
                                        quint16 fail0, quint16 fail1,
                                        float total_len0_mm,
                                        float total_len1_mm)
{
    ui_->lblMeasSeq->setText(QString::number(meas_seq));
    ui_->lblPartType->setText(QString(part_type));
    ui_->lblSlot0->setText(QString::number(slot0));
    ui_->lblSlot1->setText(QString::number(slot1));
    ui_->lblPartId0->setText(part_id0);
    ui_->lblPartId1->setText(part_id1);
    ui_->lblOk0->setText(ok0 ? "1" : "0");
    ui_->lblOk1->setText(ok1 ? "1" : "0");
    ui_->lblFail0->setText(QString::number(fail0));
    ui_->lblFail1->setText(QString::number(fail1));
    ui_->lblLenTotal0->setText(QString::number(total_len0_mm, 'f', 3));
    ui_->lblLenTotal1->setText(QString::number(total_len1_mm, 'f', 3));
}

void ProductionWidget::onBtnWriteSlotIds()
{
    // 读取 tableSlots 第 1 列
    QVector<QString> ids;
    ids.reserve(16);
    for (int i = 0; i < 16; ++i) {
        auto *item = ui_->tableSlots->item(i, 1);
        ids.push_back(item ? item->text() : QString());
    }
    emit requestWriteSlotIds(ids);
}

void ProductionWidget::onBtnReloadSlotIds()
{
    emit requestReloadSlotIds();
}

void ProductionWidget::onBtnReadMailbox()
{
    emit requestReadMailbox();
}

void ProductionWidget::onBtnAckMailbox()
{
    emit requestAckMailbox();
}

void ProductionWidget::onBtnDevDemo()
{
    // 纯 UI 演示：模拟一个流程中间态 + 一次 mailbox
    setPlcConnected(true);
    setMachineState(1, "AUTO");
    setStateSeq(123);
    setStepState(50);
    setInterlockMask(0);
    setAlarm(0, 0);
    setMeasureDone(true);

    // 随机槽位状态
    setTrayMasks(0b0000000000011111, 0b0000000000001010, 0b0000000000010100);
    QVector<QString> ids(16);
    for (int i = 0; i < 16; ++i) ids[i] = QString("SLOT%1").arg(i, 2, 10, QLatin1Char('0'));
    setSlotIds(ids);

    setMailboxPreview(4567, 'A', 2, 3,
                      "A-240301-0002",
                      "A-240301-0003",
                      true, false,
                      0, 12,
                      123.456f, 123.789f);
}
