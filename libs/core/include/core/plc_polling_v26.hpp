#pragma once

#include <QtGlobal>

namespace core {

struct PlcPollCacheV26 {
  bool has_status = false;
  bool has_command = false;

  quint32 last_state_seq = 0;
  quint32 last_scan_seq = 0;
  quint32 last_meas_seq = 0;
  quint32 last_cmd_ack_seq = 0;

  quint16 last_scan_done = 0;
  quint16 last_mailbox_ready = 0;
};

struct PlcPollEventsV26 {
  bool state_seq_advanced = false;
  bool scan_ready = false;
  bool mailbox_ready = false;
  bool new_mailbox = false;
  bool command_ack_advanced = false;
};

} // namespace core
