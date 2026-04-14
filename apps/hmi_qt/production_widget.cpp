#include "production_widget.hpp"
#include "ui_production_widget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QFont>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QUrl>

namespace {

inline QString formatFloat(float v, int prec = 3)
{
    if (qIsNaN(v) || qIsInf(v)) return QStringLiteral("--");
    return QString::number(v, 'f', prec);
}

inline QString shortId(const QString &id32)
{
    if (id32.isEmpty()) return QStringLiteral("—");
    if (id32.size() <= 10) return id32;
    return id32.left(6) + QStringLiteral("…") + id32.right(3);
}

inline QString machineStateMaskText(quint16 mask)
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

} // namespace

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
    ui_->btnModeAuto->hide();
    ui_->btnModeManual->hide();
    ui_->btnHomeAll->hide();
    ui_->btnStart->hide();
    ui_->btnPause->hide();
    ui_->btnResume->hide();
    ui_->btnStop->hide();

    auto *btnReconnectPlc = new QPushButton(QStringLiteral("重连PLC"), this);
    btnReconnectPlc->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnReconnectPlc->setMinimumHeight(30);
    if (auto *hlConn = ui_->frameTop->findChild<QHBoxLayout*>(QStringLiteral("hlConn"))) {
        hlConn->addWidget(btnReconnectPlc);
    }
    connect(btnReconnectPlc, &QPushButton::clicked, this, [this]{ emit requestReconnectPlc(); });

    auto makeTopTag = [this](const QString &text) {
        auto *lb = new QLabel(text, this);
        lb->setStyleSheet(QStringLiteral("QLabel{background:#f3f4f6;border:1px solid #e5e7eb;border-radius:8px;padding:6px 10px;font-weight:600;}"));
        return lb;
    };
    auto *statusTopFrame = new QFrame(this);
    auto *statusTopLay = new QHBoxLayout(statusTopFrame);
    statusTopLay->setContentsMargins(0, 0, 0, 0);
    statusTopLay->setSpacing(8);
    lbRuntimeMachine_ = makeTopTag(QStringLiteral("设备主状态：-"));
    lbRuntimeMachine_->setMinimumWidth(180);
    statusTopLay->addWidget(lbRuntimeMachine_);
    statusTopFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (auto *topLayout = qobject_cast<QHBoxLayout*>(ui_->frameTop->layout())) {
        topLayout->insertWidget(1, statusTopFrame, 0, Qt::AlignVCenter);
    }

    // 生产业务模式：普通 / 第二次 / 第三次 / 军检（仅 PC 侧业务语义）
    measureModeCombo_ = new QComboBox(this);
    measureModeCombo_->addItem(QStringLiteral("普通测量"), static_cast<int>(ProductionMeasureMode::Normal));
    measureModeCombo_->addItem(QStringLiteral("第二次测量"), static_cast<int>(ProductionMeasureMode::Second));
    measureModeCombo_->addItem(QStringLiteral("第三次测量"), static_cast<int>(ProductionMeasureMode::Third));
    measureModeCombo_->addItem(QStringLiteral("军检"), static_cast<int>(ProductionMeasureMode::Mil));
    partTypeCombo_ = new QComboBox(this);
    partTypeCombo_->addItem(QStringLiteral("B型"), QStringLiteral("B"));
    partTypeCombo_->addItem(QStringLiteral("A型"), QStringLiteral("A"));
    plcModeCombo_ = new QComboBox(this);
    plcModeCombo_->addItem(QStringLiteral("手动"), 1);
    plcModeCombo_->addItem(QStringLiteral("自动"), 2);
    plcModeCombo_->addItem(QStringLiteral("单步"), 3);

    const QString comboStyle = QStringLiteral("QComboBox{min-height:30px;padding:2px 8px;} QComboBox::drop-down{width:24px;}");
    measureModeCombo_->setStyleSheet(comboStyle);
    partTypeCombo_->setStyleSheet(comboStyle);
    plcModeCombo_->setStyleSheet(comboStyle);
    measureModeCombo_->setMinimumWidth(120);
    partTypeCombo_->setMinimumWidth(96);
    plcModeCombo_->setMinimumWidth(96);

    if (auto *vlRun = qobject_cast<QVBoxLayout *>(ui_->groupRun->layout())) {
        while (QLayoutItem *item = vlRun->takeAt(0)) {
            if (item->widget()) item->widget()->hide();
            delete item;
        }

        auto makeCmdBtn = [this](const QString &text) {
            auto *btn = new QPushButton(text, this);
            btn->setMinimumSize(96, 34);
            btn->setMaximumSize(160, 36);
            return btn;
        };

        auto *selectRow = new QHBoxLayout();
        selectRow->setSpacing(8);
        selectRow->addWidget(new QLabel(QStringLiteral("控制模式"), this));
        selectRow->addWidget(plcModeCombo_);
        selectRow->addSpacing(8);
        selectRow->addWidget(new QLabel(QStringLiteral("工件类型"), this));
        selectRow->addWidget(partTypeCombo_);
        selectRow->addSpacing(8);
        selectRow->addWidget(new QLabel(QStringLiteral("业务模式"), this));
        selectRow->addWidget(measureModeCombo_);
        selectRow->addStretch(1);
        vlRun->addLayout(selectRow);

        auto *cmdGrid = new QGridLayout();
        cmdGrid->setHorizontalSpacing(8);
        cmdGrid->setVerticalSpacing(8);
        auto *btnInit = makeCmdBtn(QStringLiteral("初始化"));
        auto *btnStartMeasure = makeCmdBtn(QStringLiteral("开始测量"));
        auto *btnStartCal = makeCmdBtn(QStringLiteral("开始标定"));
        auto *btnStop2 = makeCmdBtn(QStringLiteral("停止"));
        auto *btnReset2 = makeCmdBtn(QStringLiteral("报警复位"));
        auto *btnRetest = makeCmdBtn(QStringLiteral("当前件复测"));
        cmdGrid->addWidget(btnInit, 0, 0);
        cmdGrid->addWidget(btnStartMeasure, 0, 1);
        cmdGrid->addWidget(btnStartCal, 0, 2);
        cmdGrid->addWidget(btnStop2, 1, 0);
        cmdGrid->addWidget(btnReset2, 1, 1);
        cmdGrid->addWidget(btnRetest, 1, 2);
        vlRun->addLayout(cmdGrid);

        auto *flowBox = new QGroupBox(QStringLiteral("PLC联调 / 自动流程"), this);
        auto *flowLay = new QGridLayout(flowBox);
        flowLay->setHorizontalSpacing(8);
        flowLay->setVerticalSpacing(8);
        auto *btnReadIds = makeCmdBtn(QStringLiteral("读取扫码ID"));
        auto *btnContinue = makeCmdBtn(QStringLiteral("继续(ID核对通过)"));
        auto *btnReadMb = makeCmdBtn(QStringLiteral("读取测量包"));
        auto *btnAck = makeCmdBtn(QStringLiteral("写ACK"));
        btnContinue->setMinimumWidth(144);
        flowLay->addWidget(btnReadIds, 0, 0);
        flowLay->addWidget(btnContinue, 0, 1);
        flowLay->addWidget(btnReadMb, 1, 0);
        flowLay->addWidget(btnAck, 1, 1);
        flowLay->setColumnStretch(2, 1);
        vlRun->addWidget(flowBox);

        connect(plcModeCombo_, qOverload<int>(&QComboBox::activated), this, [this](int){ emit requestSetPlcMode(selectedPlcModeValue()); });
        connect(partTypeCombo_, qOverload<int>(&QComboBox::activated), this, [this](int){ emit requestWriteCategoryMode(static_cast<int>(selectedPartTypeArg())); });
        connect(btnInit, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("INITIALIZE"), args); });
        connect(btnStartMeasure, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("measure_mode"), measureModeText()); args.insert(QStringLiteral("part_type"), selectedPartTypeText()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_AUTO"), args); ui_->listMessages->addItem(QStringLiteral("开始生产测量：类型=%1，业务=%2").arg(selectedPartTypeText(), measureModeText())); });
        connect(btnStartCal, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_CALIBRATION"), args); });
        connect(btnStop2, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); emit uiCommandRequested(QStringLiteral("STOP"), args); });
        connect(btnReset2, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); emit uiCommandRequested(QStringLiteral("RESET_ALARM"), args); });
        connect(btnRetest, &QPushButton::clicked, this, [this]{ QVariantMap args; args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue()); args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg())); emit uiCommandRequested(QStringLiteral("START_RETEST_CURRENT"), args); });
        connect(btnReadIds, &QPushButton::clicked, this, &ProductionWidget::requestReloadSlotIds);
        connect(btnContinue, &QPushButton::clicked, this, &ProductionWidget::requestContinueAfterIdCheck);
        connect(btnReadMb, &QPushButton::clicked, this, &ProductionWidget::requestReadMailbox);
        connect(btnAck, &QPushButton::clicked, this, &ProductionWidget::requestAckMailbox);
    }

    connect(ui_->btnWriteSlotIds, &QPushButton::clicked, this, &ProductionWidget::onBtnWriteSlotIds);
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
        btn->setText(QStringLiteral("槽位 %1\n—\n空\n—").arg(i));
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
    switch (slot_states_[slot]) {
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

int ProductionWidget::runtimeStateStyleCode(int slot) const
{
    if (slot < 0 || slot >= slot_states_.size()) return 0;
    switch (slot_states_[slot]) {
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

bool ProductionWidget::isPartIdEditableStep() const
{
    return (step_state_ == 0 || step_state_ == 10 || step_state_ == 30);
}

void ProductionWidget::refreshSelectedDetail()
{
    ui_->lblSelectedSlot->setText(QString::number(selected_slot_));

    const QString id = (slot_part_ids_.size() == 16) ? slot_part_ids_[selected_slot_] : QString();
    ui_->editSlotId->setText(id);
    ui_->lblResult->setText(runtimeStateText(selected_slot_));

    ui_->lblPart0->setText(id.isEmpty() ? QStringLiteral("—") : id);
    ui_->lblPart1->setText(QStringLiteral("—"));
    ui_->lblFail->setText(QStringLiteral("—"));

    if (mb_meas_seq_ != 0) {
        if (selected_slot_ == static_cast<int>(mb_slot0_)) {
            ui_->lblPart1->setText(QStringLiteral("meas=%1, item0=%2").arg(mb_meas_seq_).arg(mb_part_id0_.isEmpty() ? QStringLiteral("—") : mb_part_id0_));
        } else if (selected_slot_ == static_cast<int>(mb_slot1_)) {
            ui_->lblPart1->setText(QStringLiteral("meas=%1, item1=%2").arg(mb_meas_seq_).arg(mb_part_id1_.isEmpty() ? QStringLiteral("—") : mb_part_id1_));
        }
    }

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
        ui_->lblB_AD->setText(formatFloat(ms.b_ad_len_mm));
        ui_->lblB_BC->setText(formatFloat(ms.b_bc_len_mm));
        ui_->lblB_RunoutL->setText(formatFloat(ms.b_runout_left_mm));
        ui_->lblB_RunoutR->setText(formatFloat(ms.b_runout_right_mm));
    } else {
        ui_->stackMeasure->setCurrentIndex(0);
        ui_->lblA_Total->setText(formatFloat(ms.a_total_len_mm));
        ui_->lblA_Left->setText(QStringLiteral("%1 / %2").arg(formatFloat(ms.a_id_left_mm)).arg(formatFloat(ms.a_od_left_mm)));
        ui_->lblA_Right->setText(QStringLiteral("%1 / %2").arg(formatFloat(ms.a_id_right_mm)).arg(formatFloat(ms.a_od_right_mm)));
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
                summary = QStringLiteral("AD=%1  BC=%2").arg(formatFloat(ms.b_ad_len_mm, 2)).arg(formatFloat(ms.b_bc_len_mm, 2));
            } else {
                summary = QStringLiteral("L=%1").arg(formatFloat(ms.a_total_len_mm, 2));
            }
        }
    }
    if (summary.isEmpty() && slot_notes_.size() == 16 && !slot_notes_[slot].isEmpty()) {
        summary = slot_notes_[slot];
    }
    if (summary.isEmpty()) summary = QStringLiteral("—");

    QString title = QStringLiteral("槽位 %1").arg(slot);
    if (slot == reserved_cal_slot_) {
        title += QStringLiteral("*");
    }

    btn->setText(QStringLiteral("%1\n%2\n%3\n%4")
                 .arg(title)
                 .arg(shortId(id))
                 .arg(stateText)
                 .arg(summary));
    btn->setProperty("slotState", slotState);

    btn->style()->unpolish(btn);
    btn->style()->polish(btn);

    if (slot == selected_slot_) refreshSelectedDetail();
}

