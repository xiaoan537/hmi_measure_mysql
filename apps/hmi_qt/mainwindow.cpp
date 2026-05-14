#include "alarm_widget.hpp"
#include "calibration_widget.hpp"
#include "data_widget.hpp"
#include "dev_tools_widget.hpp"
#include "manual_maintain_widget.hpp"
#include "diagnostics_widget.hpp"
#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"
#include "plc_step_rules_v26.hpp"
#include "production_widget.hpp"
#include "raw_viewer_widget.hpp"
#include "settings_widget.hpp"
#include "todo_widget.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QAbstractButton>
#include <QUuid>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVariantMap>
#include <QStringList>

#include <QSizePolicy>

#include "ui_mainwindow.h"

#include <cmath>
#include <memory>

#include "core/db.hpp"
#include "core/measurement_compute_service.hpp"
#include "core/measurement_geometry_algorithms.hpp"
#include "core/measurement_ingest.hpp"
#include "core/measurement_pipeline.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_runtime_v2.hpp"
#include "core/plc_addresses_v26.hpp"
#include "core/plc_codec_v26.hpp"
#include "core/raw_v2.hpp"

namespace {
QString mailboxPartTypeText(const core::PlcMailboxSnapshot &snapshot) {
  const QChar pt = snapshot.part_type.toUpper();
  if (pt == QChar('A') || pt == QChar('B')) {
    return QString(pt);
  }
  return QStringLiteral("-");
}

QString productionStepStateText(quint16 stepState, quint16 /*scanDone*/) {
  return plc_step_rules_v26::productionStepText(stepState);
}

QString calibrationStepStateText(quint16 stepState) {
  return plc_step_rules_v26::calibrationStepText(stepState);
}

QString stepStateText(quint16 stepState, quint16 scanDone, bool calibrationContext) {
  return calibrationContext ? calibrationStepStateText(stepState)
                            : productionStepStateText(stepState, scanDone);
}


QString plcModeTextV26(int mode) { return core::plc_codec_v26::plcModeText(static_cast<qint16>(mode)); }

QString axisNameByIndex(int axisIndex) { return core::plc_v26::axisName(axisIndex); }

QString machineStateText(quint16 machineState) { return core::plc_codec_v26::decodeMachineState(machineState).text; }

QString interlockMaskText(quint32 mask) {
  const QStringList reasons = core::plc_codec_v26::decodeInterlockBits(mask);
  return reasons.isEmpty() ? QStringLiteral("无") : reasons.join(QStringLiteral(" | "));
}

bool isCalibrationStepCode(quint16 stepState, bool calibrationFlowExpected) {
  if (plc_step_rules_v26::isCalibrationStep(stepState)) {
    return true;
  }
  return calibrationFlowExpected && plc_step_rules_v26::isCalibrationInitStep(stepState);
}

bool isCalibrationContext(bool hasLastStatus, bool calibrationFlowExpected, quint16 stepState) {
  return hasLastStatus && isCalibrationStepCode(stepState, calibrationFlowExpected);
}

quint32 mapArg(const QVariantMap &args, const QString &key, quint32 def = 0) {
  const QVariant v = args.value(key);
  if (!v.isValid()) return def;
  bool ok = false;
  const quint32 out = v.toUInt(&ok);
  return ok ? out : def;
}

QString cmdBitsText(quint16 bits) {
  QStringList names;
  if (bits & core::plc_v26::kCmdInitializeBit) names << QStringLiteral("初始化");
  if (bits & core::plc_v26::kCmdStartMeasureBit) names << QStringLiteral("开始测量");
  if (bits & core::plc_v26::kCmdStartCalibrationBit) names << QStringLiteral("开始标定");
  if (bits & core::plc_v26::kCmdStopBit) names << QStringLiteral("停止");
  if (bits & core::plc_v26::kCmdResetBit) names << QStringLiteral("复位");
  if (bits & core::plc_v26::kCmdRetestCurrentBit) names << QStringLiteral("当前件复测");
  if (bits & core::plc_v26::kCmdContinueWithoutRetestBit) names << QStringLiteral("继续(不复测)");
  if (bits & core::plc_v26::kCmdAlarmMuteBit) names << QStringLiteral("报警静音");
  if (bits & core::plc_v26::kCmdRejectMask) names << QStringLiteral("拒绝标志");
  if (names.isEmpty()) return QStringLiteral("NONE");
  return names.join(QStringLiteral("|"));
}

QString rejectInstructionText(quint16 bits) {
  QStringList reasons;
  if (bits & 0x0001u) reasons << QStringLiteral("已在执行(请勿重复操作)");
  if (bits & 0x0002u) reasons << QStringLiteral("存在错误,请先复位");
  const quint16 unknown = static_cast<quint16>(bits & ~0x0003u);
  if (unknown != 0) {
    reasons << QStringLiteral("其他位=0x%1").arg(QString::number(unknown, 16).toUpper());
  }
  if (reasons.isEmpty()) return QStringLiteral("无");
  return reasons.join(QStringLiteral("|"));
}

QString formatNumber(double v, int prec = 6) {
  return std::isfinite(v) ? QString::number(v, 'f', prec) : QStringLiteral("--");
}

constexpr double kRawInvalidThresholdMm = 5.0;

bool isRawPointValidForDisplay(double v) {
  return std::isfinite(v) && v <= kRawInvalidThresholdMm;
}

int rawFlatIndex(int channelCount, int ringCount, int pointCount,
                 quint16 orderCode, int channel, int ring, int point);

struct RawRunoutFitPointInfo {
  double fit_radius_mm = qQNaN();
  double point_radius_mm = qQNaN();
  double residual_mm = qQNaN();
};

using RawFitPointInfo = RawRunoutFitPointInfo;

core::DiameterAlgoParams buildRawViewerDiameterParams(const core::AlgorithmConfig &algo,
                                                      int pointCount,
                                                      double kInMm,
                                                      double kOutMm) {
  const int minValidPoints = qBound(3, algo.min_valid_points, qMax(3, pointCount));
  core::DiameterAlgoParams p;
  p.k_in_mm = kInMm;
  p.k_out_mm = kOutMm;
  p.use_explicit_k_out = algo.a_use_explicit_k_out;
  p.probe_base_mm = algo.a_probe_base_mm;
  p.angle_offset_deg = algo.a_angle_offset_deg;
  p.inner_fit.residual_threshold_mm = algo.a_residual_threshold_in_mm;
  p.outer_fit.residual_threshold_mm = algo.a_residual_threshold_out_mm;
  p.inner_fit.min_valid_points = minValidPoints;
  p.outer_fit.min_valid_points = minValidPoints;
  return p;
}

core::RunoutAlgoParams buildRawViewerRunoutParams(const core::AlgorithmConfig &algo,
                                                  int pointCount,
                                                  double kRunoutMm) {
  const int minValidPoints = qBound(3, algo.min_valid_points, qMax(3, pointCount));
  core::RunoutAlgoParams p;
  p.angle_offset_deg = algo.b_angle_offset_deg;
  p.k_runout_mm = kRunoutMm;
  p.interpolation_factor = qMax(1, algo.b_interpolation_factor);
  p.v_block_angle_deg = algo.b_v_block_angle_deg;
  p.fit_options.residual_threshold_mm = algo.b_residual_threshold_mm;
  p.fit_options.min_valid_points = minValidPoints;
  return p;
}

RawFitPointInfo pointInfoFromCircleFit(const core::PointSet2D &pointSet,
                                       const core::CircleFitResult &fit,
                                       int pointIndex) {
  RawFitPointInfo info;
  if (!fit.success ||
      pointIndex < 0 ||
      pointIndex >= pointSet.points.size() ||
      pointIndex >= fit.residuals_mm.size() ||
      !pointSet.points.at(pointIndex).valid) {
    return info;
  }

  const double residual = fit.residuals_mm.at(pointIndex);
  if (!std::isfinite(residual)) return info;

  const core::Point2D &p = pointSet.points.at(pointIndex);
  const double dx = p.x - fit.center_x_mm;
  const double dy = p.y - fit.center_y_mm;
  info.fit_radius_mm = fit.radius_mm;
  info.point_radius_mm = std::sqrt(dx * dx + dy * dy);
  info.residual_mm = residual;
  return info;
}

void fillDiameterFitPointInfoForChannel(const QVector<double> &rawValues,
                                        const QVector<int> &sourceIndexes,
                                        double inputOffsetMm,
                                        bool innerChannel,
                                        const core::DiameterAlgoParams &params,
                                        QVector<RawFitPointInfo> *out) {
  if (!out || rawValues.size() != sourceIndexes.size()) return;

  QVector<double> adjusted;
  QVector<bool> validMask;
  adjusted.reserve(rawValues.size());
  validMask.reserve(rawValues.size());
  for (double raw : rawValues) {
    const bool valid = isRawPointValidForDisplay(raw);
    validMask.push_back(valid);
    adjusted.push_back(valid ? raw + inputOffsetMm : raw);
  }

  const core::DiameterChannelResult fit = innerChannel
      ? core::computeInnerDiameter(adjusted, validMask, params)
      : core::computeOuterDiameter(adjusted, validMask, params);
  if (!fit.circle_fit.success) return;

  for (int i = 0; i < sourceIndexes.size(); ++i) {
    const int src = sourceIndexes.at(i);
    if (src < 0 || src >= out->size()) continue;
    (*out)[src] = pointInfoFromCircleFit(fit.point_set, fit.circle_fit, i);
  }
}

void fillRunoutFitPointInfoForChannel(const QVector<double> &rawValues,
                                      const QVector<int> &sourceIndexes,
                                      double inputOffsetMm,
                                      const core::RunoutAlgoParams &params,
                                      QVector<RawRunoutFitPointInfo> *out) {
  if (!out || rawValues.size() != sourceIndexes.size()) return;

  QVector<double> adjusted;
  QVector<bool> validMask;
  adjusted.reserve(rawValues.size());
  validMask.reserve(rawValues.size());
  for (double raw : rawValues) {
    const bool valid = isRawPointValidForDisplay(raw);
    validMask.push_back(valid);
    adjusted.push_back(valid ? raw + inputOffsetMm : raw);
  }

  const core::RunoutResult fit = core::computeRunoutAnalysis(adjusted, validMask, params);
  if (!fit.circle_fit.success) return;

  for (int i = 0; i < sourceIndexes.size(); ++i) {
    const int src = sourceIndexes.at(i);
    if (src < 0 || src >= out->size()) continue;
    (*out)[src] = pointInfoFromCircleFit(fit.point_set, fit.circle_fit, i);
  }
}

QVector<RawFitPointInfo> buildRawDiameterFitPointInfo(
    const core::MeasurementSnapshot &raw,
    const core::AlgorithmConfig &algo,
    int channelCount,
    int ringCount,
    int pointCount) {
  QVector<RawFitPointInfo> infos(raw.confocal4.size());
  if (raw.part_type.toUpper() != QChar('A') || channelCount != 4 || pointCount <= 0) {
    return infos;
  }

  for (int ring = 0; ring < ringCount; ++ring) {
    for (int ch = 0; ch < channelCount; ++ch) {
      QVector<double> channelValues;
      QVector<int> sourceIndexes;
      channelValues.reserve(pointCount);
      sourceIndexes.reserve(pointCount);
      for (int pt = 0; pt < pointCount; ++pt) {
        const int idx = rawFlatIndex(channelCount, ringCount, pointCount,
                                     raw.conf_spec.order_code, ch, ring, pt);
        sourceIndexes.push_back(idx);
        channelValues.push_back((idx >= 0 && idx < raw.confocal4.size())
                                    ? static_cast<double>(raw.confocal4.at(idx))
                                    : qQNaN());
      }

      const bool leftEnd = (ch < 2);
      const bool innerChannel = (ch == 0 || ch == 2);
      const double inputOffset = innerChannel ? algo.a_inner_input_offset_mm
                                              : algo.a_outer_input_offset_mm;
      const core::DiameterAlgoParams params = leftEnd
          ? buildRawViewerDiameterParams(algo, pointCount, algo.a_b_k_in_mm, algo.a_b_k_out_mm)
          : buildRawViewerDiameterParams(algo, pointCount, algo.a_c_k_in_mm, algo.a_c_k_out_mm);
      fillDiameterFitPointInfoForChannel(channelValues,
                                         sourceIndexes,
                                         inputOffset,
                                         innerChannel,
                                         params,
                                         &infos);
    }
  }
  return infos;
}

QVector<RawRunoutFitPointInfo> buildRawRunoutFitPointInfo(
    const core::MeasurementSnapshot &raw,
    const core::AlgorithmConfig &algo,
    int channelCount,
    int ringCount,
    int pointCount) {
  QVector<RawRunoutFitPointInfo> infos(raw.runout2.size());
  if (raw.part_type.toUpper() != QChar('B') || channelCount != 2 || pointCount <= 0) {
    return infos;
  }

  for (int ring = 0; ring < ringCount; ++ring) {
    for (int ch = 0; ch < channelCount; ++ch) {
      QVector<double> channelValues;
      QVector<int> sourceIndexes;
      channelValues.reserve(pointCount);
      sourceIndexes.reserve(pointCount);
      for (int pt = 0; pt < pointCount; ++pt) {
        const int idx = rawFlatIndex(channelCount, ringCount, pointCount,
                                     raw.run_spec.order_code, ch, ring, pt);
        sourceIndexes.push_back(idx);
        channelValues.push_back((idx >= 0 && idx < raw.runout2.size())
                                    ? static_cast<double>(raw.runout2.at(idx))
                                    : qQNaN());
      }

      const double inputOffset = (ch == 0) ? algo.b_a_input_offset_mm : algo.b_d_input_offset_mm;
      const double kRunout = (ch == 0) ? algo.b_a_k_runout_mm : algo.b_d_k_runout_mm;
      fillRunoutFitPointInfoForChannel(channelValues,
                                       sourceIndexes,
                                       inputOffset,
                                       buildRawViewerRunoutParams(algo, pointCount, kRunout),
                                       &infos);
    }
  }
  return infos;
}

QStringList rawChannelNames(QChar partType) {
  if (partType.toUpper() == QChar('A')) {
    return {QStringLiteral("B端内径"), QStringLiteral("B端外径"),
            QStringLiteral("C端内径"), QStringLiteral("C端外径")};
  }
  if (partType.toUpper() == QChar('B')) {
    return {QStringLiteral("A点跳动"), QStringLiteral("D点跳动")};
  }
  return {};
}

int rawFlatIndex(int channelCount, int ringCount, int pointCount,
                 quint16 orderCode, int channel, int ring, int point) {
  if (channelCount <= 0 || ringCount <= 0 || pointCount <= 0) return -1;
  if (channel < 0 || channel >= channelCount ||
      ring < 0 || ring >= ringCount ||
      point < 0 || point >= pointCount) {
    return -1;
  }
  if (orderCode == 0) {
    return channel * ringCount * pointCount + ring * pointCount + point;
  }
  return ring * channelCount * pointCount + channel * pointCount + point;
}

QVector<QStringList> buildRawPointRows(const core::MeasurementSnapshot &raw,
                                       const core::AlgorithmConfig &algo) {
  const QChar partType = raw.part_type.toUpper();
  const QStringList names = rawChannelNames(partType);
  const QVector<float> values = (partType == QChar('A')) ? raw.confocal4 : raw.runout2;
  const core::ScanSpec spec = (partType == QChar('A')) ? raw.conf_spec : raw.run_spec;
  const int channelCount = names.size();
  const int ringCount = qMax(1, spec.rings);
  const int pointCount = qMax(1, spec.points_per_ring);
  const double stepDeg = spec.angle_step_deg > 0.0f
                             ? static_cast<double>(spec.angle_step_deg)
                             : 360.0 / static_cast<double>(pointCount);
  const QVector<RawFitPointInfo> diameterFitInfos =
      buildRawDiameterFitPointInfo(raw, algo, channelCount, ringCount, pointCount);
  const QVector<RawRunoutFitPointInfo> runoutFitInfos =
      buildRawRunoutFitPointInfo(raw, algo, channelCount, ringCount, pointCount);

  QVector<QStringList> rows;
  rows.reserve(channelCount * ringCount * pointCount);
  for (int ring = 0; ring < ringCount; ++ring) {
    for (int ch = 0; ch < channelCount; ++ch) {
      for (int pt = 0; pt < pointCount; ++pt) {
        const int idx = rawFlatIndex(channelCount, ringCount, pointCount,
                                     spec.order_code, ch, ring, pt);
        const double v = (idx >= 0 && idx < values.size())
                             ? static_cast<double>(values.at(idx))
                             : qQNaN();
        const bool valid = isRawPointValidForDisplay(v);
        const RawFitPointInfo fitInfo =
            (partType == QChar('A') && idx >= 0 && idx < diameterFitInfos.size())
                ? diameterFitInfos.at(idx)
                : ((partType == QChar('B') && idx >= 0 && idx < runoutFitInfos.size())
                       ? runoutFitInfos.at(idx)
                       : RawFitPointInfo{});
        rows.push_back({names.at(ch),
                        QString::number(ring + 1),
                        QString::number(pt + 1),
                        QString::number(stepDeg * pt, 'f', 3),
                        formatNumber(v),
                        valid ? QStringLiteral("Y") : QStringLiteral("N"),
                        formatNumber(fitInfo.fit_radius_mm),
                        formatNumber(fitInfo.point_radius_mm),
                        formatNumber(fitInfo.residual_mm)});
      }
    }
  }
  return rows;
}

QString csvEscape(const QString &text) {
  QString out = text;
  out.replace(QStringLiteral("\""), QStringLiteral("\"\""));
  if (out.contains(QChar(',')) || out.contains(QChar('"')) ||
      out.contains(QChar('\n')) || out.contains(QChar('\r'))) {
    out = QStringLiteral("\"%1\"").arg(out);
  }
  return out;
}

QString buildRawCsvText(const core::MeasurementSnapshot &raw,
                        const core::AlgorithmConfig &algo) {
  QStringList lines;
  lines << QStringLiteral("measurement_uuid,part_type,channel,ring,point,angle_deg,value_mm,valid,fit_radius_mm,point_radius_mm,residual_mm");
  const QVector<QStringList> rows = buildRawPointRows(raw, algo);
  for (const QStringList &row : rows) {
    QStringList cols;
    cols << raw.measurement_uuid << QString(raw.part_type.toUpper());
    cols << row;
    for (QString &col : cols) col = csvEscape(col);
    lines << cols.join(QStringLiteral(","));
  }
  return lines.join(QStringLiteral("\n")) + QStringLiteral("\n");
}

} // namespace

