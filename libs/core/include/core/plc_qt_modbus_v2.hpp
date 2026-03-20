#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QVector>

#include "core/config.hpp"
#include "core/plc_transport_v2.hpp"

QT_BEGIN_NAMESPACE
class QModbusTcpClient;
QT_END_NAMESPACE

namespace core {

bool buildPlcAddressLayoutV2(const PlcConfig &cfg,
                             PlcAddressLayoutV2 *out,
                             QString *err = nullptr);

class QtModbusTcpRegisterClientV2 : public QObject, public IPlcRegisterClientV2 {
  Q_OBJECT
public:
  explicit QtModbusTcpRegisterClientV2(QObject *parent = nullptr);
  ~QtModbusTcpRegisterClientV2() override;

  bool applyConfig(const PlcConfig &cfg, QString *err = nullptr);
  const PlcConfig &config() const { return cfg_; }

  bool connectToPlc(QString *err = nullptr);
  void disconnectFromPlc();
  bool ensureConnected(QString *err = nullptr);

  bool isConnected() const;
  QString lastErrorString() const;

  bool readHoldingRegisters(quint32 start_address, quint16 reg_count,
                            QVector<quint16> *out,
                            QString *err = nullptr) override;

  bool writeHoldingRegisters(quint32 start_address,
                             const QVector<quint16> &values,
                             QString *err = nullptr) override;

private:
  bool waitForConnected(QString *err);
  bool waitForReadReply(quint32 start_address,
                        quint16 reg_count,
                        QVector<quint16> *out,
                        QString *err);
  bool waitForWriteReply(quint32 start_address,
                         const QVector<quint16> &values,
                         QString *err);
  void refreshClientParameters();

private:
  PlcConfig cfg_;
  QModbusTcpClient *client_ = nullptr;
};

} // namespace core
