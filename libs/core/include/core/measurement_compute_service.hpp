#pragma once

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/config.hpp"
#include "core/measurement_pipeline.hpp"

namespace core {

struct MeasurementComputedItem {
  int item_index = -1;
  ProductionSlotSummary summary;
};

struct MeasurementComputeServiceResult {
  bool has_items = false;
  bool overall_ok = false;
  QChar part_type = QChar('A');
  int expected_item_count = 0;
  int judged_item_count = 0;

  QVector<MeasurementComputedItem> items;
  QStringList logs;
};

bool computeMailboxSnapshot(const PlcMailboxSnapshot &snapshot,
                            const AlgorithmConfig &algo,
                            bool calibration_context,
                            MeasurementComputeServiceResult *out,
                            QString *err = nullptr);

QString measurementFormatNumber(double value, int precision = 6);

} // namespace core
