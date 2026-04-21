#pragma once

#include <QString>
#include <QtGlobal>

namespace plc_step_rules_v26 {

constexpr quint16 kStepWaitStart = 0;
constexpr quint16 kStepDataClear = 1;
constexpr quint16 kStepInitPrepareA = 2;
constexpr quint16 kStepInitPrepareB = 3;
constexpr quint16 kStepInitDone = 9;
constexpr quint16 kStepScan = 10;
constexpr quint16 kStepScanContinue = 11;
constexpr quint16 kStepDecidePickStart = 12;
constexpr quint16 kStepPickFromRackA = 100;
constexpr quint16 kStepPickFromRackB = 101;
constexpr quint16 kStepLoadMeasureStation = 111;
constexpr quint16 kStepMeasureA = 120;
constexpr quint16 kStepMeasureB = 131;
constexpr quint16 kStepExchangeMeasureA = 141;
constexpr quint16 kStepExchangeMeasureB = 151;
constexpr quint16 kStepReloadAfterExchangeA = 160;
constexpr quint16 kStepReloadAfterExchangeB = 161;
constexpr quint16 kStepMeasureAfterExchangeA = 191;
constexpr quint16 kStepMeasureAfterExchangeB = 192;
constexpr quint16 kStepUnloadAfterMeasureA = 201;
constexpr quint16 kStepUnloadAfterMeasureB = 211;
constexpr quint16 kStepArchiveAndCompute = 220;
constexpr quint16 kStepReturnToRack = 231;

// Calibration flow (v2.6)
constexpr quint16 kStepCalPickFromRack = 411;
constexpr quint16 kStepCalLoadMeasureStation = 421;
constexpr quint16 kStepCalMeasureA = 440;
constexpr quint16 kStepCalMeasureB = 441;
constexpr quint16 kStepCalUnloadFromDiameterOrRunout = 451;
constexpr quint16 kStepCalLoadLengthStation = 461;
constexpr quint16 kStepCalMeasureLength = 471;
constexpr quint16 kStepCalUnloadLengthStation = 481;
constexpr quint16 kStepCalArchiveWaitDecisionA = 490;
constexpr quint16 kStepCalArchiveWaitDecisionB = 491;
constexpr quint16 kStepCalReturnToRack = 511;
constexpr quint16 kStepAlarm = 900;
constexpr quint16 kStepEStop = 910;

inline bool isScanStep(quint16 step) {
  return step == kStepScan || step == kStepScanContinue;
}

inline bool isMailboxArchiveStep(quint16 step) {
  return step == kStepArchiveAndCompute;
}

inline bool isProductionProcessingStep(quint16 step) {
  switch (step) {
  case kStepDecidePickStart:
  case kStepPickFromRackA:
  case kStepPickFromRackB:
  case kStepLoadMeasureStation:
  case kStepMeasureA:
  case kStepMeasureB:
  case kStepExchangeMeasureA:
  case kStepExchangeMeasureB:
  case kStepReloadAfterExchangeA:
  case kStepReloadAfterExchangeB:
  case kStepMeasureAfterExchangeA:
  case kStepMeasureAfterExchangeB:
  case kStepUnloadAfterMeasureA:
  case kStepUnloadAfterMeasureB:
  case kStepReturnToRack:
    return true;
  default:
    return false;
  }
}

inline bool isNewCycleStep(quint16 step) {
  switch (step) {
  case kStepDataClear:
  case kStepInitPrepareA:
  case kStepInitPrepareB:
  case kStepInitDone:
  case kStepScan:
    return true;
  default:
    return false;
  }
}

inline bool isCalibrationStep(quint16 step) {
  switch (step) {
  case kStepCalPickFromRack:
  case kStepCalLoadMeasureStation:
  case kStepCalMeasureA:
  case kStepCalMeasureB:
  case kStepCalUnloadFromDiameterOrRunout:
  case kStepCalLoadLengthStation:
  case kStepCalMeasureLength:
  case kStepCalUnloadLengthStation:
  case kStepCalArchiveWaitDecisionA:
  case kStepCalArchiveWaitDecisionB:
  case kStepCalReturnToRack:
    return true;
  default:
    return false;
  }
}

inline bool isCalibrationArchiveStep(quint16 step) {
  return step == kStepCalArchiveWaitDecisionA || step == kStepCalArchiveWaitDecisionB;
}

inline QString calibrationStepText(quint16 step) {
  switch (step) {
  case kStepCalPickFromRack: return QStringLiteral("从料架抓料中");
  case kStepCalLoadMeasureStation: return QStringLiteral("测量工位上料中");
  case kStepCalMeasureA:
  case kStepCalMeasureB: return QStringLiteral("标定测量中");
  case kStepCalUnloadFromDiameterOrRunout: return QStringLiteral("取跳动/内外径工位物料中");
  case kStepCalLoadLengthStation: return QStringLiteral("测长工位上料中");
  case kStepCalMeasureLength: return QStringLiteral("标定测量中");
  case kStepCalUnloadLengthStation: return QStringLiteral("取测长工位物料中");
  case kStepCalArchiveWaitDecisionA:
  case kStepCalArchiveWaitDecisionB: return QStringLiteral("数据归档，等待复测或下料指令");
  case kStepCalReturnToRack: return QStringLiteral("下料回料架");
  case kStepAlarm: return QStringLiteral("报警");
  case kStepEStop: return QStringLiteral("急停");
  default:
    return QStringLiteral("STEP(%1)").arg(step);
  }
}

inline QString productionStepText(quint16 step) {
  switch (step) {
  case kStepWaitStart: return QStringLiteral("待开始");
  case kStepDataClear: return QStringLiteral("数据清零");
  case kStepInitPrepareA:
  case kStepInitPrepareB: return QStringLiteral("初始化中");
  case kStepInitDone: return QStringLiteral("初始化完成");
  case kStepScan: return QStringLiteral("扫码中");
  case kStepScanContinue: return QStringLiteral("扫码中");
  case kStepDecidePickStart: return QStringLiteral("判断开始抓料位置");
  case kStepPickFromRackA:
  case kStepPickFromRackB: return QStringLiteral("从料架抓料");
  case kStepLoadMeasureStation: return QStringLiteral("测量工位上料");
  case kStepMeasureA:
  case kStepMeasureB: return QStringLiteral("测量中");
  case kStepExchangeMeasureA:
  case kStepExchangeMeasureB: return QStringLiteral("物料交换测量工位");
  case kStepReloadAfterExchangeA:
  case kStepReloadAfterExchangeB: return QStringLiteral("交换后再次上料");
  case kStepMeasureAfterExchangeA:
  case kStepMeasureAfterExchangeB: return QStringLiteral("换位后测量中");
  case kStepUnloadAfterMeasureA:
  case kStepUnloadAfterMeasureB: return QStringLiteral("测量完成取料中");
  case kStepArchiveAndCompute: return QStringLiteral("测量完成数据归档");
  case kStepReturnToRack: return QStringLiteral("下料回料架");
  case kStepAlarm: return QStringLiteral("报警");
  case kStepEStop: return QStringLiteral("急停");
  default:
    return QStringLiteral("STEP(%1)").arg(step);
  }
}

} // namespace plc_step_rules_v26
