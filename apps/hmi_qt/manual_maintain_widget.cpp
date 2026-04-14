#include "manual_maintain_widget.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QString plcModeText(int mode) {
  switch (mode) {
  case 1: return QStringLiteral("手动");
  case 2: return QStringLiteral("自动");
  case 3: return QStringLiteral("单步");
  default: return QStringLiteral("模式(%1)").arg(mode);
  }
}

QDoubleSpinBox *makeSpinBox(double min, double max, double step, int decimals, QWidget *parent) {
  auto *sb = new QDoubleSpinBox(parent);
  sb->setRange(min, max);
  sb->setDecimals(decimals);
  sb->setSingleStep(step);
  sb->setKeyboardTracking(false);
  return sb;
}
}

ManualMaintainWidget::ManualMaintainWidget(QWidget *parent) : QWidget(parent) {
  auto *root = new QVBoxLayout(this);

  auto *statusBox = new QGroupBox(QStringLiteral("当前状态"), this);
  auto *statusLay = new QHBoxLayout(statusBox);
  lbConn_ = new QLabel(QStringLiteral("连接：-"), statusBox);
  lbMachine_ = new QLabel(QStringLiteral("机器：-"), statusBox);
  lbStep_ = new QLabel(QStringLiteral("步骤：-"), statusBox);
  lbCurrentMode_ = new QLabel(QStringLiteral("当前PLC模式：-"), statusBox);
  statusLay->addWidget(lbConn_);
  statusLay->addWidget(lbMachine_);
  statusLay->addWidget(lbStep_);
  statusLay->addWidget(lbCurrentMode_, 1);
  root->addWidget(statusBox);

  auto *manualBox = new QGroupBox(QStringLiteral("手动控制 / 维护"), this);
  auto *manualLay = new QVBoxLayout(manualBox);

  auto *modeRow = new QHBoxLayout();
  modeRow->addWidget(new QLabel(QStringLiteral("目标PLC模式："), manualBox));
  targetModeCombo_ = new QComboBox(manualBox);
  targetModeCombo_->addItem(QStringLiteral("手动"), 1);
  targetModeCombo_->addItem(QStringLiteral("自动"), 2);
  targetModeCombo_->addItem(QStringLiteral("单步"), 3);
  auto *btnWriteMode = new QPushButton(QStringLiteral("写入模式"), manualBox);
  modeRow->addWidget(targetModeCombo_);
  modeRow->addWidget(btnWriteMode);
  modeRow->addSpacing(12);
  modeRow->addWidget(new QLabel(QStringLiteral("工件类型："), manualBox));
  cmdPartTypeCombo_ = new QComboBox(manualBox);
  cmdPartTypeCombo_->addItem(QStringLiteral("B型"), 1);
  cmdPartTypeCombo_->addItem(QStringLiteral("A型"), 2);
  modeRow->addWidget(cmdPartTypeCombo_);
  modeRow->addStretch(1);
  manualLay->addLayout(modeRow);

  auto *cmdLay = new QGridLayout();
  auto addCmdBtn = [&](const QString &textLabel, const QString &cmdName, int row, int col) {
    auto *btn = new QPushButton(textLabel, manualBox);
    cmdLay->addWidget(btn, row, col);
    connect(btn, &QPushButton::clicked, this, [this, cmdName]() {
      QVariantMap args;
      args.insert(QStringLiteral("plc_mode"), selectedTargetMode());
      args.insert(QStringLiteral("part_type_arg"), selectedPartType());
      emit requestPlcNamedCommand(cmdName, args);
    });
  };
  addCmdBtn(QStringLiteral("初始化"), QStringLiteral("INITIALIZE"), 0, 0);
  addCmdBtn(QStringLiteral("回零"), QStringLiteral("HOME_ALL"), 0, 1);
  addCmdBtn(QStringLiteral("报警复位"), QStringLiteral("RESET_ALARM"), 0, 2);
  addCmdBtn(QStringLiteral("开始测量"), QStringLiteral("START_AUTO"), 0, 3);
  addCmdBtn(QStringLiteral("开始标定"), QStringLiteral("START_CALIBRATION"), 1, 0);
  addCmdBtn(QStringLiteral("停止"), QStringLiteral("STOP"), 1, 1);
  addCmdBtn(QStringLiteral("继续(ID核对通过)"), QStringLiteral("CONTINUE_AFTER_ID_CHECK"), 1, 2);
  addCmdBtn(QStringLiteral("请求重扫"), QStringLiteral("REQUEST_RESCAN_IDS"), 1, 3);
  addCmdBtn(QStringLiteral("NG后继续"), QStringLiteral("CONTINUE_AFTER_NG_CONFIRM"), 2, 0);
  addCmdBtn(QStringLiteral("当前件复测"), QStringLiteral("START_RETEST_CURRENT"), 2, 1);
  manualLay->addLayout(cmdLay);

  auto *flowBox = new QGroupBox(QStringLiteral("PLC联调 / 自动流程"), manualBox);
  auto *flowLay = new QVBoxLayout(flowBox);

  auto *flowModeLay = new QHBoxLayout();
  flowModeLay->addWidget(new QLabel(QStringLiteral("流程模式："), flowBox));
  plcFlowCombo_ = new QComboBox(flowBox);
  plcFlowCombo_->addItem(QStringLiteral("手动（只监听，不自动推进）"), 0);
  plcFlowCombo_->addItem(QStringLiteral("半自动（扫码后自动继续，不自动ACK）"), 1);
  plcFlowCombo_->addItem(QStringLiteral("全自动（扫码后自动继续，读包后自动ACK）"), 2);
  flowModeLay->addWidget(plcFlowCombo_, 1);
  flowLay->addLayout(flowModeLay);

  auto *flowBtnLay1 = new QHBoxLayout();
  auto *btnPlcPoll = new QPushButton(QStringLiteral("轮询一拍"), flowBox);
  auto *btnPlcReloadIds = new QPushButton(QStringLiteral("读取扫码ID"), flowBox);
  auto *btnPlcContinue = new QPushButton(QStringLiteral("继续流程(ID核对通过)"), flowBox);
  flowBtnLay1->addWidget(btnPlcPoll);
  flowBtnLay1->addWidget(btnPlcReloadIds);
  flowBtnLay1->addWidget(btnPlcContinue);
  flowLay->addLayout(flowBtnLay1);

  auto *flowBtnLay2 = new QHBoxLayout();
  auto *btnPlcRescan = new QPushButton(QStringLiteral("请求重扫ID"), flowBox);
  auto *btnPlcReadMailbox = new QPushButton(QStringLiteral("读取测量包"), flowBox);
  auto *btnPlcAck = new QPushButton(QStringLiteral("写 ACK(pc_ack)"), flowBox);
  flowBtnLay2->addWidget(btnPlcRescan);
  flowBtnLay2->addWidget(btnPlcReadMailbox);
  flowBtnLay2->addWidget(btnPlcAck);
  flowLay->addLayout(flowBtnLay2);

  manualLay->addWidget(flowBox);

  auto *axisBox = new QGroupBox(QStringLiteral("单轴维护"), manualBox);
  auto *axisRoot = new QVBoxLayout(axisBox);
  auto *axisTop = new QHBoxLayout();
  axisCombo_ = new QComboBox(axisBox);
  axisCombo_->addItem(QStringLiteral("龙门X轴"), 0);
  axisCombo_->addItem(QStringLiteral("龙门Y轴"), 1);
  axisCombo_->addItem(QStringLiteral("龙门Z轴"), 2);
  axisCombo_->addItem(QStringLiteral("测量X1轴（内外径工位平移）"), 3);
  axisCombo_->addItem(QStringLiteral("测量X2轴（跳动工位平移）"), 4);
  axisCombo_->addItem(QStringLiteral("测量X3轴（长度工位平移）"), 5);
  axisCombo_->addItem(QStringLiteral("内外径R1轴（左端旋转）"), 6);
  axisCombo_->addItem(QStringLiteral("内外径R2轴（右端旋转）"), 7);
  axisCombo_->addItem(QStringLiteral("跳动R3轴（左端旋转）"), 8);
  axisCombo_->addItem(QStringLiteral("跳动R4轴（右端旋转）"), 9);
  axisTop->addWidget(new QLabel(QStringLiteral("轴："), axisBox));
  axisTop->addWidget(axisCombo_, 1);
  auto *btnEnableOn = new QPushButton(QStringLiteral("使能ON"), axisBox);
  auto *btnEnableOff = new QPushButton(QStringLiteral("使能OFF"), axisBox);
  auto *btnReset = new QPushButton(QStringLiteral("复位"), axisBox);
  auto *btnHome = new QPushButton(QStringLiteral("回零"), axisBox);
  auto *btnEStop = new QPushButton(QStringLiteral("EStop"), axisBox);
  auto *btnStop = new QPushButton(QStringLiteral("停止"), axisBox);
  axisTop->addWidget(btnEnableOn);
  axisTop->addWidget(btnEnableOff);
  axisTop->addWidget(btnReset);
  axisTop->addWidget(btnHome);
  axisTop->addWidget(btnEStop);
  axisTop->addWidget(btnStop);
  axisRoot->addLayout(axisTop);

  auto *axisMotion = new QHBoxLayout();
  spAcc_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spDec_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spPos_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spVel_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spVel_->setValue(10.0);
  auto *btnMoveAbs = new QPushButton(QStringLiteral("MoveAbs"), axisBox);
  auto *btnMoveRel = new QPushButton(QStringLiteral("MoveRel"), axisBox);
  auto *btnJogFwd = new QPushButton(QStringLiteral("Jog+"), axisBox);
  auto *btnJogBwd = new QPushButton(QStringLiteral("Jog-"), axisBox);
  axisMotion->addWidget(new QLabel(QStringLiteral("Acc"), axisBox));
  axisMotion->addWidget(spAcc_);
  axisMotion->addWidget(new QLabel(QStringLiteral("Dec"), axisBox));
  axisMotion->addWidget(spDec_);
  axisMotion->addWidget(new QLabel(QStringLiteral("Pos"), axisBox));
  axisMotion->addWidget(spPos_);
  axisMotion->addWidget(new QLabel(QStringLiteral("Vel"), axisBox));
  axisMotion->addWidget(spVel_);
  axisMotion->addWidget(btnMoveAbs);
  axisMotion->addWidget(btnMoveRel);
  axisMotion->addWidget(btnJogFwd);
  axisMotion->addWidget(btnJogBwd);
  axisRoot->addLayout(axisMotion);
  manualLay->addWidget(axisBox);

  auto *cylBox = new QGroupBox(QStringLiteral("气缸维护"), manualBox);
  auto *cylLay = new QHBoxLayout(cylBox);
  cylCombo_ = new QComboBox(cylBox);
  cylCombo_->addItem(QStringLiteral("抓料气缸"), QStringLiteral("LM:0"));
  cylCombo_->addItem(QStringLiteral("内外径夹持"), QStringLiteral("CL:0"));
  cylCombo_->addItem(QStringLiteral("跳动夹持"), QStringLiteral("CL:1"));
  cylCombo_->addItem(QStringLiteral("长度夹持"), QStringLiteral("CL:2"));
  cylCombo_->addItem(QStringLiteral("GT2_1"), QStringLiteral("GT2:0"));
  cylCombo_->addItem(QStringLiteral("GT2_2"), QStringLiteral("GT2:1"));
  cylCombo_->addItem(QStringLiteral("GT2_3"), QStringLiteral("GT2:2"));
  cylCombo_->addItem(QStringLiteral("GT2_4"), QStringLiteral("GT2:3"));
  auto *btnCylP = new QPushButton(QStringLiteral("伸出(P)"), cylBox);
  auto *btnCylN = new QPushButton(QStringLiteral("缩回(N)"), cylBox);
  auto *btnCylReset = new QPushButton(QStringLiteral("Reset"), cylBox);
  cylLay->addWidget(new QLabel(QStringLiteral("气缸："), cylBox));
  cylLay->addWidget(cylCombo_);
  cylLay->addWidget(btnCylP);
  cylLay->addWidget(btnCylN);
  cylLay->addWidget(btnCylReset);
  manualLay->addWidget(cylBox);

  root->addWidget(manualBox);

  auto *singleStepBox = new QGroupBox(QStringLiteral("单步控制（待做）"), this);
  auto *singleLay = new QVBoxLayout(singleStepBox);
  singleLay->addWidget(new QLabel(QStringLiteral("后续将在这里接入 PLC 单步命令。当前先保留页面与边界，不在手动维护区混用。"), singleStepBox));
  root->addWidget(singleStepBox);

  auto *liveBox = new QGroupBox(QStringLiteral("实时状态"), this);
  auto *liveLay = new QHBoxLayout(liveBox);
  auto *axisStateBox = new QGroupBox(QStringLiteral("轴状态"), liveBox);
  auto *axisStateLay = new QVBoxLayout(axisStateBox);
  axisStateEdit_ = new QPlainTextEdit(axisStateBox);
  axisStateEdit_->setReadOnly(true);
  axisStateLay->addWidget(axisStateEdit_);
  auto *cylStateBox = new QGroupBox(QStringLiteral("气缸状态"), liveBox);
  auto *cylStateLay = new QVBoxLayout(cylStateBox);
  cylStateEdit_ = new QPlainTextEdit(cylStateBox);
  cylStateEdit_->setReadOnly(true);
  cylStateLay->addWidget(cylStateEdit_);
  liveLay->addWidget(axisStateBox, 1);
  liveLay->addWidget(cylStateBox, 1);
  root->addWidget(liveBox, 1);

  auto *logBox = new QGroupBox(QStringLiteral("维护日志"), this);
  auto *logLay = new QVBoxLayout(logBox);
  logEdit_ = new QPlainTextEdit(logBox);
  logEdit_->setReadOnly(true);
  logLay->addWidget(logEdit_);
  root->addWidget(logBox, 1);

  connect(btnWriteMode, &QPushButton::clicked, this, [this]() { emit requestSetPlcMode(selectedTargetMode()); });
  connect(plcFlowCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
    if (!plcFlowCombo_) return;
    emit plcFlowModeChanged(plcFlowCombo_->itemData(index).toInt());
  });
  connect(btnPlcPoll, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcPollOnce);
  connect(btnPlcReloadIds, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcReloadSlotIds);
  connect(btnPlcContinue, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcContinueAfterIdCheck);
  connect(btnPlcRescan, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcRequestRescanIds);
  connect(btnPlcReadMailbox, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcReadMailbox);
  connect(btnPlcAck, &QPushButton::clicked, this, &ManualMaintainWidget::requestPlcAckMailbox);

  auto emitAxis = [&](const QString &action) { emit requestAxisCommand(axisCombo_ ? axisCombo_->currentData().toInt() : 0, action); };
  connect(btnEnableOn, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("ENABLE_ON")); });
  connect(btnEnableOff, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("ENABLE_OFF")); });
  connect(btnReset, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("RESET")); });
  connect(btnHome, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("HOME")); });
  connect(btnEStop, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("ESTOP")); });
  connect(btnStop, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("STOP")); });
  connect(btnMoveAbs, &QPushButton::clicked, this, [this]() {
    emit requestAxisMove(axisCombo_ ? axisCombo_->currentData().toInt() : 0,
                         QStringLiteral("MOVE_ABS"),
                         spAcc_ ? spAcc_->value() : 0.0,
                         spDec_ ? spDec_->value() : 0.0,
                         spPos_ ? spPos_->value() : 0.0,
                         spVel_ ? spVel_->value() : 0.0);
  });
  connect(btnMoveRel, &QPushButton::clicked, this, [this]() {
    emit requestAxisMove(axisCombo_ ? axisCombo_->currentData().toInt() : 0,
                         QStringLiteral("MOVE_REL"),
                         spAcc_ ? spAcc_->value() : 0.0,
                         spDec_ ? spDec_->value() : 0.0,
                         spPos_ ? spPos_->value() : 0.0,
                         spVel_ ? spVel_->value() : 0.0);
  });
  connect(btnJogFwd, &QPushButton::pressed, this, [this]() {
    emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_FWD"), true);
  });
  connect(btnJogFwd, &QPushButton::released, this, [this]() {
    emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_FWD"), false);
  });
  connect(btnJogBwd, &QPushButton::pressed, this, [this]() {
    emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_BWD"), true);
  });
  connect(btnJogBwd, &QPushButton::released, this, [this]() {
    emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_BWD"), false);
  });

  auto emitCyl = [&](const QString &action) {
    const QString data = cylCombo_ ? cylCombo_->currentData().toString() : QStringLiteral("LM:0");
    const QStringList parts = data.split(':');
    emit requestCylinderCommand(parts.value(0), parts.value(1).toInt(), action);
  };
  connect(btnCylP, &QPushButton::clicked, this, [=]() { emitCyl(QStringLiteral("P")); });
  connect(btnCylN, &QPushButton::clicked, this, [=]() { emitCyl(QStringLiteral("N")); });
  connect(btnCylReset, &QPushButton::clicked, this, [=]() { emitCyl(QStringLiteral("RESET")); });
}

