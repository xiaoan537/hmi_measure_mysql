#include "production_widget.hpp"
#include "production_widget_logic.hpp"
#include "ui_production_widget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QFont>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QUrl>

ProductionWidget::ProductionWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), cfg_(cfg), ui_(new Ui::ProductionWidget)
{
    ui_->setupUi(this);

    // 顶部大状态字
    QFont f = ui_->lblStepBig->font();
    f.setPointSize(20);
    f.setBold(true);
    ui_->lblStepBig->setFont(f);

    QFont t = ui_->lblSelectedTitle->font();
    t.setPointSize(12);
    t.setBold(true);
    ui_->lblSelectedTitle->setFont(t);

    // 用代码把文案切到当前项目真实语义
    ui_->lblSelectedTitle->setText(QStringLiteral("槽位详情 / 当前批次"));
    ui_->groupRun->setTitle(QStringLiteral("生产流程 / 模式"));
    ui_->lblSlotIdCaption->setText(QStringLiteral("工件ID(32)"));
    ui_->editSlotId->setPlaceholderText(QStringLiteral("仅待机/等待上料/等待ID核对阶段可编辑"));
    ui_->lblPart0Caption->setText(QStringLiteral("当前工件ID"));
    ui_->lblPart1Caption->setText(QStringLiteral("测量包预览"));
    ui_->groupSlotOps->setTitle(QStringLiteral("工件ID修正"));
    ui_->btnWriteSlotIds->setText(QStringLiteral("写回工件ID"));

    // 连接状态灯（用样式表）
    setStyleSheet(R"(
        QLabel#lblConnPlc, QLabel#lblConnDb, QLabel#lblConnMes {
            border-radius: 10px;
            padding: 4px 10px;
            color: white;
            font-weight: 600;
        }
        QLabel[connState="0"] { background: #9ca3af; }
        QLabel[connState="1"] { background: #22c55e; }

        QPushButton[slotState="0"] { background: #f3f4f6; border: 1px solid #e5e7eb; border-radius: 10px; }
        QPushButton[slotState="1"] { background: #dcfce7; border: 1px solid #86efac; border-radius: 10px; }
        QPushButton[slotState="2"] { background: #fee2e2; border: 1px solid #fca5a5; border-radius: 10px; }
        QPushButton[slotState="3"] { background: #e0f2fe; border: 1px solid #7dd3fc; border-radius: 10px; }
        QPushButton[slotState="4"] { background: #fef3c7; border: 1px solid #fcd34d; border-radius: 10px; }
        QPushButton[slotState="5"] { background: #ffedd5; border: 1px solid #fdba74; border-radius: 10px; }

        QPushButton[slotSelected="1"] { border: 2px solid #111827; }
    )");

    ui_->lblConnPlc->setProperty("connState", 0);
    ui_->lblConnDb->setProperty("connState", 0);
    ui_->lblConnMes->setProperty("connState", 0);

    ui_->groupRun->setTitle(QStringLiteral("生产控制 / 自动流程"));
    ui_->groupSlotOps->setTitle(QStringLiteral("工件ID修正"));
    ui_->groupDataOps->hide();
    ui_->btnDevDemo->hide();
    ui_->btnOpenLastRaw->setMinimumHeight(32);
    ui_->btnWriteSlotIds->setMinimumHeight(32);
    if (ui_->gridRunCmd) {
        ui_->gridRunCmd->setColumnStretch(0, 1);
        ui_->gridRunCmd->setColumnStretch(1, 1);
        ui_->gridRunCmd->setColumnStretch(2, 1);
        ui_->gridRunCmd->setColumnStretch(3, 1);
    }
    lbRuntimeMachine_ = ui_->lblRuntimeMachine;
    measureModeCombo_ = ui_->comboMeasureMode;
    partTypeCombo_ = ui_->comboPartType;
    plcModeCombo_ = ui_->comboPlcMode;

    // 生产业务模式：普通 / 第二次 / 第三次 / 军检（仅 PC 侧业务语义）
    measureModeCombo_->clear();
    measureModeCombo_->addItem(QStringLiteral("普通测量"), static_cast<int>(ProductionMeasureMode::Normal));
    measureModeCombo_->addItem(QStringLiteral("第二次测量"), static_cast<int>(ProductionMeasureMode::Second));
    measureModeCombo_->addItem(QStringLiteral("第三次测量"), static_cast<int>(ProductionMeasureMode::Third));
    measureModeCombo_->addItem(QStringLiteral("军检"), static_cast<int>(ProductionMeasureMode::Mil));

    partTypeCombo_->clear();
    partTypeCombo_->addItem(QStringLiteral("B型"), QStringLiteral("B"));
    partTypeCombo_->addItem(QStringLiteral("A型"), QStringLiteral("A"));

    plcModeCombo_->clear();
    plcModeCombo_->addItem(QStringLiteral("手动"), 1);
    plcModeCombo_->addItem(QStringLiteral("自动"), 2);
    plcModeCombo_->addItem(QStringLiteral("单步"), 3);

    // 默认选择：自动模式 + A型
    const int autoIndex = plcModeCombo_->findData(2);
    if (autoIndex >= 0) plcModeCombo_->setCurrentIndex(autoIndex);
    const int partAIndex = partTypeCombo_->findData(QStringLiteral("A"));
    if (partAIndex >= 0) partTypeCombo_->setCurrentIndex(partAIndex);

    const QString comboStyle = QStringLiteral("QComboBox{min-height:30px;padding:2px 8px;} QComboBox::drop-down{width:24px;}");
    measureModeCombo_->setStyleSheet(comboStyle);
    partTypeCombo_->setStyleSheet(comboStyle);
    plcModeCombo_->setStyleSheet(comboStyle);
    measureModeCombo_->setMinimumWidth(120);
    partTypeCombo_->setMinimumWidth(96);
    plcModeCombo_->setMinimumWidth(96);

    connect(ui_->btnReconnectPlc, &QPushButton::clicked, this, [this]{ emit requestReconnectPlc(); });
    connect(plcModeCombo_, qOverload<int>(&QComboBox::activated), this, [this](int){ emit requestSetPlcMode(selectedPlcModeValue()); });
    connect(partTypeCombo_, qOverload<int>(&QComboBox::activated), this, [this](int){ emit requestWriteCategoryMode(static_cast<int>(selectedPartTypeArg())); });
    connect(ui_->btnInit, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("INITIALIZE"), args); });
    connect(ui_->btnStartMeasure, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("measure_mode"), measureModeText()); args.insert(QStringLiteral("part_type"), selectedPartTypeText()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_AUTO"), args); ui_->listMessages->addItem(QStringLiteral("开始生产测量：类型=%1，业务=%2").arg(selectedPartTypeText(), measureModeText())); });
    connect(ui_->btnStartCal, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_CALIBRATION"), args); });
    connect(ui_->btnStop2, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("STOP"), args); });
    connect(ui_->btnResetAlarm, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("RESET_ALARM"), args); });
    connect(ui_->btnAlarmMute, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("ALARM_MUTE"), args); });
    connect(ui_->btnRetest, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_RETEST_CURRENT"), args); });
    connect(ui_->btnContinueNoRetest, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), args); });
    connect(ui_->btnComputeResult, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("part_type"), selectedPartTypeText()); emit uiCommandRequested(QStringLiteral("COMPUTE_RESULT"), args); ui_->listMessages->addItem(QStringLiteral("已请求计算当前测量包结果")); });
    connect(ui_->btnReadIds, &QPushButton::clicked, this, &ProductionWidget::requestReloadSlotIds);
    connect(ui_->btnContinue, &QPushButton::clicked, this, &ProductionWidget::requestContinueAfterIdCheck);
    connect(ui_->btnReadMb, &QPushButton::clicked, this, &ProductionWidget::requestReadMailbox);
    connect(ui_->btnAck, &QPushButton::clicked, this, &ProductionWidget::requestAckMailbox);

    connect(ui_->btnWriteSlotIds, &QPushButton::clicked, this, &ProductionWidget::onBtnWriteSlotIds);
    connect(ui_->btnReloadSlotIds, &QPushButton::clicked, this, &ProductionWidget::onBtnReloadSlotIds);
    connect(ui_->btnReadMailbox, &QPushButton::clicked, this, &ProductionWidget::onBtnReadMailbox);
    connect(ui_->btnAckMailbox, &QPushButton::clicked, this, &ProductionWidget::onBtnAckMailbox);
    connect(ui_->btnDevDemo, &QPushButton::clicked, this, &ProductionWidget::onBtnDevDemo);

    connect(ui_->btnOpenLastRaw, &QPushButton::clicked, this, [this]{
        QDir dir(cfg_.paths.raw_dir);
        if (!dir.exists()) return;
        dir.setNameFilters(QStringList() << "*.raw2" << "*.bin" << "*.dat" << "*.raw");
        dir.setFilter(QDir::Files);
        dir.setSorting(QDir::Time);
        const QFileInfoList files = dir.entryInfoList();
        if (files.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(files.first().absoluteFilePath()));
    });

    connect(ui_->editSlotId, &QLineEdit::editingFinished, this, [this]{
        if (selected_slot_ < 0 || selected_slot_ >= 16) return;
        if (slot_part_ids_.size() != 16) slot_part_ids_ = QVector<QString>(16);
        slot_part_ids_[selected_slot_] = ui_->editSlotId->text().trimmed();
        if (((tray_present_ >> selected_slot_) & 0x1) != 0 && slot_states_[selected_slot_] == SlotRuntimeState::Empty) {
            slot_states_[selected_slot_] = SlotRuntimeState::Loaded;
        }
        updateSlotCard(selected_slot_);
    });

    initSlotCards();

    slot_part_ids_ = QVector<QString>(16);
    slot_states_ = QVector<SlotRuntimeState>(16, SlotRuntimeState::Empty);
    slot_notes_ = QVector<QString>(16);
    slot_meas_ = QVector<SlotMeasureSummary>(16);

    // init defaults
    setReservedCalibrationSlot(15);
    setPlcConnected(false);
    setStepState(0);
    setMachineState(0, "IDLE");
    setStateSeq(0);
    setAlarm(0, 0);
    setMeasureDone(false);
    setTrayPresentMask(0);
    setInterlockMask(0);
    setScannedPartIds(QVector<QString>(16));
    selectSlot(0);

    ui_->listMessages->addItem(QStringLiteral("提示：Production 页仅用于生产测量；标定流程已建议拆到独立 Calibration 页面。"));
    ui_->listMessages->addItem(QStringLiteral("提示：业务模式为 普通/第二次/第三次/军检；复测不在顶部选择，而在 NG 处理时触发。"));
}

