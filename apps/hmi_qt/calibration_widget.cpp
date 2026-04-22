#include "calibration_widget.hpp"
#include "calibration_widget_logic.hpp"
#include "plc_step_rules_v26.hpp"

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

namespace {
QString formatValue(double v, int prec = 3) {
    if (qIsNaN(v) || qIsInf(v)) return QStringLiteral("--");
    return QString::number(v, 'f', prec);
}
} // namespace

CalibrationWidget::CalibrationWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), cfg_(cfg)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *top = new QHBoxLayout();
    lblConnPlc_ = new QLabel(QStringLiteral("PLC"), this);
    lblConnPlc_->setStyleSheet("background:#9ca3af;color:white;border-radius:10px;padding:4px 10px;font-weight:600;");
    lblStep_ = new QLabel(QStringLiteral("标定待机"), this);
    QFont f = lblStep_->font();
    f.setPointSize(20);
    f.setBold(true);
    lblStep_->setFont(f);
    auto *btnReconnect = new QPushButton(QStringLiteral("重连PLC"), this);
    btnReconnect->setMinimumHeight(32);
    top->addWidget(lblConnPlc_);
    top->addSpacing(8);
    top->addWidget(btnReconnect);
    top->addStretch(1);
    top->addWidget(lblStep_);
    top->addStretch(1);
    lblAlarm_ = new QLabel(QStringLiteral("无报警"), this);
    auto *btnResetTop = new QPushButton(QStringLiteral("报警复位"), this);
    auto *btnMuteTop = new QPushButton(QStringLiteral("报警静音"), this);
    btnResetTop->setMinimumHeight(32);
    btnMuteTop->setMinimumHeight(32);
    top->addWidget(lblAlarm_);
    top->addSpacing(8);
    top->addWidget(btnResetTop);
    top->addWidget(btnMuteTop);
    root->addLayout(top);

    auto *infoBox = new QGroupBox(QStringLiteral("标定信息"), this);
    auto *infoLay = new QVBoxLayout(infoBox);
    auto *masterA = new QHBoxLayout();
    masterA->addWidget(new QLabel(QStringLiteral("A型标定件编号"), this));
    editMasterA_ = new QLineEdit(QStringLiteral("CAL-A-001"), this);
    masterA->addWidget(editMasterA_);
    auto *masterB = new QHBoxLayout();
    masterB->addWidget(new QLabel(QStringLiteral("B型标定件编号"), this));
    editMasterB_ = new QLineEdit(QStringLiteral("CAL-B-001"), this);
    masterB->addWidget(editMasterB_);
    lblSlot15_ = new QLabel(QStringLiteral("1 号槽位: 空"), this);
    lblSummary_ = new QLabel(QStringLiteral("结果摘要: --"), this);
    lblSummary_->setWordWrap(true);
    auto *tip = new QLabel(QStringLiteral("说明：标定流程固定使用槽位1；标定件身份由 PC 本地主数据维护。"), this);
    tip->setWordWrap(true);
    infoLay->addLayout(masterA);
    infoLay->addLayout(masterB);
    infoLay->addWidget(lblSlot15_);
    infoLay->addWidget(lblSummary_);
    infoLay->addWidget(tip);
    root->addWidget(infoBox);

    auto *resultBox = new QGroupBox(QStringLiteral("标定结果详情"), this);
    auto *resultLay = new QVBoxLayout(resultBox);
    lblResultJudge_ = new QLabel(QStringLiteral("判定：—"), this);
    lblResultReason_ = new QLabel(QStringLiteral("不合格原因：—"), this);
    lblMeasureLine1_ = new QLabel(QStringLiteral("A型总长(mm)：--"), this);
    lblMeasureLine2_ = new QLabel(QStringLiteral("左端内/外径：-- / --"), this);
    lblMeasureLine3_ = new QLabel(QStringLiteral("右端内/外径：-- / --"), this);
    lblResultReason_->setWordWrap(true);
    resultLay->addWidget(lblResultJudge_);
    resultLay->addWidget(lblResultReason_);
    resultLay->addWidget(lblMeasureLine1_);
    resultLay->addWidget(lblMeasureLine2_);
    resultLay->addWidget(lblMeasureLine3_);
    root->addWidget(resultBox);

    auto *ctrlBox = new QGroupBox(QStringLiteral("标定控制 / 自动流程"), this);
    auto *ctrlLay = new QVBoxLayout(ctrlBox);
    auto *ctrlRow = new QHBoxLayout();
    ctrlRow->addWidget(new QLabel(QStringLiteral("控制模式"), this));
    plcModeCombo_ = new QComboBox(this);
    plcModeCombo_->addItem(QStringLiteral("自动"), 2);
    plcModeCombo_->setEnabled(false);
    ctrlRow->addWidget(plcModeCombo_);
    ctrlRow->addSpacing(12);
    ctrlRow->addWidget(new QLabel(QStringLiteral("标定类型"), this));
    partTypeCombo_ = new QComboBox(this);
    partTypeCombo_->addItem(QStringLiteral("A型标定"), QStringLiteral("A"));
    partTypeCombo_->addItem(QStringLiteral("B型标定"), QStringLiteral("B"));
    ctrlRow->addWidget(partTypeCombo_);
    ctrlRow->addStretch(1);
    ctrlLay->addLayout(ctrlRow);

    auto *cmdGrid = new QGridLayout();
    cmdGrid->setHorizontalSpacing(8);
    cmdGrid->setVerticalSpacing(8);
    auto *btnInit = new QPushButton(QStringLiteral("初始化"), this);
    btnStart_ = new QPushButton(QStringLiteral("开始标定"), this);
    auto *btnStop = new QPushButton(QStringLiteral("停止"), this);
    const QList<QPushButton *> cmdButtons = {btnInit, btnStart_, btnStop};
    for (auto *b : cmdButtons) b->setMinimumHeight(34);
    cmdGrid->addWidget(btnInit, 0, 0);
    cmdGrid->addWidget(btnStart_, 0, 1);
    cmdGrid->addWidget(btnStop, 0, 2);
    cmdGrid->setColumnStretch(0, 1);
    cmdGrid->setColumnStretch(1, 1);
    cmdGrid->setColumnStretch(2, 1);
    ctrlLay->addLayout(cmdGrid);
    root->addWidget(ctrlBox);

    auto *debugBox = new QGroupBox(QStringLiteral("PLC联调 / 标定流程"), this);
    auto *debugGrid = new QGridLayout(debugBox);
    debugGrid->setHorizontalSpacing(8);
    debugGrid->setVerticalSpacing(8);
    auto *btnRead = new QPushButton(QStringLiteral("读取测量包"), this);
    auto *btnAck = new QPushButton(QStringLiteral("写ACK"), this);
    auto *btnRetest = new QPushButton(QStringLiteral("当前件复测"), this);
    auto *btnContinue = new QPushButton(QStringLiteral("继续（不复测）"), this);
    auto *btnCompute = new QPushButton(QStringLiteral("计算结果"), this);
    const QList<QPushButton *> dbgButtons = {btnRead, btnAck, btnRetest, btnContinue, btnCompute};
    for (auto *b : dbgButtons) b->setMinimumHeight(34);
    debugGrid->addWidget(btnRead, 0, 0);
    debugGrid->addWidget(btnAck, 0, 1);
    debugGrid->addWidget(btnRetest, 1, 0);
    debugGrid->addWidget(btnContinue, 1, 1);
    debugGrid->addWidget(btnCompute, 2, 0, 1, 2);
    debugGrid->setColumnStretch(0, 1);
    debugGrid->setColumnStretch(1, 1);
    root->addWidget(debugBox);

    listMessages_ = new QListWidget(this);
    root->addWidget(listMessages_, 1);

    const int autoIndex = plcModeCombo_->findData(2);
    if (autoIndex >= 0) plcModeCombo_->setCurrentIndex(autoIndex);
    const int typeAIndex = partTypeCombo_->findData(QStringLiteral("A"));
    if (typeAIndex >= 0) partTypeCombo_->setCurrentIndex(typeAIndex);

    auto cmdArgs = [this]() {
        QVariantMap args;
        args.insert(QStringLiteral("plc_mode"), selectedPlcModeValue());
        args.insert(QStringLiteral("part_type"), selectedPartTypeText());
        args.insert(QStringLiteral("part_type_arg"), static_cast<int>(selectedPartTypeArg()));
        args.insert(QStringLiteral("ui_source"), QStringLiteral("calibration"));
        return args;
    };

    connect(btnReconnect, &QPushButton::clicked, this, [this]{
        emit requestReconnectPlc();
    });
    connect(btnResetTop, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("RESET_ALARM"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：报警复位"));
    });
    connect(btnMuteTop, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("ALARM_MUTE"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：报警静音"));
    });
    connect(partTypeCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) {
        emit requestWriteCategoryMode(static_cast<int>(selectedPartTypeArg()));
        appendLogMessage(QStringLiteral("已请求写工件类型：%1").arg(selectedPartTypeText()));
    });
    connect(btnInit, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("INITIALIZE"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：初始化"));
    });
    connect(btnStart_, &QPushButton::clicked, this, [this, cmdArgs]{
        masterIdLockedByStart_ = true;
        updateMasterIdEditability();
        emit uiCommandRequested(QStringLiteral("START_CALIBRATION"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：开始标定（类型=%1）").arg(selectedPartTypeText()));
    });
    connect(btnStop, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("STOP"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：停止"));
    });
    connect(btnRead, &QPushButton::clicked, this, [this]{
        emit requestReadMailbox();
        appendLogMessage(QStringLiteral("已请求读取标定测量包"));
    });
    connect(btnAck, &QPushButton::clicked, this, [this]{
        emit requestAckMailbox();
        appendLogMessage(QStringLiteral("已请求写入 pc_ack"));
    });
    connect(btnRetest, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("START_RETEST_CURRENT"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：当前件复测"));
    });
    connect(btnContinue, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：继续（不复测）"));
    });
    connect(btnCompute, &QPushButton::clicked, this, [this, cmdArgs]{
        emit uiCommandRequested(QStringLiteral("COMPUTE_RESULT"), cmdArgs());
        appendLogMessage(QStringLiteral("已请求：计算结果"));
    });

    setPlcConnected(false);
    setStepState(0);
    setAlarm(0, 0);
    setTrayPresentMask(0);
    updateMasterIdEditability();
}

