#include "production_widget.hpp"
#include "ui_production_widget.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QPushButton>
#include <QUrl>

namespace {

// result_code: 0=unknown, 1=ok, 2=ng, 3=present_no_result
inline int slotResultCode(quint16 present_mask, quint16 ok_mask, quint16 ng_mask, int slot)
{
    const bool present = (present_mask >> slot) & 0x1;
    const bool ok = (ok_mask >> slot) & 0x1;
    const bool ng = (ng_mask >> slot) & 0x1;
    if (!present) return 0;
    if (ok) return 1;
    if (ng) return 2;
    return 3;
}

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

    // 连接状态灯（用样式表）
    setStyleSheet(R"(
        QLabel#lblConnPlc, QLabel#lblConnDb, QLabel#lblConnMes {
            border-radius: 10px;
            padding: 4px 10px;
            color: white;
            font-weight: 600;
        }
        QLabel[connState="0"] { background: #9ca3af; }   /* gray */
        QLabel[connState="1"] { background: #22c55e; }   /* green */

        QPushButton[slotState="0"] { background: #f3f4f6; border: 1px solid #e5e7eb; border-radius: 10px; }
        QPushButton[slotState="1"] { background: #dcfce7; border: 1px solid #86efac; border-radius: 10px; }
        QPushButton[slotState="2"] { background: #fee2e2; border: 1px solid #fca5a5; border-radius: 10px; }
        QPushButton[slotState="3"] { background: #e0f2fe; border: 1px solid #7dd3fc; border-radius: 10px; }

        QPushButton[slotSelected="1"] { border: 2px solid #111827; }
    )");

    ui_->lblConnPlc->setProperty("connState", 0);
    ui_->lblConnDb->setProperty("connState", 0);
    ui_->lblConnMes->setProperty("connState", 0);

    // Mode buttons: 互斥
    auto *bg = new QButtonGroup(this);
    bg->setExclusive(true);
    bg->addButton(ui_->btnModeAuto);
    bg->addButton(ui_->btnModeManual);
    ui_->btnModeAuto->setChecked(true);

    // 命令请求（后续接 PLC Command Block）
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
        if (slot_ids_.size() != 16) slot_ids_ = QVector<QString>(16);
        slot_ids_[selected_slot_] = ui_->editSlotId->text().trimmed();
        updateSlotCard(selected_slot_);
    });

    initSlotCards();

    // init defaults
    setPlcConnected(false);
    setStepState(0);
    setMachineState(0, "IDLE");
    setStateSeq(0);
    setAlarm(0, 0);
    setMeasureDone(false);
    setTrayMasks(0, 0, 0);
    setInterlockMask(0);
    setSlotIds(QVector<QString>(16));
    slot_meas_ = QVector<SlotMeasureSummary>(16);
    selectSlot(0);

    ui_->listMessages->addItem(QStringLiteral("提示：生产页已简化。互锁/寄存器/邮箱详细信息请到【诊断】页查看。"));
}

ProductionWidget::~ProductionWidget()
{
    delete ui_;
}

void ProductionWidget::initSlotCards()
{
    // 4x4 grid
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

        // 让代码能通过 objectName 访问（如需）
        btn->setObjectName(QString("btnSlot%1").arg(i));
    }
}

void ProductionWidget::selectSlot(int slot)
{
    if (slot < 0 || slot >= 16) return;
    selected_slot_ = slot;

    // update selection border
    for (int i = 0; i < 16; ++i) {
        auto *btn = findChild<QPushButton*>(QString("btnSlot%1").arg(i));
        if (!btn) continue;
        btn->setProperty("slotSelected", i == selected_slot_ ? 1 : 0);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }

    refreshSelectedDetail();
}

void ProductionWidget::refreshSelectedDetail()
{
    ui_->lblSelectedSlot->setText(QString::number(selected_slot_));

    const QString id = (slot_ids_.size() == 16) ? slot_ids_[selected_slot_] : QString();
    ui_->editSlotId->setText(id);

    // 结果：来自 tray masks（槽位级）
    const int rc = slotResultCode(tray_present_, tray_ok_, tray_ng_, selected_slot_);
    if (rc == 1) ui_->lblResult->setText(QStringLiteral("OK"));
    else if (rc == 2) ui_->lblResult->setText(QStringLiteral("NG"));
    else if (rc == 3) ui_->lblResult->setText(QStringLiteral("已上料"));
    else ui_->lblResult->setText(QStringLiteral("空"));

    // Mailbox preview：这里仅做极简展示（更详细放诊断页）
    ui_->lblPart0->setText(QStringLiteral("—"));
    ui_->lblPart1->setText(QStringLiteral("—"));
    ui_->lblFail->setText(QStringLiteral("—"));

    // 如果 mailbox preview 包含当前槽位，展示对应 part_id / fail
    if (mb_meas_seq_ != 0) {
        if (selected_slot_ == static_cast<int>(mb_slot0_)) {
            ui_->lblPart0->setText(mb_part_id0_.isEmpty() ? QStringLiteral("—") : mb_part_id0_);
            ui_->lblFail->setText(mb_ok0_ ? QStringLiteral("—") : QStringLiteral("NG(%1)").arg(mb_fail0_));
        } else if (selected_slot_ == static_cast<int>(mb_slot1_)) {
            ui_->lblPart1->setText(mb_part_id1_.isEmpty() ? QStringLiteral("—") : mb_part_id1_);
            ui_->lblFail->setText(mb_ok1_ ? QStringLiteral("—") : QStringLiteral("NG(%1)").arg(mb_fail1_));
        }
    }


    // 测量结果展示（最终关键结果）
    SlotMeasureSummary ms;
    if (slot_meas_.size() == 16) ms = slot_meas_[selected_slot_];

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
    const bool editable = (step_state_ == 0 || step_state_ == 10);
    ui_->editSlotId->setEnabled(editable);
    ui_->btnWriteSlotIds->setEnabled(editable);
}

void ProductionWidget::updateSlotCard(int slot)
{
    auto *btn = findChild<QPushButton*>(QString("btnSlot%1").arg(slot));
    if (!btn) return;

    const QString id = (slot_ids_.size() == 16) ? slot_ids_[slot] : QString();
    const int rc = slotResultCode(tray_present_, tray_ok_, tray_ng_, slot);

    QString stateText;
    int slotState = 0;
    if (rc == 1) { stateText = QStringLiteral("OK"); slotState = 1; }
    else if (rc == 2) { stateText = QStringLiteral("NG"); slotState = 2; }
    else if (rc == 3) { stateText = QStringLiteral("已上料"); slotState = 3; }
    else { stateText = QStringLiteral("空"); slotState = 0; }

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
    if (summary.isEmpty()) summary = QStringLiteral("—");

    btn->setText(QStringLiteral("槽位 %1\n%2\n%3\n%4")
                 .arg(slot)
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
    case 20: return QStringLiteral("扫码/识别");
    case 30: return QStringLiteral("夹紧/准备");
    case 40: return QStringLiteral("测量中");
    case 50: return QStringLiteral("计算/判定");
    case 60: return QStringLiteral("写入结果");
    case 70: return QStringLiteral("等待取料");
    case 80: return QStringLiteral("复位/归位");
    case 90: return QStringLiteral("空闲");
    case 100: return QStringLiteral("完成");
    case 110: return QStringLiteral("结束");
    case 900: return QStringLiteral("报警");
    case 910: return QStringLiteral("急停");
    default: return QStringLiteral("运行(%1)").arg(step);
    }
}

void ProductionWidget::setPlcConnected(bool ok)
{
    plc_connected_ = ok;
    ui_->lblConnPlc->setProperty("connState", ok ? 1 : 0);
    ui_->lblConnPlc->style()->unpolish(ui_->lblConnPlc);
    ui_->lblConnPlc->style()->polish(ui_->lblConnPlc);
}

void ProductionWidget::setMachineState(quint16 /*machine_state*/, const QString &text)
{
    // 简化：生产页只显示 step_state 主文本；machine_state 放诊断页
    if (!text.isEmpty()) {
        ui_->listMessages->addItem(QStringLiteral("机器状态：%1").arg(text));
    }
}

void ProductionWidget::setStepState(quint16 step_state)
{
    step_state_ = step_state;
    ui_->lblStepBig->setText(stepText(step_state));
    updateSlotEditability();
}

void ProductionWidget::setStateSeq(quint32 /*state_seq*/)
{
    // 简化：不展示
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
        ui_->listMessages->addItem(QStringLiteral("测量完成：PLC 已冻结 Mailbox（等待上位机读取并 ACK）"));
    }
}

void ProductionWidget::setTrayMasks(quint16 present_mask, quint16 ok_mask, quint16 ng_mask)
{
    tray_present_ = present_mask;
    tray_ok_ = ok_mask;
    tray_ng_ = ng_mask;

    for (int i = 0; i < 16; ++i) updateSlotCard(i);
}

void ProductionWidget::setInterlockMask(quint32 /*mask*/)
{
    // 简化：互锁详情放诊断页
}

void ProductionWidget::setSlotIds(const QVector<QString> &slot_ids)
{
    slot_ids_ = slot_ids;
    if (slot_ids_.size() != 16) slot_ids_.resize(16);
    for (int i = 0; i < 16; ++i) updateSlotCard(i);
}


void ProductionWidget::setSlotMeasureSummary(int slot, const SlotMeasureSummary &s)
{
    if (slot < 0 || slot >= 16) return;
    if (slot_meas_.size() != 16) slot_meas_ = QVector<SlotMeasureSummary>(16);
    slot_meas_[slot] = s;
    updateSlotCard(slot);
    if (slot == selected_slot_) refreshSelectedDetail();
}

void ProductionWidget::setMailboxPreview(quint32 meas_seq,
                                        QChar part_type,
                                        quint16 slot0, quint16 slot1,
                                        const QString &part_id0,
                                        const QString &part_id1,
                                        bool ok0, bool ok1,
                                        quint16 fail0, quint16 fail1,
                                        float /*total_len0_mm*/,
                                        float /*total_len1_mm*/)
{
    mb_meas_seq_ = meas_seq;
    mb_part_type_ = part_type;
    mb_slot0_ = slot0;
    mb_slot1_ = slot1;
    mb_part_id0_ = part_id0;
    mb_part_id1_ = part_id1;
    mb_ok0_ = ok0;
    mb_ok1_ = ok1;
    mb_fail0_ = fail0;
    mb_fail1_ = fail1;

    refreshSelectedDetail();
}

void ProductionWidget::onBtnWriteSlotIds()
{
    if (!(step_state_ == 0 || step_state_ == 10)) {
        ui_->listMessages->addItem(QStringLiteral("当前工步不允许写入槽位号（仅 step_state=0/10 可写）"));
        return;
    }
    emit requestWriteSlotIds(slot_ids_);
    ui_->listMessages->addItem(QStringLiteral("已请求写入槽位号（PC->PLC）"));
}

void ProductionWidget::onBtnReloadSlotIds()
{
    emit requestReloadSlotIds();
    ui_->listMessages->addItem(QStringLiteral("已请求读取槽位号（PLC->PC）"));
}

void ProductionWidget::onBtnReadMailbox()
{
    emit requestReadMailbox();
    ui_->listMessages->addItem(QStringLiteral("已请求读取测量结果（Mailbox）"));
}

void ProductionWidget::onBtnAckMailbox()
{
    emit requestAckMailbox();
    ui_->listMessages->addItem(QStringLiteral("已请求 ACK（pc_ack）"));
}

void ProductionWidget::onBtnDevDemo()
{
    // 仅用于 UI 演示（不依赖 PLC）
    static int tick = 0;
    tick++;

    setPlcConnected(true);
    ui_->lblConnDb->setProperty("connState", 1);
    ui_->lblConnDb->style()->unpolish(ui_->lblConnDb);
    ui_->lblConnDb->style()->polish(ui_->lblConnDb);

    ui_->lblConnMes->setProperty("connState", (tick % 2) ? 1 : 0);
    ui_->lblConnMes->style()->unpolish(ui_->lblConnMes);
    ui_->lblConnMes->style()->polish(ui_->lblConnMes);

    setStepState((tick % 6) * 10);
    setAlarm((tick % 7 == 0) ? 123 : 0, (tick % 7 == 0) ? 2 : 0);

    quint16 present = 0, ok = 0, ng = 0;
    for (int i = 0; i < 16; ++i) {
        if ((tick + i) % 3 != 0) present |= (1u << i);
        if ((tick + i) % 5 == 0) ok |= (1u << i);
        if ((tick + i) % 7 == 0) ng |= (1u << i);
    }
    setTrayMasks(present, ok, ng);

    QVector<QString> ids(16);
    for (int i = 0; i < 16; ++i) ids[i] = QStringLiteral("S%1_20260303_ABCDEF%2").arg(i,2,10,QChar('0')).arg(i);
    setSlotIds(ids);

    
    // 模拟测量结果（A/B 交替）
    for (int s = 0; s < 16; ++s) {
        SlotMeasureSummary ms;
        ms.valid = ((present >> s) & 0x1); // 有料才显示
        if (!ms.valid) { setSlotMeasureSummary(s, ms); continue; }
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
        setSlotMeasureSummary(s, ms);
    }

setMailboxPreview(100 + tick, QChar('A'), 2, 3,
                      QStringLiteral("PART_AAAAA_0001"),
                      QStringLiteral("PART_BBBBB_0002"),
                      true, false,
                      0, 12,
                      qQNaN(), qQNaN());
}