ProductionWidget::~ProductionWidget()
{
    delete ui_;
}

void ProductionWidget::initSlotCards()
{
    for (int i = 0; i < 16; ++i) {
        auto *btn = new QPushButton(this);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btn->setMinimumSize(140, 90);
        btn->setProperty("slotState", 0);
        btn->setProperty("slotSelected", 0);
        btn->setText(QStringLiteral("槽位%1\n—\n空\n—").arg(i + 1));
        btn->setToolTip(QStringLiteral("点击查看槽位详情"));
        btn->setCursor(Qt::PointingHandCursor);

        connect(btn, &QPushButton::clicked, this, [this, i]{
            selectSlot(i);
        });

        ui_->gridSlots->addWidget(btn, i / 4, i % 4);
        btn->setObjectName(QString("btnSlot%1").arg(i));
    }
}

void ProductionWidget::selectSlot(int slot)
{
    if (slot < 0 || slot >= 16) return;
    selected_slot_ = slot;

    for (int i = 0; i < 16; ++i) {
        auto *btn = findChild<QPushButton*>(QString("btnSlot%1").arg(i));
        if (!btn) continue;
        btn->setProperty("slotSelected", i == selected_slot_ ? 1 : 0);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    refreshSelectedDetail();
}

QString ProductionWidget::runtimeStateText(int slot) const
{
    if (slot < 0 || slot >= slot_states_.size()) return QStringLiteral("—");
    return production_widget_logic::runtimeStateText(slot_states_[slot]);
}

int ProductionWidget::runtimeStateStyleCode(int slot) const
{
    if (slot < 0 || slot >= slot_states_.size()) return 0;
    return production_widget_logic::runtimeStateStyleCode(slot_states_[slot]);
}

bool ProductionWidget::isPartIdEditableStep() const
{
    return production_widget_logic::isPartIdEditableStep(step_state_);
}

void ProductionWidget::refreshSelectedDetail()
{
    ui_->lblSelectedSlot->setText(QString::number(selected_slot_ + 1));

    const QString id = (slot_part_ids_.size() == 16) ? slot_part_ids_[selected_slot_] : QString();
    ui_->editSlotId->setText(id);
    ui_->lblResult->setText(runtimeStateText(selected_slot_));

    ui_->lblPart0->setText(id.isEmpty() ? QStringLiteral("—") : id);
    ui_->lblPart1->setText(QStringLiteral("—"));
    ui_->lblFail->setText(QStringLiteral("—"));


    SlotMeasureSummary ms;
    if (slot_meas_.size() == 16) ms = slot_meas_[selected_slot_];

    const QString note = (slot_notes_.size() == 16) ? slot_notes_[selected_slot_] : QString();
    if (!ms.fail_reason_text.isEmpty()) ui_->lblFail->setText(ms.fail_reason_text);
    else if (!note.isEmpty()) ui_->lblFail->setText(note);

    if (!ms.valid) {
        ui_->stackMeasure->setCurrentIndex(0);
        ui_->lblA_Total->setText(QStringLiteral("--"));
        ui_->lblA_Left->setText(QStringLiteral("-- / --"));
        ui_->lblA_Right->setText(QStringLiteral("-- / --"));
        ui_->lblB_AD->setText(QStringLiteral("--"));
        ui_->lblB_BC->setText(QStringLiteral("--"));
        ui_->lblB_RunoutL->setText(QStringLiteral("--"));
        ui_->lblB_RunoutR->setText(QStringLiteral("--"));
    } else if (ms.part_type.toUpper() == QChar('B')) {
        ui_->stackMeasure->setCurrentIndex(1);
        ui_->lblB_AD->setText(production_widget_logic::formatFloat(ms.b_ad_len_mm));
        ui_->lblB_BC->setText(production_widget_logic::formatFloat(ms.b_bc_len_mm));
        ui_->lblB_RunoutL->setText(production_widget_logic::formatFloat(ms.b_runout_left_mm));
        ui_->lblB_RunoutR->setText(production_widget_logic::formatFloat(ms.b_runout_right_mm));
    } else {
        ui_->stackMeasure->setCurrentIndex(0);
        ui_->lblA_Total->setText(production_widget_logic::formatFloat(ms.a_total_len_mm));
        ui_->lblA_Left->setText(QStringLiteral("%1 / %2").arg(production_widget_logic::formatFloat(ms.a_id_left_mm)).arg(production_widget_logic::formatFloat(ms.a_od_left_mm)));
        ui_->lblA_Right->setText(QStringLiteral("%1 / %2").arg(production_widget_logic::formatFloat(ms.a_id_right_mm)).arg(production_widget_logic::formatFloat(ms.a_od_right_mm)));
    }

    updateSlotEditability();
}

void ProductionWidget::updateSlotEditability()
{
    const bool editable = isPartIdEditableStep();
    ui_->editSlotId->setEnabled(editable);
    ui_->btnWriteSlotIds->setEnabled(editable);
}

void ProductionWidget::updateSlotCard(int slot)
{
    auto *btn = findChild<QPushButton*>(QString("btnSlot%1").arg(slot));
    if (!btn) return;

    const QString id = (slot_part_ids_.size() == 16) ? slot_part_ids_[slot] : QString();
    const QString stateText = runtimeStateText(slot);
    const int slotState = runtimeStateStyleCode(slot);

    QString summary;
    if (slot_meas_.size() == 16) {
        const auto &ms = slot_meas_[slot];
        if (ms.valid) {
            if (ms.part_type.toUpper() == QChar('B')) {
                summary = QStringLiteral("AD=%1  BC=%2").arg(production_widget_logic::formatFloat(ms.b_ad_len_mm, 2)).arg(production_widget_logic::formatFloat(ms.b_bc_len_mm, 2));
            } else {
                summary = QStringLiteral("L=%1").arg(production_widget_logic::formatFloat(ms.a_total_len_mm, 2));
            }
        }
    }
    if (summary.isEmpty() && slot_notes_.size() == 16 && !slot_notes_[slot].isEmpty()) {
        summary = slot_notes_[slot];
    }
    if (summary.isEmpty()) summary = QStringLiteral("—");

    QString title = QStringLiteral("槽位%1").arg(slot + 1);
    if (slot == reserved_cal_slot_) {
        title += QStringLiteral("*");
    }

    btn->setText(QStringLiteral("%1\n%2\n%3\n%4")
                 .arg(title)
                 .arg(production_widget_logic::shortId(id))
                 .arg(stateText)
                 .arg(summary));
    btn->setProperty("slotState", slotState);

    btn->style()->unpolish(btn);
    btn->style()->polish(btn);

    if (slot == selected_slot_) refreshSelectedDetail();
}

QString ProductionWidget::stepText(quint16 step) const
{
    return production_widget_logic::stepText(step);
}


QString ProductionWidget::selectedPartTypeTextInternal() const
{
    if (!partTypeCombo_) return QStringLiteral("A");
    return production_widget_logic::partTypeTextFromData(partTypeCombo_->currentData().toString());
}

QString ProductionWidget::selectedPartTypeText() const
{
    return selectedPartTypeTextInternal();
}

quint32 ProductionWidget::selectedPartTypeArg() const
{
    return selectedPartTypeTextInternal() == QStringLiteral("B") ? 1u : 2u;
}

int ProductionWidget::selectedPlcModeValue() const
{
    return plcModeCombo_ ? plcModeCombo_->currentData().toInt() : 1;
}

void ProductionWidget::setCurrentPlcMode(int /*mode*/)
{
}

QString ProductionWidget::measureModeText() const
{
    if (!measureModeCombo_) return production_widget_logic::measureModeText(ProductionMeasureMode::Normal);
    const auto mode = static_cast<ProductionMeasureMode>(measureModeCombo_->currentData().toInt());
    return production_widget_logic::measureModeText(mode);
}

quint32 ProductionWidget::measureModeCommandArg() const
{
    if (!measureModeCombo_) return production_widget_logic::measureModeCommandArg(ProductionMeasureMode::Normal);
    const auto mode = static_cast<ProductionMeasureMode>(measureModeCombo_->currentData().toInt());
    return production_widget_logic::measureModeCommandArg(mode);
}

void ProductionWidget::appendPlcLogMessage(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        ui_->listMessages->addItem(text);
    }
}