QString ProductionWidget::stepText(quint16 step) const
{
    switch (step) {
    case 0: return QStringLiteral("待机");
    case 10: return QStringLiteral("等待上料");
    case 20: return QStringLiteral("PLC扫码中");
    case 30: return QStringLiteral("等待PC核对ID");
    case 40: return QStringLiteral("抓取工件");
    case 50: return QStringLiteral("移载到工位");
    case 60: return QStringLiteral("夹紧/准备");
    case 70: return QStringLiteral("测量中");
    case 80: return QStringLiteral("整理测量包");
    case 90: return QStringLiteral("等待PC读取");
    case 100: return QStringLiteral("放回料架");
    case 110: return QStringLiteral("循环完成");
    case 200: return QStringLiteral("标定待上料(16号槽)");
    case 210: return QStringLiteral("标定等待PC确认");
    case 220: return QStringLiteral("标定测量中");
    case 230: return QStringLiteral("标定等待PC读取");
    case 240: return QStringLiteral("标定完成");
    case 900: return QStringLiteral("报警");
    case 910: return QStringLiteral("急停");
    default: return QStringLiteral("运行(%1)").arg(step);
    }
}


QString ProductionWidget::selectedPartTypeTextInternal() const
{
    if (!partTypeCombo_) return QStringLiteral("A");
    const QString t = partTypeCombo_->currentData().toString().trimmed().toUpper();
    return (t == QStringLiteral("B")) ? QStringLiteral("B") : QStringLiteral("A");
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
    if (!measureModeCombo_) return QStringLiteral("NORMAL");
    switch (static_cast<ProductionMeasureMode>(measureModeCombo_->currentData().toInt())) {
    case ProductionMeasureMode::Second: return QStringLiteral("SECOND");
    case ProductionMeasureMode::Third: return QStringLiteral("THIRD");
    case ProductionMeasureMode::Mil: return QStringLiteral("MIL");
    case ProductionMeasureMode::Normal:
    default: return QStringLiteral("NORMAL");
    }
}

quint32 ProductionWidget::measureModeCommandArg() const
{
    if (!measureModeCombo_) return 0;
    switch (static_cast<ProductionMeasureMode>(measureModeCombo_->currentData().toInt())) {
    case ProductionMeasureMode::Second: return 2;
    case ProductionMeasureMode::Third: return 3;
    case ProductionMeasureMode::Mil: return 9;
    case ProductionMeasureMode::Normal:
    default: return 1;
    }
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
    const QString display = machine_state != 0 ? machineStateMaskText(machine_state)
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
    setSlotComputedResult(slot, toWidgetSummary(s));
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
    mb_meas_seq_ = 0;
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
    mb_meas_seq_ = meas_seq;
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