MainWindow::MainWindow(const core::AppConfig &cfg, const QString &iniPath,
                       MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath), appCfg_(cfg) {
  const QString idCheckCfg = appCfg_.mes.id_check_strategy.trimmed().toUpper();
  if (idCheckCfg == QStringLiteral("LOCAL_MOCK")) {
    idCheckStrategy_ = IdCheckStrategy::LocalMock;
  } else if (idCheckCfg == QStringLiteral("MES_STRICT")) {
    idCheckStrategy_ = IdCheckStrategy::MesStrict;
  } else {
    idCheckStrategy_ = IdCheckStrategy::Bypass;
  }
  ui_->setupUi(this);

  setMinimumSize(800, 500);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

  if (centralWidget()) {
    centralWidget()->setMinimumSize(0, 0);
    centralWidget()->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  }

  ui_->stackedWidget->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  ui_->navList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

  setWindowTitle(QStringLiteral("工件自动测量装置"));

  ui_->navList->clear();
  ui_->navList->addItems(
      {QStringLiteral("生产"), QStringLiteral("标定"), QStringLiteral("数据"),
       QStringLiteral("MES上传"), QStringLiteral("设置"),
       QStringLiteral("报警/事件"), QStringLiteral("诊断"),
       QStringLiteral("RAW查看"), QStringLiteral("开发调试"),
       QStringLiteral("手动/维护"), QStringLiteral("报表(待做)"),
       QStringLiteral("用户权限(待做)")});
  ui_->navList->setIconSize(QSize(18,18));
  ui_->navList->setSpacing(6);
  ui_->navList->setStyleSheet(QStringLiteral("QListWidget{background:#f8fafc;border:1px solid #e5e7eb;border-radius:12px;padding:8px;} QListWidget::item{padding:10px 12px;border-radius:10px;font-weight:600;} QListWidget::item:selected{background:#dbeafe;color:#111827;} QListWidget::item:hover{background:#eff6ff;}"));
  const QList<QStyle::StandardPixmap> navIcons = {QStyle::SP_DesktopIcon, QStyle::SP_FileDialogDetailedView, QStyle::SP_DirIcon, QStyle::SP_DialogSaveButton, QStyle::SP_FileDialogContentsView, QStyle::SP_MessageBoxWarning, QStyle::SP_MessageBoxInformation, QStyle::SP_DriveHDIcon, QStyle::SP_ComputerIcon, QStyle::SP_ToolBarHorizontalExtensionButton, QStyle::SP_FileDialogListView, QStyle::SP_DialogOpenButton};
  for (int i = 0; i < ui_->navList->count() && i < navIcons.size(); ++i) { ui_->navList->item(i)->setIcon(style()->standardIcon(navIcons.at(i))); }

  productionWidget_ = new ProductionWidget(cfg, ui_->stackedWidget);
  calibrationWidget_ = new CalibrationWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(productionWidget_);
  ui_->stackedWidget->addWidget(calibrationWidget_);
  dataWidget_ = new DataWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(dataWidget_);
  ui_->stackedWidget->addWidget(
      new MesUploadWidget(cfg, worker, ui_->stackedWidget));
  settingsWidget_ = new SettingsWidget(cfg, iniPath_, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(settingsWidget_);
  connect(settingsWidget_, &SettingsWidget::configApplied, this, [this](const core::AppConfig &newCfg){
    appCfg_ = newCfg;
    const QString idCheckCfg = appCfg_.mes.id_check_strategy.trimmed().toUpper();
    if (idCheckCfg == QStringLiteral("LOCAL_MOCK")) {
      idCheckStrategy_ = IdCheckStrategy::LocalMock;
    } else if (idCheckCfg == QStringLiteral("MES_STRICT")) {
      idCheckStrategy_ = IdCheckStrategy::MesStrict;
    } else {
      idCheckStrategy_ = IdCheckStrategy::Bypass;
    }
    if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
      mesExpectedPartIds_.clear();
    }
    appendProductionLog(QStringLiteral("设置已应用：算法参数已更新；ID核对策略=%1").arg(idCheckStrategyText()));
  });
  connect(settingsWidget_, &SettingsWidget::configSaved, this, [this](const QString &path){
    appendProductionLog(QStringLiteral("配置已保存：%1").arg(path));
  });
  ui_->stackedWidget->addWidget(new AlarmWidget(cfg, ui_->stackedWidget));
  diagnosticsWidget_ = new DiagnosticsWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(diagnosticsWidget_);
  rawViewerWidget_ = new RawViewerWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(rawViewerWidget_);
  devToolsWidget_ = new DevToolsWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(devToolsWidget_);

  connect(dataWidget_, &DataWidget::requestOpenRaw, this,
          &MainWindow::openRawUuidForViewer);
  connect(rawViewerWidget_, &RawViewerWidget::requestOpenRaw, this,
          &MainWindow::openRawFileForViewer);
  connect(rawViewerWidget_, &RawViewerWidget::requestComputeRaw, this,
          &MainWindow::computeLoadedRawForViewer);
  connect(rawViewerWidget_, &RawViewerWidget::requestExportCsv, this,
          &MainWindow::exportLoadedRawCsvForViewer);

  manualMaintainWidget_ = new ManualMaintainWidget(ui_->stackedWidget);
  ui_->stackedWidget->addWidget(manualMaintainWidget_);
  ui_->stackedWidget->addWidget(new TodoWidget(
      QStringLiteral("统计报表（TODO）"),
      QStringLiteral("后续将增加良率、趋势、SPC/CPK 等统计视图。"),
      ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new TodoWidget(QStringLiteral("用户与权限（TODO）"),
                     QStringLiteral("后续将增加登录、角色权限控制与操作审计。"),
                     ui_->stackedWidget));

  connect(ui_->navList, &QListWidget::currentRowChanged, ui_->stackedWidget,
          &QStackedWidget::setCurrentIndex);

  ui_->navList->setCurrentRow(0);

  lbDb_ = new QLabel(QStringLiteral("数据库: 已连接"), this);
  lbPlc_ = new QLabel(QStringLiteral("PLC: 未启用"), this);
  lbMes_ = new QLabel(QStringLiteral("MES: -"), this);
  statusBar()->addPermanentWidget(lbDb_);
  statusBar()->addPermanentWidget(lbPlc_);
  statusBar()->addPermanentWidget(lbMes_);

  auto *actToggleFullScreen = new QAction(this);
  actToggleFullScreen->setShortcut(QKeySequence(Qt::Key_F11));
  addAction(actToggleFullScreen);
  connect(actToggleFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    } else {
      showFullScreen();
    }
  });

  auto *actExitFullScreen = new QAction(this);
  actExitFullScreen->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(actExitFullScreen);
  connect(actExitFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    }
  });

  setupPlcRuntime(cfg);
  setupDiagnosticsBindings();
  setupBusinessPageBindings();
  appendProductionLog(QStringLiteral("ID核对策略：%1").arg(idCheckStrategyText()));
  if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
    appendProductionLog(QStringLiteral("LOCAL_MOCK 文件：%1").arg(resolveIdCheckMockFilePath()));
  }
}