void ProductionWidget::setPlcConnected(bool ok)
{
    plc_connected_ = ok;
    ui_->lblConnPlc->setProperty("connState", ok ? 1 : 0);
    ui_->lblConnPlc->style()->unpolish(ui_->lblConnPlc);
    ui_->lblConnPlc->style()->polish(ui_->lblConnPlc);
}

void ProductionWidget::setMachineState(quint16 machine_state, const QString &text)
{
    const QString display = machine_state != 0 ? production_widget_logic::machineStateMaskText(machine_state)
                                               : (text.isEmpty() ? QStringLiteral("-") : text);
    if (lbRuntimeMachine_) lbRuntimeMachine_->setText(QStringLiteral("设备主状态：%1").arg(display));
    if (display == last_machine_state_text_) return;
    last_machine_state_text_ = display;
    ui_->listMessages->addItem(QStringLiteral("机器状态：%1").arg(display));
}

void ProductionWidget::setStepState(quint16 step_state)
{
    step_state_ = step_state;
    const QString txt = stepText(step_state);
    ui_->lblStepBig->setText(txt);
    updateSlotEditability();
}

void ProductionWidget::setStateSeq(quint32 /*state_seq*/)
{
}

void ProductionWidget::setAlarm(quint16 alarm_code, quint16 alarm_level)
{
    if (alarm_code == 0) {
        ui_->lblAlarm->setText(QStringLiteral("无报警"));
    } else {
        ui_->lblAlarm->setText(QStringLiteral("报警：%1（等级 %2）").arg(alarm_code).arg(alarm_level));
    }
}

