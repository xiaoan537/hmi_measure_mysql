#pragma once
#include <QWidget>

namespace Ui { class TodoWidget; }

class TodoWidget : public QWidget {
  Q_OBJECT
public:
  explicit TodoWidget(const QString& title, const QString& desc, QWidget* parent=nullptr);
  ~TodoWidget() override;

private:
  Ui::TodoWidget* ui_{};
};