MainWindow::~MainWindow() {
  if (plcRuntime_) {
    plcRuntime_->stop();
  }
  delete ui_;
}

void MainWindow::setupPlcRuntime(const core::AppConfig &cfg) {
  plcRuntime_ = std::make_unique<core::PlcRuntimeServiceV2>(cfg, this);
  updatePlcStatusLabel();

  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::connectionChanged,
          this, [this](bool connected) {
            updatePlcStatusLabel();
            if (productionWidget_) productionWidget_->setPlcConnected(connected);
            if (calibrationWidget_) calibrationWidget_->setPlcConnected(connected);
            if (!connected) {
              awaitingCmdReply_ = false;
              pendingCmdBits_ = 0;
              hasLastMailboxSnapshot_ = false;
              lastMailboxSnapshot_.reset();
              hasLastTray_ = false;
              calibrationFlowExpected_ = false;
              calibrationAutoState_ = CalibrationAutoState::Idle;
            }
            if (manualMaintainWidget_) {
              const bool calCtx = hasLastStatus_
                                  && isCalibrationStepCode(lastStatus_.step_state, calibrationFlowExpected_);
              manualMaintainWidget_->setRuntimeSummary(connected,
                                                       hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                                       hasLastStatus_ ? stepStateText(lastStatus_.step_state, lastStatus_.scan_done, calCtx) : QStringLiteral("-"));
            }
            if (!lastPlcConnectedKnown_ || lastPlcConnected_ != connected) {
              appendProductionLog(connected ? QStringLiteral("PLC 连接成功") : QStringLiteral("PLC 已断开"));
              lastPlcConnectedKnown_ = true;
              lastPlcConnected_ = connected;
              reconnectAttemptLogged_ = false;
            }
          });
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::errorOccurred,
          this, &MainWindow::handlePlcRuntimeError);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statsUpdated,
          this, &MainWindow::onPlcStatsUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statusUpdated,
          this, &MainWindow::onPlcStatusUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::commandUpdated,
          this, &MainWindow::onPlcCommandUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::trayUpdated,
          this, &MainWindow::onPlcTrayUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::mailboxSnapshotUpdated,
          this, &MainWindow::onPlcMailboxSnapshotUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::plcEventsRaised,
          this, &MainWindow::onPlcEventsRaised);

  if (!cfg.plc.enabled) {
    return;
  }

  appendProductionLog(QStringLiteral("正在连接 PLC..."));
  QString err;
  if (!plcRuntime_->start(&err)) {
    handlePlcRuntimeError(err);
    updatePlcStatusLabel();
    return;
  }

  if (!plcRuntime_->connectNow(&err)) {
    handlePlcRuntimeError(err);
  }
  updatePlcStatusLabel();
}