void ProductionWidget::setMeasureDone(bool done)
{
    if (done) {
        ui_->listMessages->addItem(QStringLiteral("PLC 已冻结原始测量包，可读取并在落盘后写入 pc_ack。"));
    }
}

void ProductionWidget::setInterlockMask(quint32 /*mask*/)
{
}

void ProductionWidget::clearSlotRuntimeData(int slot)
{
    if (slot < 0 || slot >= 16) return;
    slot_part_ids_[slot].clear();
    slot_notes_[slot].clear();
    slot_meas_[slot] = SlotMeasureSummary{};
}

void ProductionWidget::setTrayPresentMask(quint16 present_mask)
{
    tray_present_ = present_mask;

    for (int i = 0; i < 16; ++i) {
        const bool present = ((present_mask >> i) & 0x1) != 0;
        if (!present) {
            clearSlotRuntimeData(i);
            slot_states_[i] = SlotRuntimeState::Empty;
        } else if (i == reserved_cal_slot_ && calibration_mode_) {
            if (slot_states_[i] == SlotRuntimeState::Empty || slot_states_[i] == SlotRuntimeState::Unknown) {
                slot_states_[i] = SlotRuntimeState::Calibration;
            }
        } else if (slot_states_[i] == SlotRuntimeState::Empty || slot_states_[i] == SlotRuntimeState::Unknown) {
            slot_states_[i] = SlotRuntimeState::Loaded;
        }
        updateSlotCard(i);
    }
}

