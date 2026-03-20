#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

#include "core/plc_polling_v2.hpp"

namespace core {

// Command Block 中真正由 PC 写入 PLC 的寄存器长度：
// cmd_code(1) + cmd_seq(2) + cmd_arg0(2) + cmd_arg1(2) = 7 regs。
constexpr int kCommandWriteRegsV2 = 7;
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

struct PlcPollRunResultV2 {
  PlcPollPlanV2 bootstrap_plan; // 第一阶段：固定读取 Status + Command
  PlcPollPlanV2 final_plan;     // 第二阶段：按当前状态决定是否补读 Tray / Mailbox
  PlcRegisterWindowV2 window;
  PlcPollStepResultV2 step;
};

bool readPlcRegisterSpanV2(IPlcRegisterClientV2 *client,
                           const PlcRegisterSpanV2 &span,
                           QVector<quint16> *out,
                           QString *err = nullptr);

bool readPlcRegisterWindowByPlanV2(IPlcRegisterClientV2 *client,
                                   const PlcAddressLayoutV2 &layout,
                                   const PlcPollPlanV2 &plan,
                                   PlcRegisterWindowV2 *out,
                                   QString *err = nullptr);

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

// 执行“一拍完整轮询”的骨架：
// 1) 先读 Status + Command
// 2) 根据当前 Status 决定是否补读 Tray / Mailbox
// 3) 解码并做事件去重
bool runPlcPollCycleV2(IPlcRegisterClientV2 *client,
                       const PlcAddressLayoutV2 &layout,
                       PlcPollCacheV2 *cache,
                       PlcPollRunResultV2 *out,
                       QString *err = nullptr);

} // namespace core