void MainWindow::setupDiagnosticsBindings() {
  if (!plcRuntime_) {
    return;
  }

  if (diagnosticsWidget_) {
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestRefresh,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().trimmed().isEmpty() ? QChar('A') : productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
  }

  if (manualMaintainWidget_) {
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestSetPlcMode, this, [this](int mode) {
      QString err;
      if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(mode)));
      plcRuntime_->pollOnce();
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcReloadSlotIds,
            this, [this] {
              core::PlcTrayPartIdBlockV2 tray; QString err;
              if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) { handlePlcRuntimeError(err); return; }
              if (manualMaintainWidget_) {
                manualMaintainWidget_->appendLog(QStringLiteral("读取扫码ID成功："));
                for (int i = 0; i < core::kLogicalSlotCount; ++i) {
                  const QString id = tray.part_ids[i].trimmed().isEmpty() ? QStringLiteral("NG") : tray.part_ids[i].trimmed();
                  manualMaintainWidget_->appendLog(QStringLiteral("  槽位%1: %2").arg(i + 1).arg(id));
                }
              }
            });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcContinueAfterIdCheck,
            this, [this] {
              QString err;
              if (!plcRuntime_->writeScanDone(0, &err)) { handlePlcRuntimeError(err); return; }
              if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
              plcRuntime_->pollOnce();
            });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcNamedCommand, this, &MainWindow::handleUiCommandRequested);
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisCommand, this, [this](int axisIndex, const QString &action) {
      if (!plcRuntime_) return;
      QString err;
      if (action == QStringLiteral("ENABLE_ON")) {
        if (!plcRuntime_->axisSetEnable(axisIndex, true, &err)) { handlePlcRuntimeError(err); return; }
      } else if (action == QStringLiteral("ENABLE_OFF")) {
        if (!plcRuntime_->axisSetEnable(axisIndex, false, &err)) { handlePlcRuntimeError(err); return; }
      } else {
        if (!plcRuntime_->axisPulseAction(axisIndex, action, &err)) { handlePlcRuntimeError(err); return; }
      }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴控制：%1 action=%2").arg(axisNameByIndex(axisIndex), action));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisJog, this, [this](int axisIndex, const QString &direction, bool active) {
      if (!plcRuntime_) return;
      QString err;
      const bool forward = (direction != QStringLiteral("JOG_BWD"));
      if (!plcRuntime_->axisJog(axisIndex, forward, active, &err)) { handlePlcRuntimeError(err); }
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisMove, this, [this](int axisIndex, const QString &action, double acc, double dec, double pos, double vel) {
      if (!plcRuntime_) return;
      QString err;
      const bool relative = (action == QStringLiteral("MOVE_REL"));
      if (!plcRuntime_->axisMove(axisIndex, relative, acc, dec, pos, vel, &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴运动：%1 action=%2 acc=%3 dec=%4 pos=%5 vel=%6").arg(axisNameByIndex(axisIndex)).arg(action).arg(acc,0,'f',3).arg(dec,0,'f',3).arg(pos,0,'f',3).arg(vel,0,'f',3));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestCylinderCommand, this, [this](const QString &group, int index, const QString &action) {
      if (!plcRuntime_) return;
      QString err;
      if (!plcRuntime_->cylinderAction(group, index, action, &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写气缸控制：%1 action=%2").arg(core::plc_v26::cylinderName(group, index), action));
    });
  }
}

void MainWindow::setupBusinessPageBindings() {
  if (!plcRuntime_) {
    return;
  }

  if (productionWidget_) {
    connect(productionWidget_, &ProductionWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A'), false); });
    connect(productionWidget_, &ProductionWidget::requestSaveTestRaw,
            this, &MainWindow::handleSaveTestRawRequested);
    connect(productionWidget_, &ProductionWidget::requestReloadSlotIds,
            this, [this] {
              core::PlcTrayPartIdBlockV2 tray; QString err;
              if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) { handlePlcRuntimeError(err); return; }
              onPlcTrayUpdated(tray);
              const quint16 presentMask = hasLastStatus_ ? lastStatus_.tray_present_mask : 0xFFFFu;
              productionWidget_->appendPlcLogMessage(QStringLiteral("读取扫码ID成功："));
              for (int i = 0; i < core::kLogicalSlotCount; ++i) {
                const bool present = ((presentMask >> i) & 0x1u) != 0;
                if (!present) continue;
                const QString id = tray.part_ids[i].trimmed().isEmpty()
                                       ? QStringLiteral("NG")
                                       : tray.part_ids[i].trimmed();
                productionWidget_->appendPlcLogMessage(QStringLiteral("  槽位%1：%2").arg(i + 1).arg(id));
              }
            });
    connect(productionWidget_, &ProductionWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
    connect(productionWidget_, &ProductionWidget::requestContinueAfterIdCheck,
            this, [this]{ handleUiCommandRequested(QStringLiteral("CONTINUE_AFTER_ID_CHECK"), {}); });
    connect(productionWidget_, &ProductionWidget::requestSetPlcMode,
            this, [this](int mode){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(mode))); plcRuntime_->pollOnce(); });
    connect(productionWidget_, &ProductionWidget::requestWriteCategoryMode,
            this, [this](int partTypeArg){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->setCategoryMode(static_cast<qint16>(partTypeArg), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写工件类型到 PLC：%1").arg(partTypeArg == 1 ? QStringLiteral("B型") : QStringLiteral("A型"))); });
    connect(productionWidget_, &ProductionWidget::requestWriteSlotId,
            this, &MainWindow::handleWriteTrayPartIdRequested);
    connect(productionWidget_, &ProductionWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
    connect(productionWidget_, &ProductionWidget::requestReconnectPlc,
            this, [this]{ attemptReconnectPlc(true); });
  }

  if (calibrationWidget_) {
    connect(calibrationWidget_, &CalibrationWidget::requestReconnectPlc,
            this, [this]{
              if (!plcRuntime_) return;
              appendCalibrationLog(QStringLiteral("手动重连 PLC..."));
              QString err;
              plcRuntime_->disconnectNow();
              if (!plcRuntime_->connectNow(&err)) {
                handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("PLC 重连失败") : err);
                return;
              }
              reconnectAttemptLogged_ = false;
              updatePlcStatusLabel();
              plcRuntime_->pollOnce();
            });
    connect(calibrationWidget_, &CalibrationWidget::requestWriteCategoryMode,
            this, [this](int partTypeArg){
              if (!plcRuntime_) return;
              QString err;
              if (!plcRuntime_->setCategoryMode(static_cast<qint16>(partTypeArg), &err)) {
                handlePlcRuntimeError(err);
                return;
              }
              appendCalibrationLog(QStringLiteral("写工件类型到 PLC：%1")
                                       .arg(partTypeArg == 1 ? QStringLiteral("B型") : QStringLiteral("A型")));
            });
    connect(calibrationWidget_, &CalibrationWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(calibrationWidget_ ? calibrationWidget_->selectedPartTypeText().at(0) : QChar('A'), true); });
    connect(calibrationWidget_, &CalibrationWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(true); });
    connect(calibrationWidget_, &CalibrationWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
  }
}


void MainWindow::appendProductionLog(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return;
  if (productionWidget_) productionWidget_->appendPlcLogMessage(trimmed);
}

void MainWindow::appendCalibrationLog(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return;
  if (calibrationWidget_) calibrationWidget_->appendLogMessage(trimmed);
}

void MainWindow::attemptReconnectPlc(bool manual) {
  if (!plcRuntime_) return;
  if (manual) appendProductionLog(QStringLiteral("手动重连 PLC..."));
  QString err;
  plcRuntime_->disconnectNow();
  if (!plcRuntime_->connectNow(&err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("PLC 重连失败") : err);
    return;
  }
  reconnectAttemptLogged_ = false;
  updatePlcStatusLabel();
  plcRuntime_->pollOnce();
}

void MainWindow::updatePlcStatusLabel() {
  if (!lbPlc_) {
    return;
  }
  if (!plcRuntime_) {
    lbPlc_->setText(QStringLiteral("PLC: -"));
    return;
  }
  const auto &cfg = plcRuntime_->config().plc;
  if (!cfg.enabled) {
    lbPlc_->setText(QStringLiteral("PLC: 未启用"));
    return;
  }
  const QString mode = QStringLiteral("Real");
  const QString conn = plcRuntime_->isConnected() ? QStringLiteral("已连接") : QStringLiteral("未连接");
  lbPlc_->setText(QStringLiteral("PLC: %1 / %2").arg(mode, conn));
}

void MainWindow::handlePlcRuntimeError(const QString &message) {
  if (!message.trimmed().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("PLC: %1").arg(message), 5000);
    const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
    if (calibrationContext) appendCalibrationLog(message);
    else appendProductionLog(message);
  }
}

void MainWindow::onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats) {
  updatePlcStatusLabel();
  if (productionWidget_) {
    productionWidget_->setPlcConnected(stats.connected);
    if (hasLastStatus_) productionWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
  if (calibrationWidget_) {
    calibrationWidget_->setPlcConnected(stats.connected);
    if (hasLastStatus_) calibrationWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
  if (diagnosticsWidget_) {
    const int pollHz = (stats.poll_interval_ms > 0) ? (1000 / stats.poll_interval_ms) : 0;
    diagnosticsWidget_->setCommStats(pollHz, stats.last_poll_ms,
                                     stats.poll_ok_count, stats.poll_error_count);
  }
  if (!stats.connected && plcRuntime_ && plcRuntime_->config().plc.auto_reconnect && !reconnectAttemptLogged_) {
    const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
    if (calibrationContext) appendCalibrationLog(QStringLiteral("尝试重新连接 PLC..."));
    else appendProductionLog(QStringLiteral("尝试重新连接 PLC..."));
    reconnectAttemptLogged_ = true;
  }
  if (manualMaintainWidget_) {
    const bool calCtx = hasLastStatus_
                        && isCalibrationStepCode(lastStatus_.step_state, calibrationFlowExpected_);
    manualMaintainWidget_->setRuntimeSummary(stats.connected,
                                             hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                             hasLastStatus_ ? stepStateText(lastStatus_.step_state, lastStatus_.scan_done, calCtx) : QStringLiteral("-"));
    if (hasLastStatus_) manualMaintainWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
}

void MainWindow::onPlcStatusUpdated(const core::PlcStatusBlockV2 &status) {
  const bool hadPrev = hasLastStatus_;
  const core::PlcStatusBlockV2 prev = lastStatus_;
  lastStatus_ = status;
  hasLastStatus_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, status.step_state);
  auto appendFlowLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  updateCalibrationAutoState(status.step_state);
  if (manualMaintainWidget_) {
    manualMaintainWidget_->setRuntimeSummary(plcRuntime_ ? plcRuntime_->isConnected() : false,
                                             machineStateText(status.machine_state),
                                             stepStateText(status.step_state, status.scan_done, calibrationContext));
    manualMaintainWidget_->setCurrentPlcMode(status.control_mode);
    refreshManualMaintainLiveStatus();
  }
  if (diagnosticsWidget_) {
    diagnosticsWidget_->setStatusFields(static_cast<int>(status.step_state),
                                        static_cast<int>(status.machine_state),
                                        static_cast<int>(status.alarm_code),
                                        0,
                                        static_cast<quint32>(status.interlock_mask),
                                        static_cast<int>(status.mailbox_ready));
  }

  if (hadPrev && prev.step_state != status.step_state) {
    appendFlowLog(QStringLiteral("流程步骤变化：%1 -> %2 (code=%3)")
                      .arg(stepStateText(prev.step_state, prev.scan_done, calibrationContext))
                      .arg(stepStateText(status.step_state, status.scan_done, calibrationContext))
                      .arg(status.step_state));
  }
  if (hadPrev && prev.interlock_mask != status.interlock_mask) {
    if (status.interlock_mask == 0) {
      appendFlowLog(QStringLiteral("互锁位图恢复正常"));
    } else {
      appendFlowLog(QStringLiteral("互锁位图触发：0x%1 (%2)")
                        .arg(QString::number(status.interlock_mask, 16).toUpper())
                        .arg(interlockMaskText(status.interlock_mask)));
    }
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->setMachineState(status.machine_state, machineStateText(status.machine_state));
    productionWidget_->setStateSeq(0);
    productionWidget_->setAlarm(status.alarm_code, 0);
    productionWidget_->setInterlockMask(status.interlock_mask);
    productionWidget_->setCurrentPlcMode(status.control_mode);
    productionWidget_->applyPlcRuntimeSnapshot(status.step_state,
                                               status.scan_done,
                                               status.tray_present_mask,
                                               status.active_slot_mask,
                                               calibrationContext,
                                               status.mailbox_ready);
  }

  if (calibrationWidget_) {
    calibrationWidget_->setStepState(status.step_state);
    calibrationWidget_->setAlarm(status.alarm_code, 0);
    calibrationWidget_->setCurrentPlcMode(status.control_mode);
    calibrationWidget_->setTrayPresentMask(status.tray_present_mask);
    // 标定页日志仅记录标定上下文内的 mailbox 变化，避免生产流程日志串入标定页。
    if (calibrationContext && status.mailbox_ready != lastMailboxReady_) {
      calibrationWidget_->setMailboxReady(status.mailbox_ready != 0);
    }
    lastMailboxReady_ = status.mailbox_ready;
  }
}

void MainWindow::onPlcCommandUpdated(const core::PlcCommandBlockV2 &command) {
  if (command.category_mode == core::plc_v26::kPartTypeA ||
      command.category_mode == core::plc_v26::kPartTypeB) {
    lastCategoryMode_ = command.category_mode;
  }
  const bool unchanged = hasLastCommandSample_
                      && lastCmdResult_ == command.cmd_result
                      && lastRejectInstruction_ == command.cmd_error_code;
  if (unchanged && !awaitingCmdReply_) {
    return;
  }
  hasLastCommandSample_ = true;
  lastCmdResult_ = command.cmd_result;
  lastRejectInstruction_ = command.cmd_error_code;

  if (!awaitingCmdReply_) {
    return;
  }
  if (command.cmd_result == 0) {
    return;
  }

  const QString resultHex = QStringLiteral("0x%1").arg(QString::number(command.cmd_result, 16).toUpper());
  const QString pendingHex = QStringLiteral("0x%1").arg(QString::number(pendingCmdBits_, 16).toUpper());
  const bool rejected = (command.cmd_result & core::plc_v26::kCmdRejectMask) != 0;

  QString line;
  if (rejected) {
    line = QStringLiteral("PLC 命令回复：拒绝 result=%1 pending=%2 reject=0x%3 reason=%4")
               .arg(resultHex)
               .arg(pendingHex)
               .arg(QString::number(command.cmd_error_code, 16).toUpper())
               .arg(rejectInstructionText(command.cmd_error_code));
  } else if ((command.cmd_result & pendingCmdBits_) == pendingCmdBits_) {
    line = QStringLiteral("PLC 命令回复：接收成功 result=%1 (%2)")
               .arg(resultHex)
               .arg(cmdBitsText(command.cmd_result));
  } else {
    return;
  }

  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0)
                               || calibrationFlowExpected_;
  if (calibrationContext) appendCalibrationLog(line);
  else appendProductionLog(line);
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(line);
  awaitingCmdReply_ = false;
  pendingCmdBits_ = 0;
}


void MainWindow::refreshManualMaintainLiveStatus() {
  if (!manualMaintainWidget_ || !plcRuntime_ || !plcRuntime_->isConnected()) return;
  QString err;
  QStringList axisLines;
  for (int axisIndex = 0; axisIndex < core::plc_v26::kAxisCount; ++axisIndex) {
    core::PlcAxisStateV26 s;
    if (!plcRuntime_->readAxisState(axisIndex, &s, &err)) { handlePlcRuntimeError(err); break; }
    axisLines << QStringLiteral("%1 | En=%2 Homed=%3 Err=%4 Busy=%5 Done=%6 ErrId=%7 Pos=%8 Vel=%9")
                     .arg(s.axis_name)
                     .arg(s.enabled ? 1 : 0)
                     .arg(s.homed ? 1 : 0)
                     .arg(s.error ? 1 : 0)
                     .arg(s.busy ? 1 : 0)
                     .arg(s.done ? 1 : 0)
                     .arg(s.error_id)
                     .arg(s.act_position, 0, 'f', 3)
                     .arg(s.act_velocity, 0, 'f', 3);
  }
  manualMaintainWidget_->setAxisStatesText(axisLines.join(QStringLiteral("\n")));

  QStringList cylLines;
  auto appendCyl = [&](const QString &group, int index) -> bool {
    core::PlcCylinderStateV26 s;
    if (!plcRuntime_->readCylinderState(group, index, &s, &err)) return false;
    cylLines << QStringLiteral("%1 | P=%2 N=%3 Err=%4 ErrId=%5")
                    .arg(s.name)
                    .arg(s.p ? 1 : 0)
                    .arg(s.n ? 1 : 0)
                    .arg(s.error ? 1 : 0)
                    .arg(s.error_id);
    return true;
  };
  for (int i = 0; i < core::plc_v26::kClCylinderCount; ++i) if (!appendCyl(QStringLiteral("CL"), i)) { handlePlcRuntimeError(err); return; }
  for (int i = 0; i < core::plc_v26::kGt2CylinderCount; ++i) if (!appendCyl(QStringLiteral("GT2"), i)) { handlePlcRuntimeError(err); return; }
  manualMaintainWidget_->setCylinderStatesText(cylLines.join(QStringLiteral("\n")));
}

void MainWindow::onPlcTrayUpdated(const core::PlcTrayPartIdBlockV2 &tray) {
  lastTray_ = tray;
  hasLastTray_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  if (productionWidget_ && !calibrationContext) {
    QVector<QString> slotIds(core::kLogicalSlotCount);
    const quint16 presentMask = hasLastStatus_ ? lastStatus_.tray_present_mask : 0xFFFFu;
    for (int i = 0; i < core::kLogicalSlotCount; ++i) {
      const bool present = ((presentMask >> i) & 0x1u) != 0;
      if (!present) continue; // 无料槽位不写入
      const QString id = tray.part_ids[i].trimmed();
      // 有料槽位必须落位：空值按 NG 显示，便于人工修正
      slotIds[i] = id.isEmpty() ? QStringLiteral("NG") : id;
    }
    productionWidget_->setScannedPartIds(slotIds);
  }
}

void MainWindow::onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot) {
  if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
  *lastMailboxSnapshot_ = snapshot;
  hasLastMailboxSnapshot_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);

  QString slot0 = QStringLiteral("-");
  QString slot1 = QStringLiteral("-");
  QString partId0;
  QString partId1;

  if (!snapshot.items.isEmpty()) {
    slot0 = snapshot.items.at(0).slot_index >= 0 ? QString::number(snapshot.items.at(0).slot_index + 1) : QStringLiteral("-");
    partId0 = snapshot.items.at(0).part_id;
  }
  if (snapshot.items.size() > 1) {
    slot1 = snapshot.items.at(1).slot_index >= 0 ? QString::number(snapshot.items.at(1).slot_index + 1) : QStringLiteral("-");
    partId1 = snapshot.items.at(1).part_id;
  }

  if (diagnosticsWidget_) {
    diagnosticsWidget_->setMailboxPreview(mailboxPartTypeText(snapshot), slot0, slot1, partId0, partId1);
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->setMeasureDone(true);
  }

  if (calibrationWidget_ && calibrationContext) {
    for (const auto &item : snapshot.items) {
      if (item.slot_index == core::kCalibrationSlotIndex) {
        core::CalibrationSlotSummary s;
        s.slot_index = item.slot_index;
        s.calibration_type = QString(snapshot.part_type.toUpper());
        s.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
        s.measured_part_id = item.part_id;
        s.valid = false;
        calibrationWidget_->setSlotSummary(s);
        break;
      }
    }
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->appendPlcLogMessage(QStringLiteral("Mailbox 已解析：part=%1 slot0=%2 slot1=%3")
                                               .arg(mailboxPartTypeText(snapshot), slot0, slot1));
  }

  if (calibrationContext) {
    const bool singleItem = (snapshot.item_count == 1 && snapshot.items.size() == 1);
    const bool slotCalibrationOnly = singleItem && snapshot.items.at(0).slot_index == core::kCalibrationSlotIndex;
    if (!slotCalibrationOnly) {
      appendCalibrationLog(QStringLiteral("标定邮箱规则异常：标定流程仅允许槽位%1单件数据（item_count=%2, items=%3）")
                               .arg(core::kCalibrationSlotIndex + 1)
                               .arg(snapshot.item_count)
                               .arg(snapshot.items.size()));
      if (!snapshot.items.isEmpty()) {
        appendCalibrationLog(QStringLiteral("标定邮箱异常详情：slot=%1 part_id=%2")
                                 .arg(snapshot.items.first().slot_index + 1)
                                 .arg(snapshot.items.first().part_id));
      }
    }
  }
}

void MainWindow::onPlcEventsRaised(const core::PlcPollEventsV26 &events) {
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  if (events.scan_ready) {
    if (calibrationContext) {
      appendCalibrationLog(QStringLiteral("检测到扫码事件（标定流程不参与扫码核对）"));
    } else {
      appendProductionLog(QStringLiteral("检测到新扫码结果"));
      processAutoScanIdCheck();
    }
  }
  if (events.new_mailbox) {
    if (calibrationContext) {
      appendCalibrationLog(QStringLiteral("检测到新测量包，按标定流程处理"));
    } else {
      appendProductionLog(QStringLiteral("检测到新测量包，等待业务处理/ACK"));
    }
    processAutoMailboxFlow();
  }
}