void ProductionWidget::setScannedPartIds(const QVector<QString> &part_ids)
{
    slot_part_ids_ = part_ids;
    if (slot_part_ids_.size() != 16) slot_part_ids_.resize(16);

    for (int i = 0; i < 16; ++i) {
        if (!slot_part_ids_[i].trimmed().isEmpty() && ((tray_present_ >> i) & 0x1) != 0) {
            if (slot_states_[i] == SlotRuntimeState::Empty || slot_states_[i] == SlotRuntimeState::Unknown) {
                slot_states_[i] = (i == reserved_cal_slot_ && calibration_mode_) ? SlotRuntimeState::Calibration : SlotRuntimeState::Loaded;
            }
        }
        updateSlotCard(i);
    }
}

void ProductionWidget::setSlotRuntimeState(int slot, SlotRuntimeState state, const QString &note)
{
    if (slot < 0 || slot >= 16) return;
    slot_states_[slot] = state;
    if (slot_notes_.size() != 16) slot_notes_ = QVector<QString>(16);
    slot_notes_[slot] = note;
    updateSlotCard(slot);
}

void ProductionWidget::setSlotComputedResult(int slot, const SlotMeasureSummary &s)
{
    if (slot < 0 || slot >= 16) return;
    slot_meas_[slot] = s;
    if (s.valid && s.judgement_known) {
        slot_states_[slot] = s.judgement_ok ? SlotRuntimeState::Ok : SlotRuntimeState::Ng;
        slot_notes_[slot] = s.fail_reason_text;
    }
    updateSlotCard(slot);
}