void CalibrationWidget::setPlcConnected(bool ok)
{
    lblConnPlc_->setStyleSheet(calibration_widget_logic::plcConnStyle(ok));
}

void CalibrationWidget::setStepState(quint16 step)
{
    stepState_ = step;
    if (!plc_step_rules_v26::isCalibrationStep(stepState_)
        && stepState_ == plc_step_rules_v26::kStepWaitStart) {
        masterIdLockedByStart_ = false;
    }
    updateMasterIdEditability();
    lblStep_->setText(calibration_widget_logic::stepText(step));
}

void CalibrationWidget::setAlarm(quint16 alarmCode, quint16 alarmLevel)
{
    if (!lblAlarm_) return;
    if (alarmCode == 0) {
        lblAlarm_->setText(QStringLiteral("无报警"));
    } else {
        lblAlarm_->setText(QStringLiteral("报警：%1（等级 %2）").arg(alarmCode).arg(alarmLevel));
    }
}

void CalibrationWidget::setTrayPresentMask(quint16 mask)
{
    trayPresentMask_ = mask;
    refreshSlot15State();
}

void CalibrationWidget::setMailboxReady(bool ready)
{
    appendLogMessage(ready ? QStringLiteral("PLC 已冻结标定测量包，可读取。")
                           : QStringLiteral("标定测量包已清空。"));
}