void MainWindow::updateCalibrationAutoState(quint16 stepState) {
  if (!calibrationFlowExpected_) {
    calibrationAutoState_ = CalibrationAutoState::Idle;
    return;
  }
  CalibrationAutoState next = calibrationAutoState_;
  switch (stepState) {
  case plc_step_rules_v26::kStepCalPickFromRack:
  case plc_step_rules_v26::kStepCalLoadMeasureStation:
    next = CalibrationAutoState::WaitLoadSlot16;
    break;
  case plc_step_rules_v26::kStepCalMeasureA:
  case plc_step_rules_v26::kStepCalMeasureB:
  case plc_step_rules_v26::kStepCalMeasureLength:
    next = CalibrationAutoState::Measuring;
    break;
  case plc_step_rules_v26::kStepCalArchiveWaitDecisionA:
  case plc_step_rules_v26::kStepCalArchiveWaitDecisionB:
    next = CalibrationAutoState::WaitPcRead;
    break;
  case plc_step_rules_v26::kStepCalReturnToRack:
    next = CalibrationAutoState::Completed;
    break;
  default:
    if (!isCalibrationStepCode(stepState, calibrationFlowExpected_)) {
      next = CalibrationAutoState::Idle;
      calibrationFlowExpected_ = false;
    }
    break;
  }
  if (next == calibrationAutoState_) return;
  calibrationAutoState_ = next;
  QString text;
  switch (calibrationAutoState_) {
  case CalibrationAutoState::Idle: text = QStringLiteral("空闲"); break;
  case CalibrationAutoState::WaitLoadSlot16:
    text = QStringLiteral("等待槽%1上料").arg(core::kCalibrationSlotIndex + 1);
    break;
  case CalibrationAutoState::WaitPcConfirm: text = QStringLiteral("等待PC确认"); break;
  case CalibrationAutoState::Measuring: text = QStringLiteral("标定测量中"); break;
  case CalibrationAutoState::WaitPcRead: text = QStringLiteral("等待PC读取"); break;
  case CalibrationAutoState::Completed: text = QStringLiteral("标定完成"); break;
  }
  appendCalibrationLog(QStringLiteral("标定状态机切换：%1").arg(text));
}

qint16 MainWindow::currentCategoryModeForAutoFlow() const {
  if (lastCategoryMode_ == core::plc_v26::kPartTypeA ||
      lastCategoryMode_ == core::plc_v26::kPartTypeB) {
    return lastCategoryMode_;
  }
  if (productionWidget_) {
    const quint32 arg = productionWidget_->selectedPartTypeArg();
    if (arg == core::plc_v26::kPartTypeA || arg == core::plc_v26::kPartTypeB) {
      return static_cast<qint16>(arg);
    }
  }
  return core::plc_v26::kPartTypeA;
}

QString MainWindow::idCheckStrategyText() const {
  switch (idCheckStrategy_) {
  case IdCheckStrategy::LocalMock:
    return QStringLiteral("LOCAL_MOCK");
  case IdCheckStrategy::MesStrict:
    return QStringLiteral("MES_STRICT");
  case IdCheckStrategy::Bypass:
  default:
    return QStringLiteral("BYPASS");
  }
}

QString MainWindow::resolveIdCheckMockFilePath() const {
  const QString raw = appCfg_.mes.id_check_mock_file.trimmed();
  if (raw.isEmpty()) {
    return QFileInfo(QFileInfo(iniPath_).absolutePath(),
                     QStringLiteral("mes_id_mock.json")).absoluteFilePath();
  }
  QFileInfo fi(raw);
  if (fi.isAbsolute()) return fi.absoluteFilePath();
  return QFileInfo(QFileInfo(iniPath_).absolutePath(), raw).absoluteFilePath();
}

bool MainWindow::loadMockExpectedPartIds(QVector<QString> *out, QString *err) const {
  if (!out) {
    if (err) *err = QStringLiteral("out 不能为空");
    return false;
  }
  out->fill(QString(), core::kLogicalSlotCount);
  const QString path = resolveIdCheckMockFilePath();
  QFile f(path);
  if (!f.exists()) {
    if (err) *err = QStringLiteral("LOCAL_MOCK 文件不存在：%1").arg(path);
    return false;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    if (err) *err = QStringLiteral("LOCAL_MOCK 文件打开失败：%1").arg(path);
    return false;
  }
  const QByteArray bytes = f.readAll();
  f.close();

  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (err) *err = QStringLiteral("LOCAL_MOCK JSON解析失败：%1 (line=%2)")
                        .arg(parseErr.errorString())
                        .arg(parseErr.offset);
    return false;
  }
  const QJsonObject obj = doc.object();

  auto applyPair = [&](int slot1Based, const QString &id) {
    if (slot1Based < 1 || slot1Based > core::kLogicalSlotCount) return;
    (*out)[slot1Based - 1] = id.trimmed();
  };

  const QJsonValue slotIdsVal = obj.value(QStringLiteral("slot_ids"));
  if (slotIdsVal.isObject()) {
    const QJsonObject m = slotIdsVal.toObject();
    for (int slot = 1; slot <= core::kLogicalSlotCount; ++slot) {
      const QString key = QString::number(slot);
      if (!m.contains(key)) continue;
      applyPair(slot, m.value(key).toString());
    }
  }

  const QJsonValue slotsVal = obj.value(QStringLiteral("slots"));
  if (slotsVal.isArray()) {
    const QJsonArray arr = slotsVal.toArray();
    for (int i = 0; i < arr.size() && i < core::kLogicalSlotCount; ++i) {
      const QJsonValue v = arr.at(i);
      if (v.isString()) {
        applyPair(i + 1, v.toString());
        continue;
      }
      if (v.isObject()) {
        const QJsonObject one = v.toObject();
        const int slot = one.value(QStringLiteral("slot")).toInt(i + 1);
        const QString id = one.value(QStringLiteral("part_id")).toString(
            one.value(QStringLiteral("partId")).toString(
                one.value(QStringLiteral("id")).toString()));
        applyPair(slot, id);
      }
    }
  }

  return true;
}

bool MainWindow::evaluateIdCheckAgainstMes(QStringList *mismatchDetails,
                                           QVector<int> *mismatchSlots) const {
  if (!mismatchDetails || !mismatchSlots) return false;
  mismatchDetails->clear();
  mismatchSlots->clear();
  if (!hasLastStatus_ || !hasLastTray_) return false;
  if (mesExpectedPartIds_.size() != core::kLogicalSlotCount) return false;

  const quint16 presentMask = lastStatus_.tray_present_mask;
  for (int slot = 0; slot < core::kLogicalSlotCount; ++slot) {
    const bool present = ((presentMask >> slot) & 0x1u) != 0;
    if (!present) continue;
    const QString expected = mesExpectedPartIds_.at(slot).trimmed();
    if (expected.isEmpty()) continue;
    QString actual = lastTray_.part_ids[slot].trimmed();
    if (actual.isEmpty()) actual = QStringLiteral("NG");
    if (actual != expected) {
      mismatchSlots->push_back(slot);
      mismatchDetails->push_back(
          QStringLiteral("槽位%1：扫描ID=%2，MES期望=%3")
              .arg(slot + 1)
              .arg(actual)
              .arg(expected));
    }
  }
  return mismatchSlots->isEmpty();
}

bool MainWindow::tryAutoContinueAfterIdCheck() {
  if (!plcRuntime_) return false;
  QString err;
  if (!plcRuntime_->writeScanDone(0, &err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 scan_done=0 失败") : err);
    return false;
  }
  appendProductionLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
  plcRuntime_->pollOnce();
  return true;
}

bool MainWindow::tryAutoWritePcAck() {
  if (!plcRuntime_) return false;
  QString err;
  if (!plcRuntime_->sendPcAck(core::plc_v26::kJudgeOk, &err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 iPc_Ack 失败") : err);
    return false;
  }
  appendProductionLog(QStringLiteral("写 iPc_Ack=1"));
  return true;
}

void MainWindow::promptNgDecisionAndDispatch() {
  if (!plcRuntime_) return;
  QMessageBox box(QMessageBox::Question,
                  QStringLiteral("NG处理决策"),
                  QStringLiteral("本次判定为 NG。\n请选择后续处理方式："),
                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                  this);
  box.setDefaultButton(QMessageBox::Yes);
  if (auto *btn = box.button(QMessageBox::Yes)) btn->setText(QStringLiteral("当前件复测"));
  if (auto *btn = box.button(QMessageBox::No)) btn->setText(QStringLiteral("继续（不复测）"));
  if (auto *btn = box.button(QMessageBox::Cancel)) btn->setText(QStringLiteral("取消"));
  box.exec();
  const auto decide = box.standardButton(box.clickedButton());
  if (decide == QMessageBox::Cancel) {
    appendProductionLog(QStringLiteral("NG自动分支：用户取消决策，保持当前步骤等待人工处理"));
    return;
  }
  QVariantMap args;
  const qint16 plcMode = (hasLastStatus_
                       && lastStatus_.control_mode >= core::plc_v26::kModeManual
                       && lastStatus_.control_mode <= core::plc_v26::kModeSingleStep)
                          ? lastStatus_.control_mode
                          : static_cast<qint16>(core::plc_v26::kModeAuto);
  args.insert(QStringLiteral("plc_mode"), plcMode);
  args.insert(QStringLiteral("part_type_arg"), static_cast<int>(currentCategoryModeForAutoFlow()));
  if (decide == QMessageBox::Yes) {
    appendProductionLog(QStringLiteral("NG自动分支：用户选择当前件复测"));
    clearPendingMailboxArchive();
    if (!tryAutoWritePcAck()) {
      appendProductionLog(QStringLiteral("NG自动分支：iPc_Ack写入失败，未发送复测命令"));
      return;
    }
    handleUiCommandRequested(QStringLiteral("START_RETEST_CURRENT"), args);
    return;
  }
  appendProductionLog(QStringLiteral("NG自动分支：用户选择继续（不复测）"));
  if (!archivePendingMailbox()) {
    appendProductionLog(QStringLiteral("NG自动分支：RAW/DB归档失败，未发送继续命令"));
    return;
  }
  if (!tryAutoWritePcAck()) {
    appendProductionLog(QStringLiteral("NG自动分支：iPc_Ack写入失败，未发送继续命令"));
    return;
  }
  handleUiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), args);
}

void MainWindow::clearPendingMailboxArchive() {
  pendingMailboxArchive_ = PendingMailboxArchive{};
}

void MainWindow::openRawUuidForViewer(const QString &measurementUuid) {
  core::Db db;
  QString err;
  if (!db.open(appCfg_.db, &err)) {
    QMessageBox::warning(this, QStringLiteral("RAW查看"),
                         QStringLiteral("打开数据库失败：%1").arg(err));
    return;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::warning(this, QStringLiteral("RAW查看"),
                         QStringLiteral("检查数据库结构失败：%1").arg(err));
    return;
  }
  QString path;
  if (!db.getRawFilePathByMeasurementUuid(measurementUuid, &path, &err)) {
    QMessageBox::warning(this, QStringLiteral("RAW查看"), err);
    return;
  }
  openRawFileForViewer(path);
}

