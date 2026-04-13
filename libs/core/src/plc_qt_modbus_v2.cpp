#include "core/plc_qt_modbus_v2.hpp"

#include <QEventLoop>
#include <QModbusDataUnit>
#include <QModbusDevice>
#include <QModbusReply>
#include <QModbusTcpClient>
#include <QPointer>
#include <QTimer>

#include "core/plc_contract_v2.hpp"

namespace core {
namespace {

void failWith(QString *err, const QString &message) {
  if (err) {
    *err = message;
  }
}

bool requireNonNegative(const QString &name, int value, QString *err) {
  if (value < 0) {
    failWith(err, QStringLiteral("%1 不能为负数：%2").arg(name).arg(value));
    return false;
  }
  return true;
}

} // namespace

bool buildPlcAddressLayoutV2(const PlcConfig &cfg,
                             PlcAddressLayoutV2 *out,
                             QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcAddressLayoutV2.out 不能为空"));
    return false;
  }

  PlcAddressLayoutV2 layout;
  layout.status = {cfg.status_start_address, static_cast<quint16>(kStatusBlockRegsV25), QStringLiteral("status")};
  layout.tray = {cfg.tray_start_address, static_cast<quint16>(kTrayAllCodingRegsV25), QStringLiteral("tray")};
  layout.command = {cfg.command_start_address, static_cast<quint16>(kCommandBlockRegsV25), QStringLiteral("command")};
  layout.mailbox = {cfg.mailbox_start_address, static_cast<quint16>(kMailboxTotalRegsV25), QStringLiteral("mailbox")};
  layout.pc_ack = {cfg.pc_ack_start_address, static_cast<quint16>(kPcAckWriteRegsV2), QStringLiteral("pc_ack")};

  if (!layout.isValid(err)) {
    return false;
  }

  *out = layout;
  return true;
}

QtModbusTcpRegisterClientV2::QtModbusTcpRegisterClientV2(QObject *parent)
    : QObject(parent), client_(new QModbusTcpClient(this)) {
}

QtModbusTcpRegisterClientV2::~QtModbusTcpRegisterClientV2() = default;

bool QtModbusTcpRegisterClientV2::applyConfig(const PlcConfig &cfg, QString *err) {
  if (cfg.host.trimmed().isEmpty()) {
    failWith(err, QStringLiteral("PLC host 不能为空"));
    return false;
  }
  if (cfg.port <= 0 || cfg.port > 65535) {
    failWith(err, QStringLiteral("PLC port 非法：%1").arg(cfg.port));
    return false;
  }
  if (!requireNonNegative(QStringLiteral("connect_timeout_ms"), cfg.connect_timeout_ms, err) ||
      !requireNonNegative(QStringLiteral("response_timeout_ms"), cfg.response_timeout_ms, err) ||
      !requireNonNegative(QStringLiteral("number_of_retries"), cfg.number_of_retries, err) ||
      !requireNonNegative(QStringLiteral("poll_interval_ms"), cfg.poll_interval_ms, err) ||
      !requireNonNegative(QStringLiteral("reconnect_interval_ms"), cfg.reconnect_interval_ms, err)) {
    return false;
  }
  if (cfg.server_address <= 0 || cfg.server_address > 247) {
    failWith(err, QStringLiteral("PLC server_address 非法：%1（常见范围 1..247）").arg(cfg.server_address));
    return false;
  }

  cfg_ = cfg;
  refreshClientParameters();
  return true;
}

void QtModbusTcpRegisterClientV2::refreshClientParameters() {
  if (!client_) {
    return;
  }
  client_->setConnectionParameter(QModbusDevice::NetworkAddressParameter, cfg_.host);
  client_->setConnectionParameter(QModbusDevice::NetworkPortParameter, cfg_.port);
  client_->setTimeout(cfg_.response_timeout_ms);
  client_->setNumberOfRetries(cfg_.number_of_retries);
}

bool QtModbusTcpRegisterClientV2::waitForConnected(QString *err) {
  if (!client_) {
    failWith(err, QStringLiteral("QModbusTcpClient 未创建"));
    return false;
  }
  if (client_->state() == QModbusDevice::ConnectedState) {
    return true;
  }

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  bool timeout = false;

  const auto c1 = QObject::connect(client_, &QModbusDevice::stateChanged,
                                   &loop, [&](QModbusDevice::State state) {
    if (state == QModbusDevice::ConnectedState || state == QModbusDevice::UnconnectedState) {
      loop.quit();
    }
  });
  const auto c2 = QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
    timeout = true;
    loop.quit();
  });

  timer.start(cfg_.connect_timeout_ms > 0 ? cfg_.connect_timeout_ms : 3000);
  loop.exec();

  QObject::disconnect(c1);
  QObject::disconnect(c2);

  if (client_->state() == QModbusDevice::ConnectedState) {
    return true;
  }
  if (timeout) {
    failWith(err, QStringLiteral("连接 PLC 超时：%1:%2").arg(cfg_.host).arg(cfg_.port));
    return false;
  }

  failWith(err, QStringLiteral("连接 PLC 失败：%1").arg(client_->errorString()));
  return false;
}