void CalibrationWidget::setCurrentPlcMode(int mode)
{
    if (!plcModeCombo_) return;
    const int idx = plcModeCombo_->findData(mode);
    if (idx >= 0 && idx != plcModeCombo_->currentIndex()) {
        plcModeCombo_->setCurrentIndex(idx);
    }
}

void CalibrationWidget::setMasterPartIds(const QString &aPartId, const QString &bPartId)
{
    if (editMasterA_) editMasterA_->setText(aPartId);
    if (editMasterB_) editMasterB_->setText(bPartId);
}

void CalibrationWidget::setSlotSummary(const core::CalibrationSlotSummary &s)
{
    const QString summary = calibration_widget_logic::buildSummaryText(s);
    if (lblSummary_) lblSummary_->setText(summary);
    if (s.valid) appendLogMessage(summary);

    const QString type = s.calibration_type.trimmed().toUpper();
    QString judge = QStringLiteral("待计算");
    if (s.judgement_known) {
        judge = s.judgement_ok ? QStringLiteral("合格") : QStringLiteral("不合格");
    } else if (s.valid && s.compute.judgement == core::MeasurementJudgement::Ok) {
        judge = QStringLiteral("合格");
    } else if (s.valid && s.compute.judgement == core::MeasurementJudgement::Ng) {
        judge = QStringLiteral("不合格");
    }
    if (lblResultJudge_) lblResultJudge_->setText(QStringLiteral("判定：%1").arg(judge));
    if (lblResultReason_) {
        lblResultReason_->setText(QStringLiteral("不合格原因：%1")
                                      .arg(s.fail_reason_text.trimmed().isEmpty()
                                               ? QStringLiteral("—")
                                               : s.fail_reason_text.trimmed()));
    }

    if (type == QStringLiteral("B")) {
        if (lblMeasureLine1_) {
            lblMeasureLine1_->setText(QStringLiteral("B_AD长度(mm)：%1")
                                          .arg(formatValue(s.compute.values.ad_len_mm)));
        }
        if (lblMeasureLine2_) {
            lblMeasureLine2_->setText(QStringLiteral("B_BC长度(mm)：%1")
                                          .arg(formatValue(s.compute.values.bc_len_mm)));
        }
        if (lblMeasureLine3_) {
            lblMeasureLine3_->setText(QStringLiteral("左/右跳动(mm)：%1 / %2")
                                          .arg(formatValue(s.compute.values.runout_left_mm))
                                          .arg(formatValue(s.compute.values.runout_right_mm)));
        }
        return;
    }

    if (lblMeasureLine1_) {
        lblMeasureLine1_->setText(QStringLiteral("A型总长(mm)：%1")
                                      .arg(formatValue(s.compute.values.total_len_mm)));
    }
    if (lblMeasureLine2_) {
        lblMeasureLine2_->setText(QStringLiteral("左端内/外径：%1 / %2")
                                      .arg(formatValue(s.compute.values.id_left_mm))
                                      .arg(formatValue(s.compute.values.od_left_mm)));
    }
    if (lblMeasureLine3_) {
        lblMeasureLine3_->setText(QStringLiteral("右端内/外径：%1 / %2")
                                      .arg(formatValue(s.compute.values.id_right_mm))
                                      .arg(formatValue(s.compute.values.od_right_mm)));
    }
}

