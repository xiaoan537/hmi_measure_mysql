#include "raw_viewer_widget.hpp"
#include "ui_raw_viewer_widget.h"

#include <QFileDialog>

RawViewerWidget::RawViewerWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::RawViewerWidget), cfg_(cfg) {
  ui_->setupUi(this);

  connect(ui_->btnBrowse, &QPushButton::clicked, this, [this](){
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 RAW 文件"), QString(), QStringLiteral("RAW v2 (*.raw *.bin);;All (*.*)"));
    if(path.isEmpty()) return;
    ui_->editPath->setText(path);
    emit requestOpenRaw(path);
  });

  connect(ui_->btnOpen, &QPushButton::clicked, this, [this](){
    QString path = ui_->editPath->text().trimmed();
    if(path.isEmpty()) return;
    emit requestOpenRaw(path);
  });
}

RawViewerWidget::~RawViewerWidget(){ delete ui_; }

void RawViewerWidget::setRawPath(const QString& path){ ui_->editPath->setText(path); }
void RawViewerWidget::setMetaJson(const QString& jsonText){ ui_->txtMeta->setPlainText(jsonText); }
void RawViewerWidget::setSummaryText(const QString& text){ ui_->txtSummary->setPlainText(text); }
