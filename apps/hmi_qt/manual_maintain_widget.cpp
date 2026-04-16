#include "manual_maintain_widget.hpp"
#include "manual_maintain_logic.hpp"
#include "ui_manual_maintain_widget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>

ManualMaintainWidget::ManualMaintainWidget(QWidget *parent)
    : QWidget(parent), ui_(new Ui::ManualMaintainWidget) {
  ui_->setupUi(this);

  const QString tagStyle =
      QStringLiteral("QLabel{background:#f8fafc;border:1px solid #dbe4ee;border-radius:8px;padding:6px 10px;font-weight:600;}");
  ui_->lbConn->setStyleSheet(tagStyle);
  ui_->lbMachine->setStyleSheet(tagStyle);
  ui_->lbStep->setStyleSheet(tagStyle);
  ui_->lbCurrentMode->setStyleSheet(tagStyle);
  ui_->manualTip->setStyleSheet(QStringLiteral("QLabel{color:#475569;}"));

  ui_->axisCombo->clear();
  ui_->axisCombo->addItem(QStringLiteral("龙门X轴"), 0);
  ui_->axisCombo->addItem(QStringLiteral("龙门Y轴"), 1);
  ui_->axisCombo->addItem(QStringLiteral("龙门Z轴"), 2);
  ui_->axisCombo->addItem(QStringLiteral("测量X1轴（内外径工位平移）"), 3);
  ui_->axisCombo->addItem(QStringLiteral("测量X2轴（跳动工位平移）"), 4);
  ui_->axisCombo->addItem(QStringLiteral("测量X3轴（长度工位平移）"), 5);
  ui_->axisCombo->addItem(QStringLiteral("内外径R1轴（左端旋转）"), 6);
  ui_->axisCombo->addItem(QStringLiteral("内外径R2轴（右端旋转）"), 7);
  ui_->axisCombo->addItem(QStringLiteral("跳动R3轴（左端旋转）"), 8);
  ui_->axisCombo->addItem(QStringLiteral("跳动R4轴（右端旋转）"), 9);

  ui_->cylCombo->clear();
  ui_->cylCombo->addItem(QStringLiteral("抓料气缸"), QStringLiteral("LM:0"));
  ui_->cylCombo->addItem(QStringLiteral("内外径夹持"), QStringLiteral("CL:0"));
  ui_->cylCombo->addItem(QStringLiteral("跳动夹持"), QStringLiteral("CL:1"));
  ui_->cylCombo->addItem(QStringLiteral("长度夹持"), QStringLiteral("CL:2"));
  ui_->cylCombo->addItem(QStringLiteral("GT2_1"), QStringLiteral("GT2:0"));
  ui_->cylCombo->addItem(QStringLiteral("GT2_2"), QStringLiteral("GT2:1"));
  ui_->cylCombo->addItem(QStringLiteral("GT2_3"), QStringLiteral("GT2:2"));
  ui_->cylCombo->addItem(QStringLiteral("GT2_4"), QStringLiteral("GT2:3"));
  ui_->gridMaintain->setColumnStretch(0, 2);
  ui_->gridMaintain->setColumnStretch(1, 1);

  const auto configureSpinBox = [](QDoubleSpinBox *sb, double min, double max,
                                   double step, int decimals, double value) {
    if (!sb) return;
    sb->setRange(min, max);
    sb->setDecimals(decimals);
    sb->setSingleStep(step);
    sb->setKeyboardTracking(false);
    sb->setValue(value);
  };
  configureSpinBox(ui_->spAcc, -1000000.0, 1000000.0, 1.0, 3, 0.0);
  configureSpinBox(ui_->spDec, -1000000.0, 1000000.0, 1.0, 3, 0.0);
  configureSpinBox(ui_->spPos, -1000000.0, 1000000.0, 1.0, 3, 0.0);
  configureSpinBox(ui_->spVel, -1000000.0, 1000000.0, 1.0, 3, 10.0);

  ui_->axisStateTable->setHorizontalHeaderLabels(
      QStringList{QStringLiteral("轴名称"), QStringLiteral("使能"),
                  QStringLiteral("回零"), QStringLiteral("错误"),
                  QStringLiteral("忙"), QStringLiteral("完成"),
                  QStringLiteral("错误ID"), QStringLiteral("位置"),
                  QStringLiteral("速度")});
  ui_->axisStateTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  ui_->axisStateTable->verticalHeader()->setVisible(false);
  ui_->axisStateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->axisStateTable->setSelectionMode(QAbstractItemView::NoSelection);
  ui_->axisStateTable->setAlternatingRowColors(true);

  ui_->cylStateTable->setHorizontalHeaderLabels(
      QStringList{QStringLiteral("气缸名称"), QStringLiteral("伸出到位"),
                  QStringLiteral("缩回到位"), QStringLiteral("错误"),
                  QStringLiteral("错误ID")});
  ui_->cylStateTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  ui_->cylStateTable->verticalHeader()->setVisible(false);
  ui_->cylStateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->cylStateTable->setSelectionMode(QAbstractItemView::NoSelection);
  ui_->cylStateTable->setAlternatingRowColors(true);

  auto emitAxis = [this](const QString &action) {
    emit requestAxisCommand(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                            action);
  };
  connect(ui_->btnEnableOn, &QPushButton::clicked, this,
          [=]() { emitAxis(QStringLiteral("ENABLE_ON")); });
  connect(ui_->btnEnableOff, &QPushButton::clicked, this,
          [=]() { emitAxis(QStringLiteral("ENABLE_OFF")); });
  connect(ui_->btnReset, &QPushButton::clicked, this,
          [=]() { emitAxis(QStringLiteral("RESET")); });
  connect(ui_->btnEStop, &QPushButton::clicked, this,
          [=]() { emitAxis(QStringLiteral("ESTOP")); });
  connect(ui_->btnStop, &QPushButton::clicked, this,
          [=]() { emitAxis(QStringLiteral("STOP")); });

  connect(ui_->btnMoveAbs, &QPushButton::clicked, this, [this]() {
    emit requestAxisMove(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                         QStringLiteral("MOVE_ABS"),
                         ui_->spAcc ? ui_->spAcc->value() : 0.0,
                         ui_->spDec ? ui_->spDec->value() : 0.0,
                         ui_->spPos ? ui_->spPos->value() : 0.0,
                         ui_->spVel ? ui_->spVel->value() : 0.0);
  });
  connect(ui_->btnMoveRel, &QPushButton::clicked, this, [this]() {
    emit requestAxisMove(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                         QStringLiteral("MOVE_REL"),
                         ui_->spAcc ? ui_->spAcc->value() : 0.0,
                         ui_->spDec ? ui_->spDec->value() : 0.0,
                         ui_->spPos ? ui_->spPos->value() : 0.0,
                         ui_->spVel ? ui_->spVel->value() : 0.0);
  });

  connect(ui_->btnJogFwd, &QPushButton::pressed, this, [this]() {
    emit requestAxisJog(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                        QStringLiteral("JOG_FWD"), true);
  });
  connect(ui_->btnJogFwd, &QPushButton::released, this, [this]() {
    emit requestAxisJog(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                        QStringLiteral("JOG_FWD"), false);
  });
  connect(ui_->btnJogBwd, &QPushButton::pressed, this, [this]() {
    emit requestAxisJog(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                        QStringLiteral("JOG_BWD"), true);
  });
  connect(ui_->btnJogBwd, &QPushButton::released, this, [this]() {
    emit requestAxisJog(ui_->axisCombo ? ui_->axisCombo->currentData().toInt() : 0,
                        QStringLiteral("JOG_BWD"), false);
  });

  auto emitCyl = [this](const QString &action) {
    const QString data =
        ui_->cylCombo ? ui_->cylCombo->currentData().toString()
                      : QStringLiteral("LM:0");
    const auto target = manual_maintain_logic::parseCylinderSelector(data);
    emit requestCylinderCommand(target.first, target.second, action);
  };
  connect(ui_->btnCylP, &QPushButton::clicked, this,
          [=]() { emitCyl(QStringLiteral("P")); });
  connect(ui_->btnCylN, &QPushButton::clicked, this,
          [=]() { emitCyl(QStringLiteral("N")); });
  connect(ui_->btnCylReset, &QPushButton::clicked, this,
          [=]() { emitCyl(QStringLiteral("RESET")); });
}

