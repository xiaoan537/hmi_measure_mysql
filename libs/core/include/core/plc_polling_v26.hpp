#pragma once

#include <QtGlobal>

namespace core {

struct PlcPollCacheV26 {
  bool has_status = false;
  bool has_command = false;

  quint16 last_scan_done = 0;
  quint16 last_mailbox_ready = 0;
};

struct PlcPollEventsV26 {
  bool scan_ready = false;
  bool mailbox_ready = false;
  bool new_mailbox = false;
};

} // namespace core
