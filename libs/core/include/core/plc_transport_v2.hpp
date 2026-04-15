#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

#include "core/plc_contract_v2.hpp"

namespace core {

// Command Block 中真正由 PC 写入 PLC 的寄存器长度：
// cmd_code(1) + cmd_seq(2) + cmd_arg0(2) + cmd_arg1(2) = 7 regs。
constexpr int kCommandWriteRegsV2 = 2;
constexpr int kPcAckWriteRegsV2 = 1;

struct PlcRegisterSpanV2 {
  quint32 start_address = 0;
  quint16 reg_count = 0;
  QString name;

  bool isValid(QString *err = nullptr) const;
};

// 说明：当前仍不在这里固化最终寄存器地址；
// 真正的地址由你和 PLC 工程师确定后，从上层把这组 span 配进来即可。
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

bool encodePlcCommandWriteRegsV2(const PlcCommandBlockV2 &command,
                                 QVector<quint16> *out,
                                 QString *err = nullptr);

bool encodePcAckWriteRegsV2(quint16 pc_ack,
                            QVector<quint16> *out,
                            QString *err = nullptr);

bool writePlcCommandV2(IPlcRegisterClientV2 *client,
                       const PlcAddressLayoutV2 &layout,
                       const PlcCommandBlockV2 &command,
                       QString *err = nullptr);

bool writePlcPcAckV2(IPlcRegisterClientV2 *client,
                     const PlcAddressLayoutV2 &layout,
                     quint16 pc_ack,
                     QString *err = nullptr);

bool writePlcTrayPartIdSlotV2(IPlcRegisterClientV2 *client,
                              const PlcAddressLayoutV2 &layout,
                              int slotIndex,
                              const QString &partId,
                              QString *err = nullptr);

} // namespace core
