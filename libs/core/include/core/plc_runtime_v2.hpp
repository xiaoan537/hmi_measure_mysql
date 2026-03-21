#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QtGlobal>

#include <memory>

#include "core/config.hpp"
#include "core/plc_qt_modbus_v2.hpp"
#include "core/plc_transport_v2.hpp"

namespace core {

struct PlcRuntimeStatsV2 {
  bool plc_enabled = false;
  bool running = false;
  bool connected = false;

  int poll_interval_ms = 100;
  int last_poll_ms = 0;
  int poll_ok_count = 0;
  int poll_error_count = 0;
  int consecutive_error_count = 0;

  quint32 last_state_seq = 0;
  quint32 last_scan_seq = 0;
  quint32 last_meas_seq = 0;
  quint32 last_cmd_ack_seq = 0;

  QString last_error;
};

class PlcRuntimeServiceV2 : public QObject {
  Q_OBJECT
public:
  explicit PlcRuntimeServiceV2(const AppConfig &cfg, QObject *parent = nullptr);
  ~PlcRuntimeServiceV2() override;

  bool applyConfig(const AppConfig &cfg, QString *err = nullptr);

  // 默认创建真实 QModbusTcpClient 版本；
  // 若后续接 Fake PLC Client，可改用 setRegisterClient(...) 注入。
  bool initializeRealClient(QString *err = nullptr);

  void setRegisterClient(IPlcRegisterClientV2 *client, bool takeOwnership);

  bool start(QString *err = nullptr);
  void stop();

  bool isRunning() const { return stats_.running; }
  bool isConnected() const { return stats_.connected; }

  const AppConfig &config() const { return cfg_; }
  const PlcAddressLayoutV2 &addressLayout() const { return layout_; }
  const PlcPollCacheV2 &pollCache() const { return cache_; }
  const PlcRuntimeStatsV2 &stats() const { return stats_; }

public slots:
  void pollOnce();

  bool connectNow(QString *err = nullptr);
  void disconnectNow();

  bool sendCommand(const PlcCommandBlockV2 &command, QString *err = nullptr);
  bool sendPcAck(quint16 pc_ack, QString *err = nullptr);
  bool writeTrayPartIdSlot(int slotIndex, const QString &partId,
                           QString *err = nullptr);

signals:
  void runningChanged(bool running);
  void connectionChanged(bool connected);
  void statsUpdated(const core::PlcRuntimeStatsV2 &stats);
  void errorOccurred(const QString &message);

  void pollCycleCompleted(const core::PlcPollRunResultV2 &result);
  void plcEventsRaised(const core::PlcPollEventsV2 &events);

  void statusUpdated(const core::PlcStatusBlockV2 &status);
  void trayUpdated(const core::PlcTrayPartIdBlockV2 &tray);
  void commandUpdated(const core::PlcCommandBlockV2 &command);
  void mailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot);

private slots:
  void onPollTimerTimeout();

private:
  bool ensureClientReady(QString *err = nullptr);
  bool refreshConnectionState();
  void updateStatsFromCache();
  void publishError(const QString &message);

private:
  AppConfig cfg_;
  PlcAddressLayoutV2 layout_;
  PlcPollCacheV2 cache_;
  PlcRuntimeStatsV2 stats_;

  std::unique_ptr<IPlcRegisterClientV2> owned_client_;
  IPlcRegisterClientV2 *client_ = nullptr;

  QTimer poll_timer_;
  bool poll_in_progress_ = false;
};

} // namespace core