void ProductionWidget::setSlotSummary(int slot, const core::ProductionSlotSummary &s)
{
    if (slot < 0) slot = s.slot_index;
    if (slot < 0 || slot >= 16) return;

    if (!s.part_id.isEmpty()) {
        if (slot_part_ids_.size() != 16) slot_part_ids_.resize(16);
        slot_part_ids_[slot] = s.part_id;
    }
    setSlotComputedResult(slot, production_widget_logic::toWidgetSummary(s));
}

void ProductionWidget::setSlotSummaries(const QVector<core::ProductionSlotSummary> &summaries)
{
    for (const auto &s : summaries) {
        setSlotSummary(s.slot_index, s);
    }
}

void ProductionWidget::clearCurrentBatch()
{
    tray_present_ = 0;
    mb_slot0_ = 0;
    mb_slot1_ = 0xFFFF;
    mb_part_id0_.clear();
    mb_part_id1_.clear();
    slot_part_ids_ = QVector<QString>(16);
    slot_states_ = QVector<SlotRuntimeState>(16, SlotRuntimeState::Empty);
    slot_notes_ = QVector<QString>(16);
    slot_meas_ = QVector<SlotMeasureSummary>(16);
    for (int i = 0; i < 16; ++i) updateSlotCard(i);
}

void ProductionWidget::setReservedCalibrationSlot(int slot)
{
    if (slot < 0 || slot >= 16) slot = 15;
    reserved_cal_slot_ = slot;
    for (int i = 0; i < 16; ++i) updateSlotCard(i);
}

