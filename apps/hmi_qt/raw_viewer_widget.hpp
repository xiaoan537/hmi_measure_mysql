#pragma once
#include <QWidget>
#include "core/config.hpp"

namespace Ui { class RawViewerWidget; }

class RawViewerWidget : public QWidget {
  Q_OBJECT
public:
  explicit RawViewerWidget(const core::AppConfig& cfg, QWidget* parent=nullptr);
  ~RawViewerWidget() override;

signals:
  void requestOpenRaw(const QString& path);
  void requestComputeRaw();

public slots:
  void setRawPath(const QString& path);
  void setMetaJson(const QString& jsonText);
  void setSummaryText(const QString& text);
  void setReplayResultText(const QString& text);
  void setComputeEnabled(bool enabled);

private:
  Ui::RawViewerWidget* ui_{};
  core::AppConfig cfg_;
};
