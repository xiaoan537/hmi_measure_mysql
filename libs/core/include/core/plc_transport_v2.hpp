#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

namespace core {

struct PlcRegisterSpanV2 {
  quint32 start_address = 0;
  quint16 reg_count = 0;
  QString name;

  bool isValid(QString *err = nullptr) const;
};

// PLC 通讯地址布局（v2.6）。
struct PlcAddressLayoutV2 {
  PlcRegisterSpanV2 status;
  PlcRegisterSpanV2 tray;
  PlcRegisterSpanV2 command;
  PlcRegisterSpanV2 mailbox;
  PlcRegisterSpanV2 pc_ack;

  bool isValid(QString *err = nullptr) const;
};

// 抽象通讯接口。
// 当前阶段只定义最小读写能力，不绑定具体 Modbus/TCP 库。
class IPlcRegisterClientV2 {
public:
  virtual ~IPlcRegisterClientV2() = default;

  virtual bool readHoldingRegisters(quint32 start_address, quint16 reg_count,
                                    QVector<quint16> *out,
                                    QString *err = nullptr) = 0;

  virtual bool writeHoldingRegisters(quint32 start_address,
                                     const QVector<quint16> &values,
                                     QString *err = nullptr) = 0;
};

} // namespace core
