#include "core/plc_runtime_v2.hpp"

#include <QElapsedTimer>

namespace core {
namespace {

void failWith(QString *err, const QString &message) {
  if (err) {
    *err = message;
  }
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
  poll_timer_.start();
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
  if (!ensureClientReady(err)) {
    return false;
  }
  if (!writePlcCommandV2(client_, layout_, command, err)) {
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 PLC Command 失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendPcAck(quint16 pc_ack, QString *err) {
  if (!ensureClientReady(err)) {
    return false;
  }
  if (!writePlcPcAckV2(client_, layout_, pc_ack, err)) {
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 pc_ack 失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::writeTrayPartIdSlot(int slotIndex,
                                              const QString &partId,
                                              QString *err) {
  if (!ensureClientReady(err)) {
    return false;
  }
  if (!writePlcTrayPartIdSlotV2(client_, layout_, slotIndex, partId, err)) {
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 Tray Part-ID 失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

void PlcRuntimeServiceV2::onPollTimerTimeout() { pollOnce(); }

void PlcRuntimeServiceV2::pollOnce() {
  if (poll_in_progress_) {
    return;
  }
  QString err;
  if (!ensureClientReady(&err)) {
    publishError(err);
    refreshConnectionState();
    emit statsUpdated(stats_);
    return;
  }

  poll_in_progress_ = true;
  QElapsedTimer timer;
  timer.start();

  PlcPollRunResultV2 result;
  const bool ok = runPlcPollCycleV2(client_, layout_, &cache_, &result, &err);
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
  updateStatsFromCache();

  emit pollCycleCompleted(result);
  emit plcEventsRaised(result.step.events);

  if (result.step.decoded.has_status) {
    emit statusUpdated(result.step.decoded.status);
  }
  if (result.step.decoded.has_tray) {
    emit trayUpdated(result.step.decoded.tray);
  }
  if (result.step.decoded.has_command) {
    emit commandUpdated(result.step.decoded.command);
  }
  if (result.step.decoded.has_mailbox_snapshot) {
    emit mailboxSnapshotUpdated(result.step.decoded.mailbox_snapshot);
  }

  emit statsUpdated(stats_);
  poll_in_progress_ = false;
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

