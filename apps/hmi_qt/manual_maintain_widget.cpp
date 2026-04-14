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
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFrame>
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
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(10);

  auto makeTag = [this](const QString &text) {
    auto *lb = new QLabel(text, this);
    lb->setStyleSheet(QStringLiteral("QLabel{background:#f8fafc;border:1px solid #dbe4ee;border-radius:8px;padding:6px 10px;font-weight:600;}"));
    return lb;
  };
  auto makeBtn = [this](const QString &text) {
    auto *btn = new QPushButton(text, this);
    btn->setMinimumSize(96, 34);
    btn->setMaximumHeight(36);
    return btn;
  };

  auto *statusBox = new QGroupBox(QStringLiteral("当前状态"), this);
  auto *statusLay = new QHBoxLayout(statusBox);
  statusLay->setSpacing(8);
  lbConn_ = makeTag(QStringLiteral("连接：-"));
  lbMachine_ = makeTag(QStringLiteral("机器：-"));
  lbStep_ = makeTag(QStringLiteral("步骤：-"));
  lbCurrentMode_ = makeTag(QStringLiteral("当前PLC模式：-"));
  statusLay->addWidget(lbConn_);
  statusLay->addWidget(lbMachine_);
  statusLay->addWidget(lbStep_);
  statusLay->addWidget(lbCurrentMode_, 1);
  root->addWidget(statusBox);

  auto *manualBox = new QGroupBox(QStringLiteral("手动控制 / 维护"), this);
  auto *manualRoot = new QVBoxLayout(manualBox);
  manualRoot->setSpacing(10);

  auto *manualTip = new QLabel(QStringLiteral("本页用于轴与气缸的维护调试。生产流程命令、模式选择、A/B型选择与PLC联调按钮已迁移到生产页。"), manualBox);
  manualTip->setWordWrap(true);
  manualTip->setStyleSheet(QStringLiteral("QLabel{color:#475569;}"));
  manualRoot->addWidget(manualTip);

  auto *maintGrid = new QGridLayout();
  maintGrid->setHorizontalSpacing(10);
  maintGrid->setVerticalSpacing(10);

  auto *axisBox = new QGroupBox(QStringLiteral("单轴维护"), manualBox);
  auto *axisRoot = new QVBoxLayout(axisBox);
  axisRoot->setSpacing(8);
  auto *axisHead = new QHBoxLayout();
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
  axisCombo_->setMinimumWidth(240);
  axisHead->addWidget(new QLabel(QStringLiteral("选择轴："), axisBox));
  axisHead->addWidget(axisCombo_, 1);
  axisRoot->addLayout(axisHead);

  auto *paramGrid = new QGridLayout();
  spAcc_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spDec_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spPos_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  spVel_ = makeSpinBox(-1000000, 1000000, 1.0, 3, axisBox);
  for (auto *sb : {spAcc_, spDec_, spPos_, spVel_}) { sb->setMinimumHeight(30); sb->setMinimumWidth(110); }
  spVel_->setValue(10.0);
  paramGrid->addWidget(new QLabel(QStringLiteral("加速度"), axisBox), 0, 0);
  paramGrid->addWidget(spAcc_, 0, 1);
  paramGrid->addWidget(new QLabel(QStringLiteral("减速度"), axisBox), 0, 2);
  paramGrid->addWidget(spDec_, 0, 3);
  paramGrid->addWidget(new QLabel(QStringLiteral("目标位置"), axisBox), 1, 0);
  paramGrid->addWidget(spPos_, 1, 1);
  paramGrid->addWidget(new QLabel(QStringLiteral("目标速度"), axisBox), 1, 2);
  paramGrid->addWidget(spVel_, 1, 3);
  axisRoot->addLayout(paramGrid);

  auto *axisBtns1 = new QGridLayout();
  auto *btnEnableOn = makeBtn(QStringLiteral("使能开"));
  auto *btnEnableOff = makeBtn(QStringLiteral("使能关"));
  auto *btnReset = makeBtn(QStringLiteral("复位"));
  auto *btnEStop = makeBtn(QStringLiteral("急停"));
  auto *btnStop = makeBtn(QStringLiteral("停止"));
  auto *btnMoveAbs = makeBtn(QStringLiteral("绝对定位"));
  auto *btnMoveRel = makeBtn(QStringLiteral("相对定位"));
  auto *btnJogFwd = makeBtn(QStringLiteral("正向点动"));
  auto *btnJogBwd = makeBtn(QStringLiteral("反向点动"));
  axisBtns1->addWidget(btnEnableOn, 0, 0);
  axisBtns1->addWidget(btnEnableOff, 0, 1);
  axisBtns1->addWidget(btnReset, 0, 2);
  axisBtns1->addWidget(btnEStop, 1, 0);
  axisBtns1->addWidget(btnStop, 1, 1);
  axisBtns1->addWidget(btnMoveAbs, 1, 2);
  axisBtns1->addWidget(btnMoveRel, 2, 0);
  axisBtns1->addWidget(btnJogFwd, 2, 1);
  axisBtns1->addWidget(btnJogBwd, 2, 2);
  axisRoot->addLayout(axisBtns1);

  auto *cylBox = new QGroupBox(QStringLiteral("气缸维护"), manualBox);
  auto *cylRoot = new QVBoxLayout(cylBox);
  auto *cylRow = new QHBoxLayout();
  cylCombo_ = new QComboBox(cylBox);
  cylCombo_->addItem(QStringLiteral("抓料气缸"), QStringLiteral("LM:0"));
  cylCombo_->addItem(QStringLiteral("内外径夹持"), QStringLiteral("CL:0"));
  cylCombo_->addItem(QStringLiteral("跳动夹持"), QStringLiteral("CL:1"));
  cylCombo_->addItem(QStringLiteral("长度夹持"), QStringLiteral("CL:2"));
  cylCombo_->addItem(QStringLiteral("GT2_1"), QStringLiteral("GT2:0"));
  cylCombo_->addItem(QStringLiteral("GT2_2"), QStringLiteral("GT2:1"));
  cylCombo_->addItem(QStringLiteral("GT2_3"), QStringLiteral("GT2:2"));
  cylCombo_->addItem(QStringLiteral("GT2_4"), QStringLiteral("GT2:3"));
  cylCombo_->setMinimumWidth(200);
  cylRow->addWidget(new QLabel(QStringLiteral("选择气缸："), cylBox));
  cylRow->addWidget(cylCombo_, 1);
  cylRoot->addLayout(cylRow);
  auto *cylBtns = new QGridLayout();
  auto *btnCylP = makeBtn(QStringLiteral("伸出"));
  auto *btnCylN = makeBtn(QStringLiteral("缩回"));
  auto *btnCylReset = makeBtn(QStringLiteral("复位"));
  cylBtns->addWidget(btnCylP, 0, 0);
  cylBtns->addWidget(btnCylN, 0, 1);
  cylBtns->addWidget(btnCylReset, 0, 2);
  cylRoot->addLayout(cylBtns);

  maintGrid->addWidget(axisBox, 0, 0);
  maintGrid->addWidget(cylBox, 0, 1);
  maintGrid->setColumnStretch(0, 2);
  maintGrid->setColumnStretch(1, 1);
  manualRoot->addLayout(maintGrid);
  root->addWidget(manualBox);

  auto *singleStepBox = new QGroupBox(QStringLiteral("单步控制（待做）"), this);
  auto *singleLay = new QVBoxLayout(singleStepBox);
  singleLay->addWidget(new QLabel(QStringLiteral("后续将在这里接入 PLC 单步命令。当前先保留页面与边界，不在手动维护区混用。"), singleStepBox));
  root->addWidget(singleStepBox);

  auto *liveBox = new QGroupBox(QStringLiteral("实时状态"), this);
  auto *liveLay = new QHBoxLayout(liveBox);
  axisStateTable_ = new QTableWidget(0, 9, liveBox);
  axisStateTable_->setHorizontalHeaderLabels(QStringList{QStringLiteral("轴名称"), QStringLiteral("使能"), QStringLiteral("回零"), QStringLiteral("错误"), QStringLiteral("忙"), QStringLiteral("完成"), QStringLiteral("错误ID"), QStringLiteral("位置"), QStringLiteral("速度")});
  axisStateTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  axisStateTable_->verticalHeader()->setVisible(false);
  axisStateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  axisStateTable_->setSelectionMode(QAbstractItemView::NoSelection);
  axisStateTable_->setAlternatingRowColors(true);

  cylStateTable_ = new QTableWidget(0, 5, liveBox);
  cylStateTable_->setHorizontalHeaderLabels(QStringList{QStringLiteral("气缸名称"), QStringLiteral("伸出到位"), QStringLiteral("缩回到位"), QStringLiteral("错误"), QStringLiteral("错误ID")});
  cylStateTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  cylStateTable_->verticalHeader()->setVisible(false);
  cylStateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  cylStateTable_->setSelectionMode(QAbstractItemView::NoSelection);
  cylStateTable_->setAlternatingRowColors(true);

  auto *axisStateBox = new QGroupBox(QStringLiteral("轴状态"), liveBox);
  auto *axisStateLay = new QVBoxLayout(axisStateBox);
  axisStateLay->addWidget(axisStateTable_);
  auto *cylStateBox = new QGroupBox(QStringLiteral("气缸状态"), liveBox);
  auto *cylStateLay = new QVBoxLayout(cylStateBox);
  cylStateLay->addWidget(cylStateTable_);
  liveLay->addWidget(axisStateBox, 3);
  liveLay->addWidget(cylStateBox, 2);
  root->addWidget(liveBox, 1);

  auto *logBox = new QGroupBox(QStringLiteral("维护日志"), this);
  auto *logLay = new QVBoxLayout(logBox);
  logEdit_ = new QPlainTextEdit(logBox);
  logEdit_->setReadOnly(true);
  logLay->addWidget(logEdit_);
  root->addWidget(logBox, 1);

  auto emitAxis = [&](const QString &action) { emit requestAxisCommand(axisCombo_ ? axisCombo_->currentData().toInt() : 0, action); };
  connect(btnEnableOn, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("ENABLE_ON")); });
  connect(btnEnableOff, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("ENABLE_OFF")); });
  connect(btnReset, &QPushButton::clicked, this, [=]() { emitAxis(QStringLiteral("RESET")); });
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
  connect(btnJogFwd, &QPushButton::pressed, this, [this]() { emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_FWD"), true); });
  connect(btnJogFwd, &QPushButton::released, this, [this]() { emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_FWD"), false); });
  connect(btnJogBwd, &QPushButton::pressed, this, [this]() { emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_BWD"), true); });
  connect(btnJogBwd, &QPushButton::released, this, [this]() { emit requestAxisJog(axisCombo_ ? axisCombo_->currentData().toInt() : 0, QStringLiteral("JOG_BWD"), false); });

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
  return 2;
}

int ManualMaintainWidget::selectedTargetMode() const {
  return 1;
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
  if (!axisStateTable_) return;
  axisStateTable_->setRowCount(0);
  const QStringList lines = text.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    const QStringList parts = line.split('|');
    if (parts.isEmpty()) continue;
    const int row = axisStateTable_->rowCount();
    axisStateTable_->insertRow(row);
    axisStateTable_->setItem(row, 0, new QTableWidgetItem(parts.at(0).trimmed()));
    QStringList kvs;
    if (parts.size() > 1) kvs = parts.at(1).split(' ', Qt::SkipEmptyParts);
    QStringList values;
    for (QString kv : kvs) {
      const int pos = kv.indexOf('=');
      values << (pos >= 0 ? kv.mid(pos + 1).trimmed() : kv.trimmed());
    }
    while (values.size() < 8) values << QStringLiteral("-");
    for (int i = 0; i < 8; ++i) {
      axisStateTable_->setItem(row, i + 1, new QTableWidgetItem(values.at(i)));
    }
  }
}

void ManualMaintainWidget::setCylinderStatesText(const QString &text) {
  if (!cylStateTable_) return;
  cylStateTable_->setRowCount(0);
  const QStringList lines = text.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    const QStringList parts = line.split('|');
    if (parts.isEmpty()) continue;
    const int row = cylStateTable_->rowCount();
    cylStateTable_->insertRow(row);
    cylStateTable_->setItem(row, 0, new QTableWidgetItem(parts.at(0).trimmed()));
    QStringList kvs;
    if (parts.size() > 1) kvs = parts.at(1).split(' ', Qt::SkipEmptyParts);
    QStringList values;
    for (QString kv : kvs) {
      const int pos = kv.indexOf('=');
      values << (pos >= 0 ? kv.mid(pos + 1).trimmed() : kv.trimmed());
    }
    while (values.size() < 4) values << QStringLiteral("-");
    for (int i = 0; i < 4; ++i) {
      cylStateTable_->setItem(row, i + 1, new QTableWidgetItem(values.at(i)));
    }
  }
}

void ManualMaintainWidget::appendLog(const QString &text) {
  if (logEdit_) logEdit_->appendPlainText(text);
}