void ProductionWidget::setCalibrationMode(bool enabled)
{
    calibration_mode_ = enabled;
    for (int i = 0; i < 16; ++i) {
        if (i == reserved_cal_slot_ && ((tray_present_ >> i) & 0x1) != 0 && !slot_meas_[i].valid) {
            slot_states_[i] = enabled ? SlotRuntimeState::Calibration : SlotRuntimeState::Loaded;
        }
        updateSlotCard(i);
    }
}

void ProductionWidget::setTrayMasks(quint16 present_mask, quint16 /*ok_mask*/, quint16 /*ng_mask*/)
{
    // v2 起不再把 tray_ok/ng 作为 Production 判定来源。
    setTrayPresentMask(present_mask);
}

void ProductionWidget::setSlotIds(const QVector<QString> &slot_ids)
{
    // v2 起语义改为“槽位当前工件ID”。
    setScannedPartIds(slot_ids);
}

void ProductionWidget::setSlotMeasureSummary(int slot, const SlotMeasureSummary &s)
{
    setSlotComputedResult(slot, s);
}

void ProductionWidget::setMailboxPreview(quint32 meas_seq,
                                         QChar part_type,
                                         quint16 slot0, quint16 slot1,
                                         const QString &part_id0,
                                         const QString &part_id1,
                                         bool /*ok0*/,
                                         bool /*ok1*/,
                                         quint16 /*fail0*/,
                                         quint16 /*fail1*/,
                                         float /*total_len0_mm*/,
                                         float /*total_len1_mm*/)
{
    Q_UNUSED(meas_seq);
    mb_part_type_ = part_type;
    mb_slot0_ = slot0;
    mb_slot1_ = slot1;
    mb_part_id0_ = part_id0;
    mb_part_id1_ = part_id1;

    refreshSelectedDetail();
}

void ProductionWidget::onBtnWriteSlotIds()
{
    if (!isPartIdEditableStep()) {
        ui_->listMessages->addItem(QStringLiteral("当前工步不允许写回工件ID（仅待机/等待上料/等待ID核对阶段可写）"));
        return;
    }
    emit requestWriteSlotIds(slot_part_ids_);
    ui_->listMessages->addItem(QStringLiteral("已请求写回工件ID（PC->PLC，对应槽位寄存器）"));
}

