#include "raw_viewer_widget.hpp"
#include "ui_raw_viewer_widget.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QTableWidgetItem>

RawViewerWidget::RawViewerWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::RawViewerWidget), cfg_(cfg) {
  ui_->setupUi(this);
  ui_->tableRawPoints->setColumnCount(9);
  ui_->tableRawPoints->setHorizontalHeaderLabels(
      {QStringLiteral("通道"), QStringLiteral("圈号"), QStringLiteral("点号"),
       QStringLiteral("角度(°)"), QStringLiteral("原始值(mm)"), QStringLiteral("有效"),
       QStringLiteral("拟合半径(mm)"), QStringLiteral("点到圆心距离(mm)"),
       QStringLiteral("残差(mm)")});
  ui_->tableRawPoints->horizontalHeader()->setStretchLastSection(true);
  ui_->tableRawPoints->verticalHeader()->setVisible(false);
  ui_->tableRawPoints->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->tableRawPoints->setSelectionBehavior(QAbstractItemView::SelectRows);

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

  connect(ui_->btnExportCsv, &QPushButton::clicked, this, [this](){
    emit requestExportCsv();
  });

  setComputeEnabled(false);
  setExportEnabled(false);
}

RawViewerWidget::~RawViewerWidget(){ delete ui_; }

void RawViewerWidget::setRawPath(const QString& path){ ui_->editPath->setText(path); }
void RawViewerWidget::setMetaJson(const QString& jsonText){ ui_->txtMeta->setPlainText(jsonText); }
void RawViewerWidget::setSummaryText(const QString& text){ ui_->txtSummary->setPlainText(text); }
void RawViewerWidget::setReplayResultText(const QString& text){ ui_->txtReplayResult->setPlainText(text); }
void RawViewerWidget::setComputeEnabled(bool enabled){ ui_->btnCompute->setEnabled(enabled); }
void RawViewerWidget::setExportEnabled(bool enabled){ ui_->btnExportCsv->setEnabled(enabled); }

void RawViewerWidget::clearRawPointRows(){
  ui_->tableRawPoints->setRowCount(0);
  ui_->labelPointCount->setText(QStringLiteral("未加载 RAW 点数据"));
}

void RawViewerWidget::setRawPointRows(const QVector<QStringList>& rows){
  ui_->tableRawPoints->setSortingEnabled(false);
  ui_->tableRawPoints->setRowCount(rows.size());
  for (int r = 0; r < rows.size(); ++r) {
    const QStringList row = rows.at(r);
    for (int c = 0; c < ui_->tableRawPoints->columnCount(); ++c) {
      auto *item = new QTableWidgetItem(c < row.size() ? row.at(c) : QString());
      ui_->tableRawPoints->setItem(r, c, item);
    }
  }
  ui_->tableRawPoints->resizeColumnsToContents();
  ui_->tableRawPoints->horizontalHeader()->setStretchLastSection(true);
  ui_->labelPointCount->setText(QStringLiteral("点数据：%1 行").arg(rows.size()));
}
