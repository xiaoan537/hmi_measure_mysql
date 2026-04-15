#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include "core/plc_types_v26.hpp"

namespace core::plc_codec_v26 {

QByteArray regsToMbBytes(const QVector<quint16> &regs);
QVector<quint16> mbBytesToRegs(const QByteArray &bytes);
QString asciiFromMbBytes(const QByteArray &bytes);
QByteArray asciiToMbBytes(const QString &text, int byteCount);

bool readUInt16(const QVector<quint16> &regs, int offset, quint16 *out, QString *err = nullptr);
bool readInt16(const QVector<quint16> &regs, int offset, qint16 *out, QString *err = nullptr);
bool readUInt32WordSwapped(const QVector<quint16> &regs, int offset, quint32 *out, QString *err = nullptr);
bool readFloat32WordSwapped(const QVector<quint16> &regs, int offset, float *out, QString *err = nullptr);
bool readFloat64WordSwapped(const QVector<quint16> &regs, int offset, double *out, QString *err = nullptr);

QVector<int> slotMaskToLogicalSlots(quint16 mask);
PlcMachineStateDecodedV26 decodeMachineState(quint16 mask);
QString plcModeText(qint16 mode);

} // namespace core::plc_codec_v26