ManualMaintainWidget::~ManualMaintainWidget() { delete ui_; }

void ManualMaintainWidget::setCurrentPlcMode(int mode) {
  if (ui_->lbCurrentMode)
    ui_->lbCurrentMode->setText(
        QStringLiteral("当前PLC模式：%1")
            .arg(manual_maintain_logic::plcModeText(mode)));
}

void ManualMaintainWidget::setRuntimeSummary(bool connected,
                                             const QString &machineText,
                                             const QString &stepText) {
  if (ui_->lbConn)
    ui_->lbConn->setText(QStringLiteral("连接：%1")
                             .arg(connected ? QStringLiteral("已连接")
                                            : QStringLiteral("未连接")));
  if (ui_->lbMachine)
    ui_->lbMachine->setText(QStringLiteral("机器：%1")
                                .arg(machineText.isEmpty() ? QStringLiteral("-")
                                                            : machineText));
  if (ui_->lbStep)
    ui_->lbStep->setText(QStringLiteral("步骤：%1")
                             .arg(stepText.isEmpty() ? QStringLiteral("-")
                                                     : stepText));
}

void ManualMaintainWidget::setAxisStatesText(const QString &text) {
  if (!ui_->axisStateTable) return;
  ui_->axisStateTable->setRowCount(0);
  const auto rows = manual_maintain_logic::parseStateRows(text, 8);
  for (const auto &rowData : rows) {
    const int row = ui_->axisStateTable->rowCount();
    ui_->axisStateTable->insertRow(row);
    ui_->axisStateTable->setItem(row, 0, new QTableWidgetItem(rowData.name));
    for (int i = 0; i < 8; ++i) {
      ui_->axisStateTable->setItem(row, i + 1,
                                   new QTableWidgetItem(rowData.values.at(i)));
    }
  }
}

void ManualMaintainWidget::setCylinderStatesText(const QString &text) {
  if (!ui_->cylStateTable) return;
  ui_->cylStateTable->setRowCount(0);
  const auto rows = manual_maintain_logic::parseStateRows(text, 4);
  for (const auto &rowData : rows) {
    const int row = ui_->cylStateTable->rowCount();
    ui_->cylStateTable->insertRow(row);
    ui_->cylStateTable->setItem(row, 0, new QTableWidgetItem(rowData.name));
    for (int i = 0; i < 4; ++i) {
      ui_->cylStateTable->setItem(row, i + 1,
                                  new QTableWidgetItem(rowData.values.at(i)));
    }
  }
}

void ManualMaintainWidget::appendLog(const QString &text) {
  if (ui_->logEdit) ui_->logEdit->appendPlainText(text);
}