void CalibrationWidget::setSlotSummaries(const QVector<core::CalibrationSlotSummary> &summaries)
{
    for (const auto &s : summaries) {
        if (s.slot_index == core::kCalibrationSlotIndex) {
            setSlotSummary(s);
            return;
        }
    }
}

void CalibrationWidget::appendLogMessage(const QString &text)
{
    if (!listMessages_) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    listMessages_->addItem(trimmed);
}

QString CalibrationWidget::masterPartIdForType(QChar partType) const
{
    const bool isB = partType.toUpper() == QChar('B');
    const QString id = isB
                           ? (editMasterB_ ? editMasterB_->text().trimmed() : QString())
                           : (editMasterA_ ? editMasterA_->text().trimmed() : QString());
    if (!id.isEmpty()) return id;
    return isB ? QStringLiteral("CAL-B-001") : QStringLiteral("CAL-A-001");
}

QString CalibrationWidget::selectedPartTypeText() const
{
    if (!partTypeCombo_) return QStringLiteral("A");
    const QString v = partTypeCombo_->currentData().toString().trimmed().toUpper();
    return (v == QStringLiteral("B")) ? QStringLiteral("B") : QStringLiteral("A");
}

quint32 CalibrationWidget::selectedPartTypeArg() const
{
    return selectedPartTypeText() == QStringLiteral("B") ? 1u : 2u;
}

int CalibrationWidget::selectedPlcModeValue() const
{
    return plcModeCombo_ ? plcModeCombo_->currentData().toInt() : 2;
}

void CalibrationWidget::refreshSlot15State()
{
    lblSlot15_->setText(calibration_widget_logic::calibrationSlotStateText(trayPresentMask_));
}

void CalibrationWidget::updateMasterIdEditability()
{
    const bool inCalibrationFlow = plc_step_rules_v26::isCalibrationStep(stepState_);
    const bool editable = !masterIdLockedByStart_ && !inCalibrationFlow;
    if (editMasterA_) editMasterA_->setEnabled(editable);
    if (editMasterB_) editMasterB_->setEnabled(editable);
}