void MainWindow::openRawFileForViewer(const QString &path) {
  if (!rawViewerWidget_) return;
  const QString trimmedPath = path.trimmed();
  if (trimmedPath.isEmpty()) return;

  QFileInfo fi(trimmedPath);
  const QString absPath = fi.absoluteFilePath();
  rawViewerWidget_->setRawPath(absPath);
  rawViewerWidget_->setReplayResultText(QString());
  rawViewerWidget_->setMetaJson(QString());
  rawViewerWidget_->setComputeEnabled(false);
  rawViewerWidget_->setExportEnabled(false);
  rawViewerWidget_->clearRawPointRows();
  rawViewerSnapshot_.reset();
  rawViewerPath_.clear();

  if (!fi.exists() || !fi.isFile()) {
    const QString err = QStringLiteral("RAW 文件不存在：%1").arg(absPath);
    rawViewerWidget_->setSummaryText(err);
    QMessageBox::warning(this, QStringLiteral("RAW查看"), err);
    return;
  }

  core::MeasurementSnapshot raw;
  QString err;
  if (!core::readRawV2(absPath, &raw, &err)) {
    const QString text = QStringLiteral("读取 RAW 失败：%1").arg(err);
    rawViewerWidget_->setSummaryText(text);
    QMessageBox::warning(this, QStringLiteral("RAW查看"), text);
    return;
  }

  QJsonObject meta;
  QString prettyMeta = raw.meta_json;
  if (!raw.meta_json.trimmed().isEmpty()) {
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.meta_json.toUtf8(), &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
      meta = doc.object();
      prettyMeta = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
  }

  QStringList summary;
  summary << QStringLiteral("RAW 文件已加载");
  summary << QStringLiteral("路径：%1").arg(absPath);
  summary << QStringLiteral("UUID：%1").arg(raw.measurement_uuid);
  summary << QStringLiteral("类型：%1").arg(QString(raw.part_type.toUpper()));
  summary << QStringLiteral("测量时间：%1").arg(raw.measured_at_utc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
  summary << QStringLiteral("工件ID：%1").arg(meta.value(QStringLiteral("part_id")).toString(QStringLiteral("-")));
  summary << QStringLiteral("槽位：%1").arg(meta.contains(QStringLiteral("slot_index"))
                                      ? QString::number(meta.value(QStringLiteral("slot_index")).toInt() + 1)
                                      : QStringLiteral("-"));
  summary << QStringLiteral("原始item：%1").arg(meta.contains(QStringLiteral("item_index"))
                                         ? QString::number(meta.value(QStringLiteral("item_index")).toInt())
                                         : QStringLiteral("-"));
  summary << QStringLiteral("流程：%1").arg(meta.value(QStringLiteral("run_kind")).toString(QStringLiteral("-")));
  if (raw.part_type.toUpper() == QChar('A')) {
    summary << QStringLiteral("CONF4：rings=%1 points=%2 angle=%3 raw_points=%4")
                   .arg(raw.conf_spec.rings)
                   .arg(raw.conf_spec.points_per_ring)
                   .arg(raw.conf_spec.angle_step_deg, 0, 'f', 3)
                   .arg(raw.confocal4.size());
    summary << QStringLiteral("长度：L=%1")
                   .arg(formatNumber(raw.gt2r_mm3.value(0, qQNaN())));
  } else {
    summary << QStringLiteral("RUNO2：rings=%1 points=%2 angle=%3 raw_points=%4")
                   .arg(raw.run_spec.rings)
                   .arg(raw.run_spec.points_per_ring)
                   .arg(raw.run_spec.angle_step_deg, 0, 'f', 3)
                   .arg(raw.runout2.size());
    summary << QStringLiteral("长度：AD=%1 BC=%2")
                   .arg(formatNumber(raw.gt2r_mm3.value(0, qQNaN())))
                   .arg(formatNumber(raw.gt2r_mm3.value(1, qQNaN())));
  }

  rawViewerSnapshot_ = std::make_unique<core::MeasurementSnapshot>(raw);
  rawViewerPath_ = absPath;
  rawViewerWidget_->setSummaryText(summary.join(QStringLiteral("\n")));
  rawViewerWidget_->setMetaJson(prettyMeta);
  rawViewerWidget_->setRawPointRows(buildRawPointRows(raw, appCfg_.algo));
  rawViewerWidget_->setComputeEnabled(true);
  rawViewerWidget_->setExportEnabled(true);

  const int page = ui_->stackedWidget->indexOf(rawViewerWidget_);
  if (page >= 0) {
    ui_->navList->setCurrentRow(page);
    ui_->stackedWidget->setCurrentIndex(page);
  }
}

void MainWindow::computeLoadedRawForViewer() {
  if (!rawViewerWidget_) return;
  if (!rawViewerSnapshot_) {
    rawViewerWidget_->setReplayResultText(QStringLiteral("请先打开 RAW 文件。"));
    return;
  }
  QString text;
  QString err;
  if (!computeRawReplayForViewer(*rawViewerSnapshot_, &text, &err)) {
    rawViewerWidget_->setReplayResultText(QStringLiteral("RAW 回放计算失败：%1").arg(err));
    return;
  }
  rawViewerWidget_->setReplayResultText(text);
}

void MainWindow::exportLoadedRawCsvForViewer() {
  if (!rawViewerSnapshot_) {
    QMessageBox::information(this, QStringLiteral("RAW导出"),
                             QStringLiteral("请先打开 RAW 文件。"));
    return;
  }

  QFileInfo rawFi(rawViewerPath_);
  const QString defaultName = rawViewerSnapshot_->measurement_uuid.trimmed().isEmpty()
                                  ? QStringLiteral("raw_points.csv")
                                  : rawViewerSnapshot_->measurement_uuid + QStringLiteral(".csv");
  const QString defaultPath = rawFi.exists()
                                  ? rawFi.absoluteDir().filePath(defaultName)
                                  : QDir(appCfg_.paths.raw_dir).filePath(defaultName);
  const QString path = QFileDialog::getSaveFileName(
      this,
      QStringLiteral("导出 RAW CSV"),
      defaultPath,
      QStringLiteral("CSV (*.csv);;所有文件 (*)"));
  if (path.trimmed().isEmpty()) return;

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, QStringLiteral("RAW导出"),
                         QStringLiteral("写入 CSV 失败：%1").arg(f.errorString()));
    return;
  }
  QByteArray bytes;
  bytes.append("\xEF\xBB\xBF"); // 便于 Excel 正确识别 UTF-8 中文表头。
  bytes.append(buildRawCsvText(*rawViewerSnapshot_, appCfg_.algo).toUtf8());
  if (f.write(bytes) != bytes.size()) {
    QMessageBox::warning(this, QStringLiteral("RAW导出"),
                         QStringLiteral("写入 CSV 不完整：%1").arg(f.errorString()));
    return;
  }
  f.close();
  QMessageBox::information(this, QStringLiteral("RAW导出"),
                           QStringLiteral("CSV已导出：%1").arg(path));
}

bool MainWindow::computeRawReplayForViewer(const core::MeasurementSnapshot &raw,
                                           QString *resultText,
                                           QString *err) const {
  if (!resultText) {
    if (err) *err = QStringLiteral("resultText 不能为空");
    return false;
  }

  core::PlcMailboxSnapshot snapshot;
  if (!core::buildMailboxSnapshotFromRawV2(raw, &snapshot, err)) {
    return false;
  }

  QJsonObject meta;
  if (!raw.meta_json.trimmed().isEmpty()) {
    const QJsonDocument doc = QJsonDocument::fromJson(raw.meta_json.toUtf8());
    if (doc.isObject()) meta = doc.object();
  }
  const bool calibrationContext =
      meta.value(QStringLiteral("run_kind")).toString().trimmed().toUpper() == QStringLiteral("CALIBRATION") ||
      rawViewerPath_.contains(QStringLiteral("/calibration/"), Qt::CaseInsensitive);

  core::MeasurementComputeServiceResult computed;
  if (!core::computeMailboxSnapshot(snapshot, appCfg_.algo, calibrationContext, &computed, err)) {
    return false;
  }

  QStringList lines;
  lines << QStringLiteral("RAW 回放计算（不写PLC、不落库）");
  lines << QStringLiteral("UUID：%1").arg(raw.measurement_uuid);
  lines << computed.logs;
  lines << QStringLiteral("计算结束");

  if (!computed.items.isEmpty()) {
    const core::ProductionSlotSummary &summary = computed.items.first().summary;
    lines << QStringLiteral("--- 回放结果摘要 ---");
    lines << QStringLiteral("结果：%1").arg(summary.judgement_ok ? QStringLiteral("OK") : QStringLiteral("NG"));
    lines << QStringLiteral("原因：%1").arg(summary.fail_reason_text.isEmpty() ? QStringLiteral("-") : summary.fail_reason_text);
    if (summary.part_type.toUpper() == QChar('A')) {
      lines << QStringLiteral("A总长(mm)：%1").arg(core::measurementFormatNumber(summary.compute.values.total_len_mm));
      lines << QStringLiteral("左端 内/外径(mm)：%1 / %2")
                   .arg(core::measurementFormatNumber(summary.compute.values.id_left_mm))
                   .arg(core::measurementFormatNumber(summary.compute.values.od_left_mm));
      lines << QStringLiteral("右端 内/外径(mm)：%1 / %2")
                   .arg(core::measurementFormatNumber(summary.compute.values.id_right_mm))
                   .arg(core::measurementFormatNumber(summary.compute.values.od_right_mm));
    } else {
      lines << QStringLiteral("AD长度(mm)：%1").arg(core::measurementFormatNumber(summary.compute.values.ad_len_mm));
      lines << QStringLiteral("BC长度(mm)：%1").arg(core::measurementFormatNumber(summary.compute.values.bc_len_mm));
      lines << QStringLiteral("左/右跳动(mm)：%1 / %2")
                   .arg(core::measurementFormatNumber(summary.compute.values.runout_left_mm))
                   .arg(core::measurementFormatNumber(summary.compute.values.runout_right_mm));
    }
  }

  *resultText = lines.join(QStringLiteral("\n"));
  return true;
}

bool MainWindow::archivePendingMailbox() {
  if (!pendingMailboxArchive_.valid || pendingMailboxArchive_.items.isEmpty()) {
    return true;
  }

  auto appendArchiveLog = [this](bool calibrationContext, const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };

  const bool calibrationContext = pendingMailboxArchive_.calibration_context;
  core::Db db;
  QString err;
  if (!db.open(appCfg_.db, &err)) {
    handlePlcRuntimeError(QStringLiteral("打开数据库失败，无法归档测量数据：%1").arg(err));
    return false;
  }
  if (!db.ensureSchema(&err)) {
    handlePlcRuntimeError(QStringLiteral("更新数据库结构失败，无法归档测量数据：%1").arg(err));
    return false;
  }

  core::MeasurementComputeInput input;
  input.snapshot = pendingMailboxArchive_.snapshot;
  input.context.run_kind = calibrationContext ? core::BusinessRunKind::Calibration
                                              : core::BusinessRunKind::Production;
  input.context.measure_mode = calibrationContext
                                   ? core::BusinessMeasureMode::Unknown
                                   : core::businessMeasureModeFromString(
                                         productionWidget_ ? productionWidget_->selectedMeasureModeText()
                                                           : QStringLiteral("NORMAL"));
  input.context.attempt_kind = core::BusinessAttemptKind::Primary;
  input.context.source_mode = QStringLiteral("AUTO");
  input.context.measured_at_utc = QDateTime::currentDateTime();
  if (calibrationContext && calibrationWidget_) {
    input.context.calibration_type = QString(input.snapshot.part_type.toUpper());
    input.context.calibration_master_part_id =
        calibrationWidget_->masterPartIdForType(input.snapshot.part_type);
  }
  const QString rawKindDir = calibrationContext ? QStringLiteral("calibration")
                                                : QStringLiteral("production");
  const QString rawDateDir =
      input.context.measured_at_utc.date().toString(QStringLiteral("yyyy-MM-dd"));
  const QString rawOutputDir = QDir(appCfg_.paths.raw_dir).filePath(
      QDir(rawKindDir).filePath(rawDateDir));

  core::MeasurementIngestRequest req;
  req.cycle.cycle_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  req.cycle.part_type = QString(input.snapshot.part_type.toUpper());
  req.cycle.item_count = pendingMailboxArchive_.items.size();
  req.cycle.source_mode = input.context.source_mode;
  req.cycle.measured_at_utc = input.context.measured_at_utc;
  req.cycle.mailbox_header_json =
      QString::fromUtf8(QJsonDocument(core::toJson(input.snapshot)).toJson(QJsonDocument::Compact));

  QJsonObject meta;
  meta.insert(QStringLiteral("schema"), QStringLiteral("v2.6"));
  meta.insert(QStringLiteral("time_basis"), QStringLiteral("LOCAL"));
  meta.insert(QStringLiteral("run_kind"), core::toString(input.context.run_kind));
  meta.insert(QStringLiteral("measure_mode"), core::toString(input.context.measure_mode));
  if (hasLastStatus_) {
    meta.insert(QStringLiteral("plc_step_state"), static_cast<int>(lastStatus_.step_state));
    meta.insert(QStringLiteral("plc_machine_state"), static_cast<int>(lastStatus_.machine_state));
  }
  req.cycle.mailbox_meta_json =
      QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));

  for (const PendingArchiveItem &pendingItem : pendingMailboxArchive_.items) {
    const QString measurementUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    core::RawLoopItemBuildResult built;
    if (!core::buildRawLoopItem(input, pendingItem.item_index,
                                pendingItem.summary.compute, measurementUuid,
                                &built, &err)) {
      handlePlcRuntimeError(QStringLiteral("构建RAW归档数据失败：%1").arg(err));
      return false;
    }
    built.ingest_item.measurement_uuid = measurementUuid;

    core::RawWriteInfoV2 rawInfo;
    if (!core::writeRawV2(rawOutputDir, built.raw_snapshot, &rawInfo, &err)) {
      handlePlcRuntimeError(QStringLiteral("写RAW文件失败：%1").arg(err));
      return false;
    }

    core::IngestRawInput raw;
    raw.enabled = true;
    raw.file_path = rawInfo.final_path;
    raw.file_size_bytes = rawInfo.file_size_bytes;
    raw.format_version = rawInfo.format_version;
    raw.file_crc32 = rawInfo.file_crc32;
    raw.chunk_mask = rawInfo.chunk_mask;
    raw.scan_kind = rawInfo.scan_kind;
    raw.main_channels = rawInfo.main_channels;
    raw.rings = rawInfo.rings;
    raw.points_per_ring = rawInfo.points_per_ring;
    raw.angle_step_deg = rawInfo.angle_step_deg;
    raw.meta_json = rawInfo.meta_json;
    raw.raw_kind = QStringLiteral("MAILBOX_V26");

    req.items.push_back(built.ingest_item);
    req.results.push_back(built.ingest_result);
    req.raws.push_back(raw);
  }

  core::MeasurementIngestResponse resp;
  core::MeasurementIngestService svc(db);
  if (!svc.ingest(req, &resp, &err)) {
    handlePlcRuntimeError(QStringLiteral("RAW/DB归档失败：%1").arg(err));
    return false;
  }

  appendArchiveLog(calibrationContext,
                   QStringLiteral("RAW/DB归档完成：cycle_id=%1 item_count=%2")
                       .arg(resp.plc_cycle_id)
                       .arg(resp.items.size()));
  clearPendingMailboxArchive();
  return true;
}