bool QtModbusTcpRegisterClientV2::connectToPlc(QString *err) {
  if (!client_) {
    failWith(err, QStringLiteral("QModbusTcpClient 未创建"));
    return false;
  }
  refreshClientParameters();
  if (client_->state() == QModbusDevice::ConnectedState) {
    return true;
  }
  if (!client_->connectDevice()) {
    failWith(err, QStringLiteral("connectDevice 失败：%1").arg(client_->errorString()));
    return false;
  }
  return waitForConnected(err);
}

void QtModbusTcpRegisterClientV2::disconnectFromPlc() {
  if (client_) {
    client_->disconnectDevice();
  }
}

bool QtModbusTcpRegisterClientV2::ensureConnected(QString *err) {
  if (isConnected()) {
    return true;
  }
  return connectToPlc(err);
}

bool QtModbusTcpRegisterClientV2::isConnected() const {
  return client_ && client_->state() == QModbusDevice::ConnectedState;
}

QString QtModbusTcpRegisterClientV2::lastErrorString() const {
  return client_ ? client_->errorString() : QStringLiteral("QModbusTcpClient 未创建");
}

bool QtModbusTcpRegisterClientV2::waitForReadReply(quint32 start_address,
                                                   quint16 reg_count,
                                                   QVector<quint16> *out,
                                                   QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("waitForReadReply.out 不能为空"));
    return false;
  }

  QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters,
                       static_cast<int>(start_address),
                       static_cast<qsizetype>(reg_count));
  QPointer<QModbusReply> reply = client_->sendReadRequest(unit, cfg_.server_address);
  if (!reply) {
    failWith(err, QStringLiteral("发送读请求失败：%1").arg(client_->errorString()));
    return false;
  }

  QEventLoop loop;
  QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QModbusDevice::NoError) {
    failWith(err, QStringLiteral("读取 PLC 寄存器失败 start=%1 count=%2：%3")
                      .arg(start_address)
                      .arg(reg_count)
                      .arg(reply->errorString()));
    reply->deleteLater();
    return false;
  }

  const QModbusDataUnit result = reply->result();
  QVector<quint16> regs;
  regs.reserve(result.valueCount());
  for (uint i = 0; i < result.valueCount(); ++i) {
    regs.push_back(result.value(i));
  }
  reply->deleteLater();

  if (regs.size() != reg_count) {
    failWith(err, QStringLiteral("读取 PLC 寄存器数量不匹配 start=%1：期望 %2，实际 %3")
                      .arg(start_address)
                      .arg(reg_count)
                      .arg(regs.size()));
    return false;
  }

  *out = regs;
  return true;
}

bool QtModbusTcpRegisterClientV2::waitForWriteReply(quint32 start_address,
                                                    const QVector<quint16> &values,
                                                    QString *err) {
  QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters,
                       static_cast<int>(start_address),
                       values.size());
  for (int i = 0; i < values.size(); ++i) {
    unit.setValue(i, values[i]);
  }

  QPointer<QModbusReply> reply = client_->sendWriteRequest(unit, cfg_.server_address);
  if (!reply) {
    failWith(err, QStringLiteral("发送写请求失败：%1").arg(client_->errorString()));
    return false;
  }

  QEventLoop loop;
  QObject::connect(reply, &QModbusReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  if (reply->error() != QModbusDevice::NoError) {
    failWith(err, QStringLiteral("写 PLC 寄存器失败 start=%1 count=%2：%3")
                      .arg(start_address)
                      .arg(values.size())
                      .arg(reply->errorString()));
    reply->deleteLater();
    return false;
  }

  reply->deleteLater();
  return true;
}

bool QtModbusTcpRegisterClientV2::readHoldingRegisters(quint32 start_address,
                                                       quint16 reg_count,
                                                       QVector<quint16> *out,
                                                       QString *err) {
  if (reg_count <= 0) {
    failWith(err, QStringLiteral("readHoldingRegisters.reg_count 必须 > 0"));
    return false;
  }
  if (!ensureConnected(err)) {
    return false;
  }
  return waitForReadReply(start_address, reg_count, out, err);
}

bool QtModbusTcpRegisterClientV2::writeHoldingRegisters(quint32 start_address,
                                                        const QVector<quint16> &values,
                                                        QString *err) {
  if (values.isEmpty()) {
    failWith(err, QStringLiteral("writeHoldingRegisters.values 不能为空"));
    return false;
  }
  if (!ensureConnected(err)) {
    return false;
  }
  return waitForWriteReply(start_address, values, err);
}

} // namespace core
