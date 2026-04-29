#include "diagnostics_widget.hpp"
#include "ui_diagnostics_widget.h"

DiagnosticsWidget::DiagnosticsWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::DiagnosticsWidget), cfg_(cfg) {
  ui_->setupUi(this);

  connect(ui_->btnRefresh, &QPushButton::clicked, this, &DiagnosticsWidget::requestRefresh);
  connect(ui_->btnReadMailbox, &QPushButton::clicked, this, &DiagnosticsWidget::requestReadMailbox);
  connect(ui_->btnAck, &QPushButton::clicked, this, &DiagnosticsWidget::requestAckMailbox);
}

DiagnosticsWidget::~DiagnosticsWidget() { delete ui_; }

void DiagnosticsWidget::setCommStats(int pollHz, int lastMs, int okCount, int errCount) {
  ui_->lbPollHz->setText(QString::number(pollHz));
  ui_->lbLastMs->setText(QString::number(lastMs));
  ui_->lbOkCount->setText(QString::number(okCount));
  ui_->lbErrCount->setText(QString::number(errCount));
}

void DiagnosticsWidget::setStatusFields(int stepState, int machineState, int alarmCode, int alarmLevel, quint32 interlockMask, int mailboxReady) {
  ui_->lbStep->setText(QString::number(stepState));
  ui_->lbMachine->setText(QString::number(machineState));
  ui_->lbAlarmCode->setText(QString::number(alarmCode));
  ui_->lbAlarmLevel->setText(QString::number(alarmLevel));
  ui_->lbInterlock->setText(QString("0x%1").arg(QString::number(interlockMask, 16).toUpper()));
  ui_->lbMailboxReady->setText(QString::number(mailboxReady));
}

void DiagnosticsWidget::setMailboxPreview(const QString& partType, const QString& slot0, const QString& slot1,
                                         const QString& partId0, const QString& partId1) {
  ui_->lbPartType->setText(partType);
  ui_->lbSlot0->setText(slot0);
  ui_->lbSlot1->setText(slot1);
  ui_->txtPartId0->setPlainText(partId0);
  ui_->txtPartId1->setPlainText(partId1);
}
