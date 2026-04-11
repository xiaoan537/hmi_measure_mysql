#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QVector>

#include "core/config.hpp"
#include "core/measurement_pipeline.hpp"

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
    void setSlotSummary(const core::CalibrationSlotSummary &s);
    void setSlotSummaries(const QVector<core::CalibrationSlotSummary> &summaries);

    QString selectedPartTypeText() const;
    quint32 selectedPartTypeArg() const;

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);
    void requestReadMailbox();
    void requestAckMailbox();

private:
    QString selectedMasterTypeTextInternal() const;
    quint32 selectedMasterTypeArgInternal() const;
    void refreshSlot15State();
    QString stepText(quint16 step) const;

    core::AppConfig cfg_;
    QLabel *lblConnPlc_ = nullptr;
    QLabel *lblStep_ = nullptr;
    QLabel *lblSlot15_ = nullptr;
    QLabel *lblMasterA_ = nullptr;
    QLabel *lblMasterB_ = nullptr;
    QLabel *lblSummary_ = nullptr;
    QListWidget *listMessages_ = nullptr;
    QRadioButton *rbA_ = nullptr;
    QRadioButton *rbB_ = nullptr;
    QPushButton *btnStart_ = nullptr;
    quint16 trayPresentMask_ = 0;
};
