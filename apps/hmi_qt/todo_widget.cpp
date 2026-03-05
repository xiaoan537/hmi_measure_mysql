#include "todo_widget.hpp"
#include "ui_todo_widget.h"

TodoWidget::TodoWidget(const QString& title, const QString& desc, QWidget* parent)
  : QWidget(parent), ui_(new Ui::TodoWidget) {
  ui_->setupUi(this);
  ui_->lbTitle->setText(title);
  ui_->lbDesc->setText(desc);
}

TodoWidget::~TodoWidget(){ delete ui_; }
