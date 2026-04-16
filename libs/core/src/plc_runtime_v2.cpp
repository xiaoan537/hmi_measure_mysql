#include "core/plc_runtime_v2.hpp"

#include <QElapsedTimer>
#include <QTimer>

#include "core/measurement_pipeline.hpp"
#include "core/plc_addresses_v26.hpp"
#include "core/plc_codec_v26.hpp"
#include "core/plc_motion_service_v26.hpp"
#include "core/plc_repository_v26.hpp"
#include "core/plc_service_v26.hpp"

namespace core {
namespace {
void failWith(QString *err, const QString &message) { if (err) *err = message; }
} // namespace

PlcRuntimeServiceV2::PlcRuntimeServiceV2(const AppConfig &cfg, QObject *parent)
    : QObject(parent), cfg_(cfg) {
  poll_timer_.setSingleShot(false);
  connect(&poll_timer_, &QTimer::timeout, this, &PlcRuntimeServiceV2::onPollTimerTimeout);
  QString ignored;
  applyConfig(cfg_, &ignored);
}

PlcRuntimeServiceV2::~PlcRuntimeServiceV2() { stop(); }

void PlcRuntimeServiceV2::rebuildPlcServices() {
  repo_owner_ = std::make_unique<PlcRepositoryV26>(client_);
  service_owner_ = std::make_unique<PlcServiceV26>(client_);
  motion_service_owner_ = std::make_unique<PlcMotionServiceV26>(client_);
  repo_ptr_ = repo_owner_.get();
  service_ptr_ = service_owner_.get();
  motion_service_ptr_ = motion_service_owner_.get();
}

bool PlcRuntimeServiceV2::applyConfig(const AppConfig &cfg, QString *err) {
  cfg_ = cfg;
  if (!buildPlcAddressLayoutV2(cfg_.plc, &layout_, err)) {
    return false;
  }
  stats_.plc_enabled = cfg_.plc.enabled;
  stats_.poll_interval_ms = cfg_.plc.poll_interval_ms;
  poll_timer_.setInterval(cfg_.plc.poll_interval_ms > 0 ? cfg_.plc.poll_interval_ms : 100);
  cache_ = PlcPollCacheV26{};
  stats_.last_error.clear();
  rebuildPlcServices();
  return true;
}

bool PlcRuntimeServiceV2::initializeRealClient(QString *err) {
  auto client = std::make_unique<QtModbusTcpRegisterClientV2>(this);
  if (!client->applyConfig(cfg_.plc, err)) {
    return false;
  }
  setRegisterClient(client.release(), true);
  return true;
}

void PlcRuntimeServiceV2::setRegisterClient(IPlcRegisterClientV2 *client, bool takeOwnership) {
  if (takeOwnership) {
    owned_client_.reset(client);
    client_ = owned_client_.get();
  } else {
    owned_client_.reset();
    client_ = client;
  }
  rebuildPlcServices();
}

bool PlcRuntimeServiceV2::ensureClientReady(QString *err) {
  if (!cfg_.plc.enabled) {
    failWith(err, QStringLiteral("PLC 未启用"));
    return false;
  }
  if (!client_) {
    if (!initializeRealClient(err)) {
      return false;
    }
  }
  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    return qtClient->ensureConnected(err);
  }
  return true;
}