void ProductionWidget::onBtnReloadSlotIds()
{
    emit requestReloadSlotIds();
    ui_->listMessages->addItem(QStringLiteral("已请求读取扫码工件ID（PLC->PC）"));
}

void ProductionWidget::onBtnReadMailbox()
{
    emit requestReadMailbox();
    ui_->listMessages->addItem(QStringLiteral("已请求读取原始测量包（Mailbox Raw）"));
}

void ProductionWidget::onBtnAckMailbox()
{
    emit requestAckMailbox();
    ui_->listMessages->addItem(QStringLiteral("已请求写入 pc_ack（RAW+DB 完成落盘后）"));
}

void ProductionWidget::onBtnDevDemo()
{
    static int tick = 0;
    tick++;

    setPlcConnected(true);
    ui_->lblConnDb->setProperty("connState", 1);
    ui_->lblConnDb->style()->unpolish(ui_->lblConnDb);
    ui_->lblConnDb->style()->polish(ui_->lblConnDb);

    ui_->lblConnMes->setProperty("connState", (tick % 2) ? 1 : 0);
    ui_->lblConnMes->style()->unpolish(ui_->lblConnMes);
    ui_->lblConnMes->style()->polish(ui_->lblConnMes);

    setCalibrationMode(false);
    setStepState((tick % 6 == 0) ? 30 : ((tick % 5) * 10));
    setAlarm((tick % 7 == 0) ? 123 : 0, (tick % 7 == 0) ? 2 : 0);

    quint16 present = 0;
    for (int i = 0; i < 15; ++i) {
        if ((tick + i) % 4 != 0) present |= (1u << i);
    }
    // slot15 预留给标定，自动流程默认不占用
    setTrayPresentMask(present);

    QVector<QString> ids(16);
    for (int i = 0; i < 15; ++i) {
        if (((present >> i) & 0x1) != 0) {
            ids[i] = QStringLiteral("PART_%1_%2").arg(QChar((i % 2 == 0) ? 'A' : 'B')).arg(1000 + i);
        }
    }
    setScannedPartIds(ids);

    for (int s = 0; s < 16; ++s) {
        if (((present >> s) & 0x1) == 0) continue;

        if ((s + tick) % 6 == 0) {
            setSlotRuntimeState(s, SlotRuntimeState::ScanMismatch, QStringLiteral("任务卡不匹配"));
            continue;
        }
        if ((s + tick) % 5 == 0) {
            setSlotRuntimeState(s, SlotRuntimeState::Measuring, QStringLiteral("等待测量完成"));
            continue;
        }

        SlotMeasureSummary ms;
        ms.valid = true;
        ms.judgement_known = true;
        ms.judgement_ok = ((s + tick) % 7 != 0);
        if (s % 2 == 0) {
            ms.part_type = QChar('A');
            ms.a_total_len_mm = 120.0f + s * 0.5f + (tick % 10) * 0.03f;
            ms.a_id_left_mm   = 10.0f + s * 0.01f;
            ms.a_od_left_mm   = 20.0f + s * 0.02f;
            ms.a_id_right_mm  = 10.1f + s * 0.01f;
            ms.a_od_right_mm  = 20.1f + s * 0.02f;
        } else {
            ms.part_type = QChar('B');
            ms.b_ad_len_mm       = 200.0f + s * 0.6f;
            ms.b_bc_len_mm       = 80.0f  + s * 0.2f;
            ms.b_runout_left_mm  = 0.03f + (s % 5) * 0.01f;
            ms.b_runout_right_mm = 0.04f + (s % 4) * 0.01f;
        }
        if (!ms.judgement_ok) ms.fail_reason_text = QStringLiteral("超差待人工确认");
        setSlotComputedResult(s, ms);
    }

    setMailboxPreview(100 + tick, QChar('A'), 2, 3,
                      QStringLiteral("PART_A_1002"),
                      QStringLiteral("PART_B_1003"),
                      false, false,
                      0, 0,
                      qQNaN(), qQNaN());
}