void MainWindow::processAutoScanIdCheck() {
  if (!plcRuntime_ || !hasLastStatus_) return;
  if (lastStatus_.scan_done == 0) return;

  if (!hasLastTray_) {
    core::PlcTrayPartIdBlockV2 tray;
    QString err;
    if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("自动ID核对前读取扫码ID失败") : err);
      return;
    }
  }

  if (idCheckStrategy_ == IdCheckStrategy::Bypass) {
    appendProductionLog(QStringLiteral("ID核对自动流：策略=BYPASS，自动放行"));
    tryAutoContinueAfterIdCheck();
    return;
  }

  if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
    QString err;
    QVector<QString> mockExpected;
    if (!loadMockExpectedPartIds(&mockExpected, &err)) {
      handlePlcRuntimeError(err);
      appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，读取本地期望ID失败，阻塞继续"));
      return;
    }
    mesExpectedPartIds_ = mockExpected;
    appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，文件=%1")
                            .arg(resolveIdCheckMockFilePath()));
  }

  bool hasExpectedIds = false;
  if (mesExpectedPartIds_.size() == core::kLogicalSlotCount) {
    for (const QString &id : mesExpectedPartIds_) {
      if (!id.trimmed().isEmpty()) {
        hasExpectedIds = true;
        break;
      }
    }
  }
  if (!hasExpectedIds) {
    if (idCheckStrategy_ == IdCheckStrategy::MesStrict) {
      appendProductionLog(QStringLiteral("ID核对自动流：策略=MES_STRICT，但未收到MES期望ID，阻塞继续"));
    } else {
      appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，但文件未提供任何期望ID，阻塞继续"));
    }
    return;
  }

  QStringList mismatchDetails;
  QVector<int> mismatchSlots;
  const bool pass = evaluateIdCheckAgainstMes(&mismatchDetails, &mismatchSlots);
  if (pass) {
    appendProductionLog(QStringLiteral("ID核对自动流：MES比对通过，允许继续"));
    tryAutoContinueAfterIdCheck();
    return;
  }

  for (int slot : mismatchSlots) {
    if (productionWidget_) {
      productionWidget_->markSlotScanMismatch(slot, QStringLiteral("MES工件编号不一致"));
    }
  }
  appendProductionLog(QStringLiteral("ID核对自动流：MES比对失败，阻塞自动继续"));
  for (const QString &line : mismatchDetails) {
    appendProductionLog(QStringLiteral("  %1").arg(line));
  }
}

void MainWindow::processAutoMailboxFlow() {
  if (!plcRuntime_ || !hasLastMailboxSnapshot_ || !lastMailboxSnapshot_) return;
  const bool calibrationContext = hasLastStatus_ && isCalibrationStepCode(lastStatus_.step_state, calibrationFlowExpected_);
  if (calibrationContext) {
    if (!handleComputeResultRequested(lastMailboxSnapshot_->part_type, true)) {
      appendCalibrationLog(QStringLiteral("标定自动流：测量包计算失败，保持等待人工处理"));
      return;
    }
    if (!lastComputeHasItems_) {
      appendCalibrationLog(QStringLiteral("标定自动流：本次测量包无有效工件"));
      return;
    }
    appendCalibrationLog(QStringLiteral("标定自动流：计算完成，结果已更新到标定页"));
    return;
  }

  if (!handleComputeResultRequested(lastMailboxSnapshot_->part_type)) {
    appendProductionLog(QStringLiteral("自动流：测量包计算失败，保持等待人工处理"));
    return;
  }
  if (!lastComputeHasItems_) {
    appendProductionLog(QStringLiteral("自动流：本次测量包无有效工件，跳过ACK/继续分支"));
    return;
  }

  if (lastComputeOverallOk_) {
    if (!archivePendingMailbox()) {
      appendProductionLog(QStringLiteral("自动流：RAW/DB归档失败，终止后续分支"));
      return;
    }
    if (!tryAutoWritePcAck()) {
      appendProductionLog(QStringLiteral("自动流：iPc_Ack写入失败，终止后续分支"));
      return;
    }
    appendProductionLog(QStringLiteral("自动流：判定OK，自动发送继续（不复测）"));
    QVariantMap args;
    const qint16 plcMode = (hasLastStatus_
                         && lastStatus_.control_mode >= core::plc_v26::kModeManual
                         && lastStatus_.control_mode <= core::plc_v26::kModeSingleStep)
                            ? lastStatus_.control_mode
                            : static_cast<qint16>(core::plc_v26::kModeAuto);
    args.insert(QStringLiteral("plc_mode"), plcMode);
    args.insert(QStringLiteral("part_type_arg"), static_cast<int>(currentCategoryModeForAutoFlow()));
    handleUiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), args);
    return;
  }
  appendProductionLog(QStringLiteral("自动流：判定NG，弹窗等待复测/继续决策"));
  promptNgDecisionAndDispatch();
}