bool PlcRuntimeServiceV2::start(QString *err) {
  if (!cfg_.plc.enabled) {
    stats_.running = false;
    emit runningChanged(false);
    return true;
  }
  if (!ensureClientReady(err)) {
    stats_.running = false;
    emit runningChanged(false);
    refreshConnectionState();
    return false;
  }
  stats_.running = true;
  emit runningChanged(true);
  if (cfg_.plc.poll_interval_ms > 0) {
    poll_timer_.start();
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

void PlcRuntimeServiceV2::stop() {
  poll_timer_.stop();
  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
    qtClient->disconnectFromPlc();
  }
  stats_.running = false;
  refreshConnectionState();
  emit runningChanged(false);
  emit statsUpdated(stats_);
}

bool PlcRuntimeServiceV2::connectNow(QString *err) {
  if (!ensureClientReady(err)) {
    refreshConnectionState();
    return false;
  }
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

bool PlcRuntimeServiceV2::sendInitialize(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendInitialize(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写初始化命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendStartMeasure(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendStartMeasure(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写开始测量命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendStartCalibration(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendStartCalibration(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写开始标定命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendStop(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendStop(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写停止命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendReset(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendReset(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写复位命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendRetestCurrent(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendRetestCurrent(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写当前件复测命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendContinueWithoutRetest(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendContinueWithoutRetest(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写继续（不复测）命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendAlarmMute(qint16 partType, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->sendAlarmMute(partType, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写报警静音命令失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::sendPcAck(quint16 pc_ack, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!repo_ptr_->writePcAck(pc_ack, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 ACK 失败"));
    return false;
  }
  refreshConnectionState();
  emit statsUpdated(stats_);
  return true;
}

bool PlcRuntimeServiceV2::readSecondStageTrayIds(PlcTrayPartIdBlockV2 *out, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->readTrayCoding(out, err)) return false;
  emit trayUpdated(*out);
  return true;
}

bool PlcRuntimeServiceV2::readSecondStageMailboxSnapshot(QChar partType, PlcMailboxSnapshot *out, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->readMailbox(partType, out, err)) return false;
  emit mailboxSnapshotUpdated(*out);
  return true;
}

bool PlcRuntimeServiceV2::writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err) {
  if (!ensureClientReady(err)) return false;
  return service_ptr_->writeTrayPartIdSlot(slotIndex, partId, err);
}

bool PlcRuntimeServiceV2::writePlcMode(qint16 mode, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->setControlMode(mode, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 PLC 模式失败"));
    return false;
  }
  refreshConnectionState();
  return true;
}

bool PlcRuntimeServiceV2::setModeManual(QString *err) {
  return writePlcMode(plc_v26::kModeManual, err);
}

bool PlcRuntimeServiceV2::setModeAuto(QString *err) {
  return writePlcMode(plc_v26::kModeAuto, err);
}

bool PlcRuntimeServiceV2::setModeSingleStep(QString *err) {
  return writePlcMode(plc_v26::kModeSingleStep, err);
}

bool PlcRuntimeServiceV2::setCategoryMode(qint16 categoryMode, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!service_ptr_->setPartType(categoryMode, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写工件类型失败"));
    return false;
  }
  refreshConnectionState();
  return true;
}

bool PlcRuntimeServiceV2::setPartTypeA(QString *err) {
  return setCategoryMode(plc_v26::kPartTypeA, err);
}

bool PlcRuntimeServiceV2::setPartTypeB(QString *err) {
  return setCategoryMode(plc_v26::kPartTypeB, err);
}

bool PlcRuntimeServiceV2::writeScanDone(quint16 value, QString *err) {
  if (!ensureClientReady(err)) return false;
  if (!repo_ptr_->writeScanDone(value, err)) {
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) qtClient->disconnectFromPlc();
    refreshConnectionState();
    publishError(err ? *err : QStringLiteral("写 scan_done 失败"));
    return false;
  }
  refreshConnectionState();
  return true;
}

bool PlcRuntimeServiceV2::writeJudgeResult(quint16 judgeResult, QString *err) {
  if (!ensureClientReady(err)) return false;
  return service_ptr_->writeJudgeResult(judgeResult, err);
}

bool PlcRuntimeServiceV2::readAxisState(int axisIndex, PlcAxisStateV26 *out, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->readAxisState(axisIndex, out, err);
}

bool PlcRuntimeServiceV2::readCylinderState(const QString &group, int index, PlcCylinderStateV26 *out, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->readCylinderState(group, index, out, err);
}

bool PlcRuntimeServiceV2::axisSetEnable(int axisIndex, bool on, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->axisSetEnable(axisIndex, on, err);
}

bool PlcRuntimeServiceV2::axisPulseAction(int axisIndex, const QString &action, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->axisPulseAction(axisIndex, action, err);
}

bool PlcRuntimeServiceV2::axisJog(int axisIndex, bool forward, bool active, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->axisJog(axisIndex, forward, active, err);
}

bool PlcRuntimeServiceV2::axisMove(int axisIndex, bool relative, double acc, double dec, double pos, double vel, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->axisMove(axisIndex, relative, acc, dec, pos, vel, err);
}

bool PlcRuntimeServiceV2::cylinderAction(const QString &group, int index, const QString &action, QString *err) {
  if (!ensureClientReady(err)) return false;
  return motion_service_ptr_->cylinderAction(group, index, action, err);
}

void PlcRuntimeServiceV2::onPollTimerTimeout() { pollOnce(); }

void PlcRuntimeServiceV2::pollOnce() {
  if (poll_in_progress_ || !cfg_.plc.enabled) return;
  QString err;
  if (!ensureClientReady(&err)) {
    publishError(err);
    refreshConnectionState();
    emit statsUpdated(stats_);
    return;
  }
  poll_in_progress_ = true;
  QElapsedTimer timer; timer.start();
  bool ok = pollSecondStage(&err);
  stats_.last_poll_ms = static_cast<int>(timer.elapsed());
  if (!ok) {
    stats_.poll_error_count += 1;
    stats_.consecutive_error_count += 1;
    stats_.last_error = err;
    if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) {
      qtClient->disconnectFromPlc();
    }
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
  PlcStatusBlockV2 status{};
  PlcCommandBlockV2 command{};
  if (!service_ptr_->pollStatusAndCommand(&status, &command, err)) {
    return false;
  }

  QVector<quint16> modeRegs;
  if (!repo_ptr_->readHolding(plc_v26::kRegMode, 1, &modeRegs, err)) return false;
  if (!modeRegs.isEmpty()) status.control_mode = static_cast<qint16>(modeRegs.at(0));

  PlcPollEventsV26 events;
  events.scan_ready = (status.scan_done != 0 && cache_.last_scan_done == 0);
  events.mailbox_ready = (status.mailbox_ready != 0);
  events.new_mailbox = (status.mailbox_ready != 0 && cache_.last_mailbox_ready == 0);
  events.state_seq_advanced = false;
  events.command_ack_advanced = false;

  emit statusUpdated(status);
  emit commandUpdated(command);

  if (events.scan_ready) {
    PlcTrayPartIdBlockV2 tray;
    if (service_ptr_->readTrayCoding(&tray, err)) {
      emit trayUpdated(tray);
    }
  }
  if (events.new_mailbox) {
    // read mailbox using current category if available, else infer from part type command/category
    const QChar partType = (command.category_mode == plc_v26::kPartTypeB) ? QChar('B') : QChar('A');
    PlcMailboxSnapshot mailbox;
    if (service_ptr_->readMailbox(partType, &mailbox, err)) {
      emit mailboxSnapshotUpdated(mailbox);
    }
  }

  emit plcEventsRaised(events);

  cache_.has_status = true;
  cache_.last_scan_done = status.scan_done;
  cache_.last_mailbox_ready = status.mailbox_ready;
  stats_.last_scan_seq = status.scan_done;
  stats_.last_meas_seq = status.mailbox_ready;
  return true;
}

bool PlcRuntimeServiceV2::refreshConnectionState() {
  const bool old = stats_.connected;
  bool now = false;
  if (auto *qtClient = dynamic_cast<QtModbusTcpRegisterClientV2 *>(client_)) now = qtClient->isConnected();
  else now = (client_ != nullptr);
  stats_.connected = now;
  if (old != now) emit connectionChanged(now);
  return now;
}

void PlcRuntimeServiceV2::publishError(const QString &message) {
  if (!message.trimmed().isEmpty()) emit errorOccurred(message);
}

} // namespace core
