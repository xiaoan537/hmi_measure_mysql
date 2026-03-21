#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "core/measurement_pipeline.hpp"
#include "core/plc_transport_v2.hpp"

namespace core {

struct PlcFakeWriteRecordV2 {
  quint32 start_address = 0;
  QVector<quint16> values;
};

bool encodePlcStatusBlockRegsV2(const PlcStatusBlockV2 &status,
                                QVector<quint16> *out,
                                QString *err = nullptr);

bool encodePlcCommandBlockFullRegsV2(const PlcCommandBlockV2 &command,
                                     QVector<quint16> *out,
                                     QString *err = nullptr);

bool encodePlcMailboxHeaderRegsV2(const PlcMailboxHeaderV2 &header,
                                  QVector<quint16> *out,
                                  QString *err = nullptr);

bool encodePlcMailboxArrayRegsV2(const QVector<float> &arrays_um,
                                 QVector<quint16> *out,
                                 QString *err = nullptr);

bool buildPlcMailboxRegisterBlockFromFrameV2(const PlcMailboxRawFrame &frame,
                                             PlcMailboxRegisterBlock *out,
                                             QString *err = nullptr);

bool flattenPlcMailboxRegisterBlockV2(const PlcMailboxRegisterBlock &block,
                                      QVector<quint16> *out,
                                      QString *err = nullptr);

class FakePlcRegisterClientV2 : public QObject, public IPlcRegisterClientV2 {
  Q_OBJECT
public:
  explicit FakePlcRegisterClientV2(QObject *parent = nullptr);
  ~FakePlcRegisterClientV2() override;

  void setConnected(bool connected);
  bool isConnected() const { return connected_; }

  void clearAll();
  void clearWriteHistory();

  quint16 registerValue(quint32 address, quint16 defaultValue = 0) const;
  QVector<quint16> registerRange(quint32 start_address, quint16 reg_count) const;

  void setRegister(quint32 address, quint16 value);
  void setRegisters(quint32 start_address, const QVector<quint16> &values);

  const QVector<PlcFakeWriteRecordV2> &writeHistory() const { return write_history_; }

  bool loadStatusBlock(const PlcAddressLayoutV2 &layout,
                       const PlcStatusBlockV2 &status,
                       QString *err = nullptr);

  bool loadTrayPartIdBlock(const PlcAddressLayoutV2 &layout,
                           const PlcTrayPartIdBlockV2 &tray,
                           QString *err = nullptr);

  bool loadCommandBlock(const PlcAddressLayoutV2 &layout,
                        const PlcCommandBlockV2 &command,
                        QString *err = nullptr);

  bool loadMailboxRegisterBlock(const PlcAddressLayoutV2 &layout,
                                const PlcMailboxRegisterBlock &block,
                                QString *err = nullptr);

  bool loadMailboxRawFrame(const PlcAddressLayoutV2 &layout,
                           const PlcMailboxRawFrame &frame,
                           QString *err = nullptr);

  bool readHoldingRegisters(quint32 start_address, quint16 reg_count,
                            QVector<quint16> *out,
                            QString *err = nullptr) override;

  bool writeHoldingRegisters(quint32 start_address,
                             const QVector<quint16> &values,
                             QString *err = nullptr) override;

private:
  bool connected_ = true;
  QHash<quint32, quint16> holding_registers_;
  QVector<PlcFakeWriteRecordV2> write_history_;
};

} // namespace core
