#include "alarm_widget.hpp"
#include "ui_alarm_widget.h"

#include <QDateTime>

AlarmWidget::AlarmWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::AlarmWidget), cfg_(cfg) {
  ui_->setupUi(this);

  connect(ui_->btnRefresh, &QPushButton::clicked, this, &AlarmWidget::requestRefresh);
  connect(ui_->btnResetAlarm, &QPushButton::clicked, this, &AlarmWidget::requestResetAlarm);

  ui_->tableEvents->setColumnCount(3);
  ui_->tableEvents->setHorizontalHeaderLabels({"时间", "类型", "信息"});
  ui_->tableEvents->horizontalHeader()->setStretchLastSection(true);
  ui_->tableEvents->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->tableEvents->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

AlarmWidget::~AlarmWidget() { delete ui_; }

void AlarmWidget::setCurrentAlarm(int alarmCode, int alarmLevel, quint32 interlockMask) {
  ui_->lbAlarmCode->setText(QString::number(alarmCode));
  ui_->lbAlarmLevel->setText(QString::number(alarmLevel));
  ui_->lbInterlock->setText(QString("0x%1").arg(QString::number(interlockMask, 16).toUpper()));
}

void AlarmWidget::addEventRow(const QString& time, const QString& type, const QString& message) {
  int row = ui_->tableEvents->rowCount();
  ui_->tableEvents->insertRow(row);
  ui_->tableEvents->setItem(row, 0, new QTableWidgetItem(time));
  ui_->tableEvents->setItem(row, 1, new QTableWidgetItem(type));
  ui_->tableEvents->setItem(row, 2, new QTableWidgetItem(message));
  ui_->tableEvents->scrollToBottom();
}
