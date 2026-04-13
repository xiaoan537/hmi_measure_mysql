#include "core/plc_runtime_v2.hpp"
#include "core/measurement_pipeline.hpp"

#include <QElapsedTimer>
#include <QByteArray>
#include <QEventLoop>
#include <QTimer>

namespace core {
namespace {

void failWith(QString *err, const QString &message) {
  if (err) {
    *err = message;
  }
}

QByteArray mbBytesFromRegsRuntime(const QVector<quint16> &regs) {
  QByteArray bytes;
  bytes.reserve(regs.size() * 2);
  for (quint16 reg : regs) {
    bytes.append(static_cast<char>(reg & 0x00FFu));
    bytes.append(static_cast<char>((reg >> 8) & 0x00FFu));
  }
  return bytes;
}

QVector<quint16> regsFromMbBytesRuntime(const QByteArray &bytes) {
  QVector<quint16> regs;
  regs.reserve((bytes.size() + 1) / 2);
  for (int i = 0; i < bytes.size(); i += 2) {
    const quint16 lo = static_cast<unsigned char>(bytes.at(i));
    const quint16 hi = (i + 1 < bytes.size()) ? static_cast<unsigned char>(bytes.at(i + 1)) : 0u;
    regs.push_back(static_cast<quint16>((hi << 8) | lo));
  }
  return regs;
}

} // namespace

PlcRuntimeServiceV2::PlcRuntimeServiceV2(const AppConfig &cfg, QObject *parent)
    : QObject(parent), cfg_(cfg) {
  poll_timer_.setSingleShot(false);
  connect(&poll_timer_, &QTimer::timeout,
          this, &PlcRuntimeServiceV2::onPollTimerTimeout);
  QString ignored;
  applyConfig(cfg, &ignored);
}

PlcRuntimeServiceV2::~PlcRuntimeServiceV2() { stop(); }

bool PlcRuntimeServiceV2::applyConfig(const AppConfig &cfg, QString *err) {
  cfg_ = cfg;
  if (!buildPlcAddressLayoutV2(cfg_.plc, &layout_, err)) {
    return false;
  }

  stats_.plc_enabled = cfg_.plc.enabled;
  stats_.poll_interval_ms = cfg_.plc.poll_interval_ms;
  poll_timer_.setInterval(cfg_.plc.poll_interval_ms > 0 ? cfg_.plc.poll_interval_ms
                                                        : 100);

  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    if (!qtClient->applyConfig(cfg_.plc, err)) {
      return false;
    }
  }

  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::initializeRealClient(QString *err) {
  auto client = std::make_unique<QtModbusTcpRegisterClientV2>();
  if (!client->applyConfig(cfg_.plc, err)) {
    return false;
  }
  client_ = client.get();
  owned_client_ = std::move(client);
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

void PlcRuntimeServiceV2::setRegisterClient(IPlcRegisterClientV2 *client,
                                            bool takeOwnership) {
  stop();
  owned_client_.reset();
  client_ = nullptr;

  if (!client) {
    refreshConnectionState();
    emit statsUpdated(stats_);
    return;
  }

  if (takeOwnership) {
    owned_client_.reset(client);
    client_ = owned_client_.get();
  } else {
    client_ = client;
  }

  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    QString ignored;
    qtClient->applyConfig(cfg_.plc, &ignored);
  }

  refreshConnectionState();
  emit statsUpdated(stats_);
}

bool PlcRuntimeServiceV2::ensureClientReady(QString *err) {
  if (!cfg_.plc.enabled) {
    failWith(err, QStringLiteral("PLC 未启用，请先在 app.ini 的 [plc] 中 enabled=1"));
    return false;
  }
  if (!client_) {
    return initializeRealClient(err);
  }
  return true;
}

bool PlcRuntimeServiceV2::start(QString *err) {
  if (!ensureClientReady(err)) {
    return false;
  }
  if (stats_.running) {
    return true;
  }

  stats_.running = true;
  if (cfg_.plc.poll_interval_ms > 0 && cfg_.plc.status_start_address > 0) {
    poll_timer_.start();
  }
  emit runningChanged(true);
  emit statsUpdated(stats_);
  return true;
}

void PlcRuntimeServiceV2::stop() {
  if (!stats_.running && !poll_timer_.isActive()) {
    return;
  }
  poll_timer_.stop();
  stats_.running = false;
  poll_in_progress_ = false;
  emit runningChanged(false);
  emit statsUpdated(stats_);
}

bool PlcRuntimeServiceV2::connectNow(QString *err) {
  if (!ensureClientReady(err)) {
    return false;
  }

  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    if (!qtClient->connectToPlc(err)) {
      refreshConnectionState();
      publishError(err ? *err : QStringLiteral("连接 PLC 失败"));
      return false;
    }
    refreshConnectionState();
    emit statsUpdated(stats_);
    return true;
  }

