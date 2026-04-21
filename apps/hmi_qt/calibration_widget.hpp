#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QVector>

#include "core/config.hpp"
#include "core/measurement_pipeline.hpp"

class QLabel;
class QListWidget;
class QPushButton;
class QComboBox;
class QLineEdit;

class CalibrationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalibrationWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);

    void setPlcConnected(bool ok);
    void setStepState(quint16 step);
    void setAlarm(quint16 alarmCode, quint16 alarmLevel);
    void setTrayPresentMask(quint16 mask);
    void setMailboxReady(bool ready);
    void setCurrentPlcMode(int mode);
    void setMasterPartIds(const QString &aPartId, const QString &bPartId);
    void setSlotSummary(const core::CalibrationSlotSummary &s);
    void setSlotSummaries(const QVector<core::CalibrationSlotSummary> &summaries);
    void appendLogMessage(const QString &text);
    QString masterPartIdForType(QChar partType) const;

    QString selectedPartTypeText() const;
    quint32 selectedPartTypeArg() const;
    int selectedPlcModeValue() const;

signals:
    void uiCommandRequested(const QString &cmd, const QVariantMap &args);
    void requestReadMailbox();
    void requestAckMailbox();
    void requestReconnectPlc();
    void requestWriteCategoryMode(int partTypeArg);

private:
    void refreshSlot15State();
    void updateMasterIdEditability();

    core::AppConfig cfg_;
    QLabel *lblConnPlc_ = nullptr;
    QLabel *lblStep_ = nullptr;
    QLabel *lblAlarm_ = nullptr;
    QLabel *lblSlot15_ = nullptr;
    QLineEdit *editMasterA_ = nullptr;
    QLineEdit *editMasterB_ = nullptr;
    QLabel *lblSummary_ = nullptr;
    QListWidget *listMessages_ = nullptr;
    QPushButton *btnStart_ = nullptr;
    QComboBox *partTypeCombo_ = nullptr;
    QComboBox *plcModeCombo_ = nullptr;
    quint16 trayPresentMask_ = 0;
    quint16 stepState_ = 0;
    bool masterIdLockedByStart_ = false;
};