void MainWindow::handleUiCommandRequested(const QString &cmd, const QVariantMap &args) {
  if (!plcRuntime_) {
    return;
  }
  const QString uiSource = args.value(QStringLiteral("ui_source")).toString().trimmed().toLower();
  const bool fromCalibrationUi = (uiSource == QStringLiteral("calibration"));
  const bool statusInCalibrationFlow = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  const bool commandInCalibrationFlow = fromCalibrationUi || statusInCalibrationFlow || cmd == QStringLiteral("START_CALIBRATION");
  auto appendCmdLog = [this, commandInCalibrationFlow](const QString &line) {
    if (commandInCalibrationFlow) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  if (cmd == QStringLiteral("COMPUTE_RESULT")) {
    QChar preferredPartType = QChar('A');
    const QString partTypeText = args.value(QStringLiteral("part_type")).toString().trimmed().toUpper();
    if (!partTypeText.isEmpty() && (partTypeText.at(0) == QChar('A') || partTypeText.at(0) == QChar('B'))) {
      preferredPartType = partTypeText.at(0);
    }
    handleComputeResultRequested(preferredPartType, fromCalibrationUi);
    return;
  }
  if (cmd == QStringLiteral("CONTINUE_AFTER_ID_CHECK")) {
    if (!tryAutoContinueAfterIdCheck()) return;
    return;
  }

  qint16 plcMode = static_cast<qint16>(mapArg(args, QStringLiteral("plc_mode"), static_cast<quint32>(core::plc_v26::kModeManual)));
  if (cmd == QStringLiteral("SET_MODE_MANUAL")) plcMode = core::plc_v26::kModeManual;
  if (cmd == QStringLiteral("SET_MODE_AUTO")) plcMode = core::plc_v26::kModeAuto;
  if (cmd == QStringLiteral("START_AUTO") || cmd == QStringLiteral("START_CALIBRATION")) plcMode = core::plc_v26::kModeAuto;
  qint16 categoryMode = static_cast<qint16>(mapArg(args, QStringLiteral("part_type_arg"), core::plc_v26::kPartTypeA));
  if (categoryMode == core::plc_v26::kPartTypeA || categoryMode == core::plc_v26::kPartTypeB) {
    lastCategoryMode_ = categoryMode;
  }

  QString err;
  if (!plcRuntime_->writePlcMode(plcMode, &err)) { handlePlcRuntimeError(err); return; }
  if (cmd == QStringLiteral("SET_MODE_MANUAL") || cmd == QStringLiteral("SET_MODE_AUTO")) {
    if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(plcMode)));
    plcRuntime_->pollOnce();
    return;
  }

  const bool needRewriteModeAndCategory =
      (cmd == QStringLiteral("INITIALIZE") ||
       cmd == QStringLiteral("START_AUTO") ||
       cmd == QStringLiteral("START_CALIBRATION"));
  if (needRewriteModeAndCategory) {
    if (!plcRuntime_->writePlcMode(plcMode, &err)) { handlePlcRuntimeError(err); return; }
    if (!plcRuntime_->setCategoryMode(categoryMode, &err)) { handlePlcRuntimeError(err); return; }
  }
  if (cmd == QStringLiteral("CONTINUE_NO_RETEST")) {
    if (!archivePendingMailbox()) {
      appendCmdLog(QStringLiteral("继续（不复测）：RAW/DB归档失败，未发送PLC命令"));
      return;
    }
  } else if (cmd == QStringLiteral("START_RETEST_CURRENT")) {
    clearPendingMailboxArchive();
  }
  if (cmd == QStringLiteral("START_RETEST_CURRENT")
      || cmd == QStringLiteral("CONTINUE_NO_RETEST")) {
    if (!plcRuntime_->writeJudgeResult(core::plc_v26::kJudgeUnknown, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("清零 iJudge_Result 失败") : err);
      return;
    }
    appendCmdLog(QStringLiteral("已清零 iJudge_Result=0"));
  }

  quint16 cmdBits = 0;
  bool ok = false;
  if (cmd == QStringLiteral("INITIALIZE")) {
    cmdBits = core::plc_v26::kCmdInitializeBit;
    ok = plcRuntime_->sendInitialize(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_AUTO")) {
    cmdBits = core::plc_v26::kCmdStartMeasureBit;
    ok = plcRuntime_->sendStartMeasure(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_CALIBRATION")) {
    cmdBits = core::plc_v26::kCmdStartCalibrationBit;
    ok = plcRuntime_->sendStartCalibration(categoryMode, &err);
  } else if (cmd == QStringLiteral("STOP")) {
    cmdBits = core::plc_v26::kCmdStopBit;
    ok = plcRuntime_->sendStop(categoryMode, &err);
  } else if (cmd == QStringLiteral("RESET_ALARM") || cmd == QStringLiteral("HOME_ALL")) {
    cmdBits = core::plc_v26::kCmdResetBit;
    ok = plcRuntime_->sendReset(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_RETEST_CURRENT")) {
    cmdBits = core::plc_v26::kCmdRetestCurrentBit;
    ok = plcRuntime_->sendRetestCurrent(categoryMode, &err);
  } else if (cmd == QStringLiteral("CONTINUE_NO_RETEST")) {
    cmdBits = core::plc_v26::kCmdContinueWithoutRetestBit;
    ok = plcRuntime_->sendContinueWithoutRetest(categoryMode, &err);
  } else if (cmd == QStringLiteral("ALARM_MUTE")) {
    cmdBits = core::plc_v26::kCmdAlarmMuteBit;
    ok = plcRuntime_->sendAlarmMute(categoryMode, &err);
  } else {
    handlePlcRuntimeError(QStringLiteral("暂未映射的 PLC 命令：%1").arg(cmd));
    return;
  }
  if (!ok) { handlePlcRuntimeError(err); return; }
  if (commandInCalibrationFlow
      && (cmd == QStringLiteral("START_RETEST_CURRENT")
          || cmd == QStringLiteral("CONTINUE_NO_RETEST"))) {
    err.clear();
    if (!plcRuntime_->sendPcAck(1, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 iPc_Ack=1 失败") : err);
      return;
    }
    appendCmdLog(QStringLiteral("已写 iPc_Ack=1"));
  }
  if (cmd == QStringLiteral("START_RETEST_CURRENT") && productionWidget_ && !commandInCalibrationFlow) {
    productionWidget_->clearActiveSlotsComputedResults();
    appendCmdLog(QStringLiteral("复测：已清理活跃槽位旧结果显示，等待新结果覆盖"));
  }
  if (cmd == QStringLiteral("START_CALIBRATION")) {
    calibrationFlowExpected_ = true;
  } else if (cmd == QStringLiteral("START_AUTO") ||
             cmd == QStringLiteral("INITIALIZE")) {
    calibrationFlowExpected_ = false;
  }
  awaitingCmdReply_ = true;
  pendingCmdBits_ = cmdBits;

  appendCmdLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                   .arg(cmd)
                   .arg(plcMode)
                   .arg(categoryMode)
                   .arg(QString::number(cmdBits, 16).toUpper()));
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                          .arg(cmd)
                          .arg(plcMode)
                          .arg(categoryMode)
                          .arg(QString::number(cmdBits, 16).toUpper()));
}

void MainWindow::handleWriteTrayPartIdRequested(int slotIndex, const QString &partId) {
  if (!plcRuntime_) {
    return;
  }
  if (slotIndex < 0 || slotIndex >= core::kLogicalSlotCount) {
    handlePlcRuntimeError(QStringLiteral("槽位号越界：%1").arg(slotIndex));
    return;
  }
  const QString id = partId.trimmed();
  if (id.isEmpty()) {
    handlePlcRuntimeError(QStringLiteral("工件ID不能为空"));
    return;
  }

  QString err;
  if (!plcRuntime_->writeTrayPartIdSlot(slotIndex, id, &err)) {
    handlePlcRuntimeError(err.isEmpty()
                              ? QStringLiteral("写入槽位 %1 工件ID失败").arg(slotIndex + 1)
                              : err);
    return;
  }

  appendProductionLog(QStringLiteral("已写入槽位%1工件ID：%2").arg(slotIndex + 1).arg(id));
  plcRuntime_->pollOnce();
}

void MainWindow::handleReadMailboxRequested(QChar preferredPartType, bool preferCalibrationContext) {
  if (!plcRuntime_) return;
  const bool calibrationContext = preferCalibrationContext
                               || isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  auto appendMailboxLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  core::PlcMailboxSnapshot snapshot;
  QString err;
  if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
  *lastMailboxSnapshot_ = snapshot;
  hasLastMailboxSnapshot_ = true;

  appendMailboxLog(QStringLiteral("读取测量包成功：part=%1 item_count=%2")
                       .arg(QString(snapshot.part_type))
                       .arg(snapshot.item_count));
  if (calibrationContext && calibrationWidget_) {
    for (const auto &item : snapshot.items) {
      if (item.slot_index != core::kCalibrationSlotIndex) continue;
      core::CalibrationSlotSummary s;
      s.slot_index = item.slot_index;
      s.calibration_type = QString(snapshot.part_type.toUpper());
      s.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
      s.measured_part_id = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();
      s.valid = false;
      calibrationWidget_->setSlotSummary(s);
      break;
    }
  }

  for (const auto &item : snapshot.items) {
    const QString slotText = item.slot_index >= 0 ? QString::number(item.slot_index + 1) : QStringLiteral("-");
    const QString idText = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();
    if (snapshot.part_type == QChar('A')) {
      appendMailboxLog(QStringLiteral("  item%1 slot=%2 id=%3 总长=%4 原始点数=%5")
                           .arg(item.item_index)
                           .arg(slotText)
                           .arg(idText)
                           .arg(item.total_len_mm, 0, 'f', 6)
                           .arg(item.raw_points_um.size()));
    } else {
      appendMailboxLog(QStringLiteral("  item%1 slot=%2 id=%3 AD=%4 BC=%5 原始点数=%6")
                           .arg(item.item_index)
                           .arg(slotText)
                           .arg(idText)
                           .arg(item.ad_len_mm, 0, 'f', 6)
                           .arg(item.bc_len_mm, 0, 'f', 6)
                           .arg(item.raw_points_um.size()));
    }
    if (!item.raw_points_um.isEmpty()) {
      QStringList vals;
      vals.reserve(item.raw_points_um.size());
      for (float v : item.raw_points_um) vals << QString::number(v, 'f', 6);
      appendMailboxLog(QStringLiteral("    raw=[%1]").arg(vals.join(QStringLiteral(","))));
    }
  }
}

void MainWindow::handleSaveTestRawRequested() {
  if (!plcRuntime_) {
    appendProductionLog(QStringLiteral("保存测试RAW失败：PLC未初始化"));
    return;
  }

  QString partTypeText = productionWidget_ ? productionWidget_->selectedPartTypeText().trimmed().toUpper()
                                           : QStringLiteral("A");
  if (partTypeText != QStringLiteral("A") && partTypeText != QStringLiteral("B")) {
    partTypeText = QStringLiteral("A");
  }
  const QChar partType = partTypeText.at(0);

  core::PlcMailboxSnapshot snapshot;
  QString err;
  appendProductionLog(QStringLiteral("保存测试RAW：正在读取当前PLC测量包，类型=%1").arg(partTypeText));
  if (!plcRuntime_->readSecondStageMailboxSnapshot(partType, &snapshot, &err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("保存测试RAW失败：读取测量包失败") : err);
    return;
  }

  core::PlcMailboxSnapshot saveSnapshot = snapshot;
  QVector<int> presentItemIndexes;
  for (int i = 0; i < saveSnapshot.items.size(); ++i) {
    const auto &item = saveSnapshot.items.at(i);
    if (item.present) {
      presentItemIndexes.push_back(i);
    }
  }
  if (presentItemIndexes.isEmpty()) {
    appendProductionLog(QStringLiteral("保存测试RAW取消：当前测量包没有有效工件"));
    return;
  }

  const QDateTime measuredAt = QDateTime::currentDateTime();
  const QString stamp = measuredAt.toString(QStringLiteral("yyyyMMddHHmmss"));
  for (int itemVectorIndex : presentItemIndexes) {
    auto &item = saveSnapshot.items[itemVectorIndex];
    const int slotNo = item.slot_index >= 0 ? item.slot_index + 1 : item.item_index + 1;
    const QString currentId = item.part_id.trimmed();
    const QString defaultId = (currentId.isEmpty() || currentId.toUpper() == QStringLiteral("NG"))
                                  ? QStringLiteral("TEST-%1-S%2")
                                        .arg(stamp)
                                        .arg(slotNo, 2, 10, QLatin1Char('0'))
                                  : currentId;

    bool ok = false;
    QString inputId = QInputDialog::getText(
        this,
        QStringLiteral("保存测试RAW"),
        QStringLiteral("槽位%1 / item%2 工件ID：").arg(slotNo).arg(item.item_index),
        QLineEdit::Normal,
        defaultId,
        &ok);
    if (!ok) {
      appendProductionLog(QStringLiteral("保存测试RAW已取消"));
      return;
    }
    inputId = inputId.trimmed();
    item.part_id = inputId.isEmpty() ? defaultId : inputId;
  }

  core::MeasurementComputeInput input;
  input.snapshot = saveSnapshot;
  input.context.run_kind = core::BusinessRunKind::ManualTest;
  input.context.measure_mode =
      productionWidget_ ? core::businessMeasureModeFromString(productionWidget_->selectedMeasureModeText())
                        : core::BusinessMeasureMode::Unknown;
  input.context.attempt_kind = core::BusinessAttemptKind::Primary;
  input.context.source_mode = QStringLiteral("MANUAL_TEST");
  input.context.measured_at_utc = measuredAt;

  const QString rawDateDir = measuredAt.date().toString(QStringLiteral("yyyy-MM-dd"));
  const QString rawOutputDir = QDir(appCfg_.paths.raw_dir).filePath(
      QDir(QStringLiteral("manual_test")).filePath(rawDateDir));

  QStringList savedPaths;
  for (int itemVectorIndex : presentItemIndexes) {
    const auto &item = saveSnapshot.items.at(itemVectorIndex);
    const QString measurementUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const core::MeasurementComputeResult placeholder =
        core::makePlaceholderComputeResult(input, item.item_index);

    core::RawLoopItemBuildResult built;
    if (!core::buildRawLoopItem(input, item.item_index, placeholder,
                                measurementUuid, &built, &err)) {
      handlePlcRuntimeError(QStringLiteral("保存测试RAW失败：构建RAW失败：%1").arg(err));
      return;
    }

    QJsonObject meta;
    const QJsonDocument metaDoc = QJsonDocument::fromJson(built.raw_snapshot.meta_json.toUtf8());
    if (metaDoc.isObject()) {
      meta = metaDoc.object();
    }
    meta.insert(QStringLiteral("manual_test"), true);
    meta.insert(QStringLiteral("source"), QStringLiteral("production_manual_save"));
    meta.insert(QStringLiteral("saved_from"), QStringLiteral("production_page"));
    meta.insert(QStringLiteral("operator_part_id"), item.part_id.trimmed());
    if (hasLastStatus_) {
      meta.insert(QStringLiteral("plc_step_state"), static_cast<int>(lastStatus_.step_state));
      meta.insert(QStringLiteral("plc_machine_state"), static_cast<int>(lastStatus_.machine_state));
    }
    built.raw_snapshot.meta_json =
        QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));

    core::RawWriteInfoV2 rawInfo;
    if (!core::writeRawV2(rawOutputDir, built.raw_snapshot, &rawInfo, &err)) {
      handlePlcRuntimeError(QStringLiteral("保存测试RAW失败：写RAW文件失败：%1").arg(err));
      return;
    }
    savedPaths.push_back(rawInfo.final_path);
  }

  appendProductionLog(QStringLiteral("保存测试RAW完成：%1个文件，目录=%2")
                          .arg(savedPaths.size())
                          .arg(rawOutputDir));
  for (const QString &path : savedPaths) {
    appendProductionLog(QStringLiteral("  %1").arg(path));
  }
}

bool MainWindow::handleComputeResultRequested(QChar preferredPartType,
                                              bool preferCalibrationContext,
                                              bool forceReloadMailbox) {
  if (!plcRuntime_) return false;
  lastComputeHasItems_ = false;
  lastComputeOverallOk_ = false;
  lastComputePartType_ = preferredPartType.toUpper();
  clearPendingMailboxArchive();

  core::PlcMailboxSnapshot snapshot;
  if (!forceReloadMailbox && hasLastMailboxSnapshot_ && lastMailboxSnapshot_) {
    snapshot = *lastMailboxSnapshot_;
  } else {
    QString err;
    if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("读取测量包失败，无法计算结果") : err);
      return false;
    }
    if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
    *lastMailboxSnapshot_ = snapshot;
    hasLastMailboxSnapshot_ = true;
  }

  const bool calibrationContext =
      preferCalibrationContext ||
      isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  auto appendComputeLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };

  core::MeasurementComputeServiceResult computed;
  QString computeErr;
  if (!core::computeMailboxSnapshot(snapshot, appCfg_.algo, calibrationContext, &computed, &computeErr)) {
    handlePlcRuntimeError(QStringLiteral("测量包计算失败：%1").arg(computeErr));
    return false;
  }
  for (const QString &line : computed.logs) {
    appendComputeLog(line);
  }

  PendingMailboxArchive pendingArchive;
  pendingArchive.calibration_context = calibrationContext;
  pendingArchive.snapshot = snapshot;

  for (const core::MeasurementComputedItem &item : computed.items) {
    const core::ProductionSlotSummary &slotSummary = item.summary;
    if (!calibrationContext && productionWidget_ &&
        slotSummary.slot_index >= 0 && slotSummary.slot_index < core::kLogicalSlotCount) {
      productionWidget_->setSlotSummary(slotSummary.slot_index, slotSummary);
    } else if (calibrationContext && calibrationWidget_) {
      core::CalibrationSlotSummary calSummary;
      calSummary.slot_index = slotSummary.slot_index;
      calSummary.calibration_type = QString(snapshot.part_type.toUpper());
      calSummary.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
      calSummary.measured_part_id = slotSummary.part_id;
      calSummary.valid = slotSummary.valid;
      calSummary.judgement_known = slotSummary.judgement_known;
      calSummary.judgement_ok = slotSummary.judgement_ok;
      calSummary.fail_reason_text = slotSummary.fail_reason_text;
      calSummary.compute = slotSummary.compute;
      calibrationWidget_->setSlotSummary(calSummary);
    }

    PendingArchiveItem pendingItem;
    pendingItem.item_index = item.item_index;
    pendingItem.summary = slotSummary;
    pendingArchive.items.push_back(pendingItem);
  }

  lastComputeHasItems_ = computed.has_items;
  lastComputeOverallOk_ = computed.has_items ? computed.overall_ok : false;
  lastComputePartType_ = computed.part_type.toUpper();
  pendingArchive.valid = !pendingArchive.items.isEmpty();
  pendingMailboxArchive_ = pendingArchive;

  if (computed.expected_item_count > 0) {
    const quint16 judgeResult = computed.overall_ok ? core::plc_v26::kJudgeOk : core::plc_v26::kJudgeNg;
    QString err;
    if (!plcRuntime_->writeJudgeResult(judgeResult, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 iJudge_Result 失败") : err);
    } else {
      appendComputeLog(QStringLiteral("已写 iJudge_Result=%1 (%2)")
                           .arg(judgeResult)
                           .arg(computed.overall_ok ? QStringLiteral("OK") : QStringLiteral("NG")));
    }
  } else {
    appendComputeLog(QStringLiteral("本次测量包无有效 item，跳过写 iJudge_Result"));
  }

  appendComputeLog(QStringLiteral("计算结束"));
  return true;
}


void MainWindow::handleAckMailboxRequested(bool preferCalibrationContext) {
  if (!plcRuntime_) return;
  const bool calibrationContext = preferCalibrationContext
                               || isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  if (!archivePendingMailbox()) {
    if (calibrationContext) appendCalibrationLog(QStringLiteral("手动ACK前RAW/DB归档失败，未写入pc_ack"));
    else appendProductionLog(QStringLiteral("手动ACK前RAW/DB归档失败，未写入pc_ack"));
    return;
  }
  QString err;
  if (!plcRuntime_->sendPcAck(1, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (calibrationContext) appendCalibrationLog(QStringLiteral("手动写入 pc_ack=1"));
  else appendProductionLog(QStringLiteral("手动写入 pc_ack=1"));
}
