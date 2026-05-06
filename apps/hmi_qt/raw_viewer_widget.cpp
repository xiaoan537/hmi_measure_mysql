#include "raw_viewer_widget.hpp"
#include "ui_raw_viewer_widget.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

RawViewerWidget::RawViewerWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::RawViewerWidget), cfg_(cfg) {
  ui_->setupUi(this);

  connect(ui_->btnBrowse, &QPushButton::clicked, this, [this](){
    QString startDir = ui_->editPath->text().trimmed();
    if (!startDir.isEmpty()) {
      QFileInfo fi(startDir);
      startDir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    }
    if (startDir.isEmpty()) {
      startDir = QFileInfo(cfg_.paths.raw_dir).absoluteFilePath();
    }
    QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 RAW 文件"),
        startDir,
        QStringLiteral("RAW v2 (*.rawbin *.raw *.bin);;所有文件 (*)"));
    if(path.isEmpty()) return;
    ui_->editPath->setText(path);
    emit requestOpenRaw(path);
  });

  connect(ui_->btnOpen, &QPushButton::clicked, this, [this](){
    QString path = ui_->editPath->text().trimmed();
    if(path.isEmpty()) return;
    emit requestOpenRaw(path);
  });

  connect(ui_->btnCompute, &QPushButton::clicked, this, [this](){
    emit requestComputeRaw();
  });

  setComputeEnabled(false);
}

RawViewerWidget::~RawViewerWidget(){ delete ui_; }

void RawViewerWidget::setRawPath(const QString& path){ ui_->editPath->setText(path); }
void RawViewerWidget::setMetaJson(const QString& jsonText){ ui_->txtMeta->setPlainText(jsonText); }
void RawViewerWidget::setSummaryText(const QString& text){ ui_->txtSummary->setPlainText(text); }
void RawViewerWidget::setReplayResultText(const QString& text){ ui_->txtReplayResult->setPlainText(text); }
void RawViewerWidget::setComputeEnabled(bool enabled){ ui_->btnCompute->setEnabled(enabled); }