  // 对 Fake/其他客户端，只要存在就视为可用。
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

void PlcRuntimeServiceV2::disconnectNow() {
  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    qtClient->disconnectFromPlc();
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
}

bool PlcRuntimeServiceV2::sendCommand(const PlcCommandBlockV2 &command,
                                      QString *err) {
  if (!ensureClientReady(err)) return false;
  if (cfg_.plc.command_start_address == 0) {
    failWith(err, QStringLiteral("Command Block 地址尚未配置"));
    return false;
  }
  QVector<quint16> regs;
  regs.push_back(static_cast<quint16>(qMax(0, static_cast<int>(command.category_mode))));
  regs.push_back(command.cmd_code);
  if (!writeHoldingRegistersRaw(cfg_.plc.command_start_address, regs, err)) {
    return false;
  }
  return true;
}

bool PlcRuntimeServiceV2::sendPcAck(quint16 pc_ack, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (cfg_.plc.pc_ack_start_address == 0) {
    failWith(err, QStringLiteral("pc_ack 地址尚未配置"));
    return false;
  }
  QVector<quint16> regs{pc_ack};
  return writeHoldingRegistersRaw(cfg_.plc.pc_ack_start_address, regs, err);
}

bool PlcRuntimeServiceV2::readHoldingRegistersRaw(quint32 startAddress, quint16 regCount,
                                                 QVector<quint16> *out, QString *err) {
  if (!ensureClientReady(err)) {
    return false;
  }
  if (!client_) {
    failWith(err, QStringLiteral("PLC client 未就绪"));
    return false;
  }
  if (!client_->readHoldingRegisters(startAddress, regCount, out, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) { qtClient->disconnectFromPlc(); }
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("读取原始寄存器失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::writeHoldingRegistersRaw(quint32 startAddress, const QVector<quint16> &values,
                                                   QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!client_) { failWith(err, QStringLiteral("PLC client 未就绪")); return false; }
  if (!client_->writeHoldingRegisters(startAddress, values, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) { qtClient->disconnectFromPlc(); }
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写原始寄存器失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::readMbBytesRaw(quint32 mbByteAddress, quint16 byteCount, QByteArray *out, QString *err) {
  if (!out) { failWith(err, QStringLiteral("readMbBytesRaw.out 不能为空")); return false; }
  const quint32 startReg = mbByteAddress / 2u;
  const quint32 byteOffset = mbByteAddress % 2u;
  const quint32 regCount = static_cast<quint32>((byteOffset + byteCount + 1u) / 2u);
  QVector<quint16> regs;
  if (!readHoldingRegistersRaw(startReg, static_cast<quint16>(regCount), &regs, err)) return false;
  const QByteArray bytes = mbBytesFromRegsRuntime(regs);
  *out = bytes.mid(static_cast<int>(byteOffset), byteCount);
  return true;
}

bool PlcRuntimeServiceV2::writeMbBytesRaw(quint32 mbByteAddress, const QByteArray &bytes, QString *err) {
  const quint32 startReg = mbByteAddress / 2u;
  const quint32 byteOffset = mbByteAddress % 2u;
  const quint32 regCount = static_cast<quint32>((byteOffset + bytes.size() + 1u) / 2u);
  QVector<quint16> regs;
  if (!readHoldingRegistersRaw(startReg, static_cast<quint16>(regCount), &regs, err)) return false;
  QByteArray raw = mbBytesFromRegsRuntime(regs);
  for (int i = 0; i < bytes.size(); ++i) {
    if (static_cast<int>(byteOffset) + i < raw.size()) raw[static_cast<int>(byteOffset) + i] = bytes.at(i);
  }
  return writeHoldingRegistersRaw(startReg, regsFromMbBytesRuntime(raw), err);
}

bool PlcRuntimeServiceV2::writePlcMode(qint16 mode, QString *err) {
  if (cfg_.plc.mode_start_address == 0) {
    failWith(err, QStringLiteral("PLC 模式地址未配置"));
    return false;
  }
  QVector<quint16> regs{static_cast<quint16>(mode)};
  return writeHoldingRegistersRaw(cfg_.plc.mode_start_address, regs, err);
}

bool PlcRuntimeServiceV2::writeJudgeResult(quint16 judgeResult, QString *err) {
  if (cfg_.plc.judge_result_start_address == 0) {
    failWith(err, QStringLiteral("JudgeResult 地址未配置"));
    return false;
  }
  QVector<quint16> regs{judgeResult};
  return writeHoldingRegistersRaw(cfg_.plc.judge_result_start_address, regs, err);
}

bool PlcRuntimeServiceV2::readSecondStageTrayIds(PlcTrayPartIdBlockV2 *out, QString *err) {
  QVector<quint16> regs;
  if (!readHoldingRegistersRaw(cfg_.plc.tray_start_address, static_cast<quint16>(kTrayAllCodingRegsV25), &regs, err)) return false;
  if (!buildPlcTrayAllCodingBlockV25(regs, out, err)) return false;
  emit trayUpdated(*out);
  return true;
}

bool PlcRuntimeServiceV2::readSecondStageMailboxSnapshot(QChar partType, PlcMailboxSnapshot *out,
                                                         QString *err) {
  QVector<quint16> regs;
  if (!readHoldingRegistersRaw(cfg_.plc.mailbox_start_address, static_cast<quint16>(kMailboxTotalRegsV25), &regs, err)) return false;
  if (!buildSecondStageMailboxSnapshotV25(regs, partType, out, err)) return false;
  emit mailboxSnapshotUpdated(*out);
  return true;
}

bool PlcRuntimeServiceV2::readFirstStageMailboxSnapshot(QChar partType, PlcMailboxSnapshot *out,
                                                    QString *err) {
  if (!cfg_.plc.first_stage_enabled) {
    failWith(err, QStringLiteral("当前未启用第一阶段联调模式"));
    return false;
  }
  QVector<quint16> codingRegs;
  QVector<quint16> keyenceRegs;
  QVector<quint16> chuantecRegs;
  if (!readHoldingRegistersRaw(cfg_.plc.coding_start_address, static_cast<quint16>(kFirstStageCodingRegsV24), &codingRegs, err)) return false;
  if (!readHoldingRegistersRaw(cfg_.plc.keyence_result_start_address, static_cast<quint16>(kFirstStageKeyenceRegsV24), &keyenceRegs, err)) return false;
  if (!readHoldingRegistersRaw(cfg_.plc.chuantec_result_start_address, static_cast<quint16>(kFirstStageChuantecRegsV24), &chuantecRegs, err)) return false;
  PlcMailboxSnapshot snapshot;
  if (!buildFirstStageMailboxSnapshotV24(codingRegs, keyenceRegs, chuantecRegs, partType, &snapshot, err)) {
    return false;
  }
  if (out) *out = snapshot;
  emit mailboxSnapshotUpdated(snapshot);
  return true;
}

bool PlcRuntimeServiceV2::writeTrayPartIdSlot(int slotIndex,
                                              const QString &partId,
                                              QString *err) {
  if (!ensureClientReady(err)) return false;
  if (cfg_.plc.tray_start_address == 0) {
    failWith(err, QStringLiteral("Tray Part-ID Block 地址尚未配置"));
    return false;
  }
  if (slotIndex < 0 || slotIndex >= kLogicalSlotCount) {
    failWith(err, QStringLiteral("slotIndex 越界"));
    return false;
  }
  QByteArray bytes(81, '\0');
  const QByteArray src = partId.toLatin1().left(80);
  for (int i = 0; i < src.size(); ++i) bytes[i] = src.at(i);
  return writeMbBytesRaw(cfg_.plc.tray_start_address * 2u + static_cast<quint32>(slotIndex * 81), bytes, err);
}

void PlcRuntimeServiceV2::onPollTimerTimeout() { pollOnce(); }

void PlcRuntimeServiceV2::pollOnce() {
  if (poll_in_progress_) return;
  QString err;
  if (!ensureClientReady(&err)) {
    publishError(err);
    refreshConnectionState();
    emit statsUpdated(stats_);
    return;
  }
  poll_in_progress_ = true;
  QElapsedTimer timer; timer.start();
  bool ok = false;
  if (cfg_.plc.status_start_address > 0) {
    ok = pollSecondStage(&err);
  } else {
    refreshConnectionState();
    ok = true;
  }
  stats_.last_poll_ms = static_cast<int>(timer.elapsed());
  if (!ok) {
    stats_.poll_error_count += 1;
    stats_.consecutive_error_count += 1;
    stats_.last_error = err;
    refreshConnectionState();
    emit statsUpdated(stats_);
    publishError(err);
    poll_in_progress_ = false;
    return;
  }
  stats_.poll_ok_count += 1;
  stats_.consecutive_error_count = 0;
  stats_.last_error.clear();
  refreshConnectionState();
  emit statsUpdated(stats_);
  poll_in_progress_ = false;
}

bool PlcRuntimeServiceV2::pollSecondStage(QString *err) {
  QVector<quint16> statusRegs;
  if (!readHoldingRegistersRaw(cfg_.plc.status_start_address, static_cast<quint16>(kStatusBlockRegsV25), &statusRegs, err)) return false;
  PlcStatusBlockV2 status;
  if (!buildPlcStatusBlockV25(statusRegs, &status, err)) return false;
  if (cfg_.plc.mode_start_address > 0) {
    QVector<quint16> modeRegs;
    if (!readHoldingRegistersRaw(cfg_.plc.mode_start_address, 1, &modeRegs, err)) return false;
    if (!modeRegs.isEmpty()) status.control_mode = static_cast<qint16>(modeRegs.at(0));
  }
  emit statusUpdated(status);

  QVector<quint16> commandRegs;
  if (cfg_.plc.command_start_address > 0) {
    if (!readHoldingRegistersRaw(cfg_.plc.command_start_address, static_cast<quint16>(kCommandBlockRegsV25), &commandRegs, err)) return false;
    PlcCommandBlockV2 command;
    if (!buildPlcCommandBlockV25(commandRegs, &command, err)) return false;
    emit commandUpdated(command);
  }
  return true;
}

bool PlcRuntimeServiceV2::refreshConnectionState() {
  const bool old = stats_.connected;
  bool now = false;

  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    now = qtClient->isConnected();
  } else {
    now = (client_ != nullptr);
  }

  stats_.connected = now;
  if (old != now) {
    emit connectionChanged(now);
  }
  return now;
}

void PlcRuntimeServiceV2::updateStatsFromCache() {
  stats_.last_state_seq = cache_.last_state_seq;
  stats_.last_scan_seq = cache_.last_scan_seq;
  stats_.last_meas_seq = cache_.last_meas_seq;
  stats_.last_cmd_ack_seq = cache_.last_cmd_ack_seq;
}

void PlcRuntimeServiceV2::publishError(const QString &message) {
  if (!message.trimmed().isEmpty()) {
    emit errorOccurred(message);
  }
}

} // namespace core

