#pragma once

#include <QWidget>
#include <QVariantMap>

#include "core/config.hpp"

class QLabel;
class QRadioButton;
class QListWidget;
class QPushButton;

class CalibrationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalibrationWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);

    void setPlcConnected(bool ok);
    void setStepState(quint16 step);
    void setTrayPresentMask(quint16 mask);
    void setMailboxReady(bool ready);
    void setMasterPartIds(const QString &aPartId, const QString &bPartId);

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);
    void requestReadMailbox();
    void requestAckMailbox();

private:
    QString selectedMasterTypeText() const;
    quint32 selectedMasterTypeArg() const;
    void refreshSlot15State();
    QString stepText(quint16 step) const;

    core::AppConfig cfg_;
    QLabel *lblConnPlc_ = nullptr;
    QLabel *lblStep_ = nullptr;
    QLabel *lblSlot15_ = nullptr;
    QLabel *lblMasterA_ = nullptr;
    QLabel *lblMasterB_ = nullptr;
    QListWidget *listMessages_ = nullptr;
    QRadioButton *rbA_ = nullptr;
    QRadioButton *rbB_ = nullptr;
    QPushButton *btnStart_ = nullptr;
    quint16 trayPresentMask_ = 0;
};