int ManualMaintainWidget::selectedPartType() const {
  return cmdPartTypeCombo_ ? cmdPartTypeCombo_->currentData().toInt() : 2;
}

int ManualMaintainWidget::selectedTargetMode() const {
  return targetModeCombo_ ? targetModeCombo_->currentData().toInt() : 1;
}

void ManualMaintainWidget::setPlcFlowMode(int mode) {
  if (!plcFlowCombo_) return;
  for (int i = 0; i < plcFlowCombo_->count(); ++i) {
    if (plcFlowCombo_->itemData(i).toInt() == mode) {
      const QSignalBlocker blocker(plcFlowCombo_);
      plcFlowCombo_->setCurrentIndex(i);
      break;
    }
  }
}

void ManualMaintainWidget::setCurrentPlcMode(int mode) {
  if (lbCurrentMode_) lbCurrentMode_->setText(QStringLiteral("当前PLC模式：%1").arg(plcModeText(mode)));
}

void ManualMaintainWidget::setRuntimeSummary(bool connected, const QString &machineText, const QString &stepText) {
  if (lbConn_) lbConn_->setText(QStringLiteral("连接：%1").arg(connected ? QStringLiteral("已连接") : QStringLiteral("未连接")));
  if (lbMachine_) lbMachine_->setText(QStringLiteral("机器：%1").arg(machineText.isEmpty() ? QStringLiteral("-") : machineText));
  if (lbStep_) lbStep_->setText(QStringLiteral("步骤：%1").arg(stepText.isEmpty() ? QStringLiteral("-") : stepText));
}

void ManualMaintainWidget::setAxisStatesText(const QString &text) {
  if (axisStateEdit_) axisStateEdit_->setPlainText(text);
}

void ManualMaintainWidget::setCylinderStatesText(const QString &text) {
  if (cylStateEdit_) cylStateEdit_->setPlainText(text);
}

void ManualMaintainWidget::appendLog(const QString &text) {
  if (logEdit_) logEdit_->appendPlainText(text);
}
