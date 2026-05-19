#include "core/plc_codec_v26.hpp"

#include <QStringList>
#include <cstring>

namespace core::plc_codec_v26 {
namespace {
void setErr(QString *err, const QString &msg) { if (err) *err = msg; }
}

QByteArray regsToMbBytes(const QVector<quint16> &regs) {
  QByteArray bytes; bytes.reserve(regs.size() * 2);
  for (quint16 reg : regs) { bytes.append(static_cast<char>(reg & 0xFFu)); bytes.append(static_cast<char>((reg >> 8) & 0xFFu)); }
  return bytes;
}

QVector<quint16> mbBytesToRegs(const QByteArray &bytes) {
  QVector<quint16> regs; regs.reserve((bytes.size() + 1) / 2);
  for (int i = 0; i < bytes.size(); i += 2) {
    const quint16 lo = static_cast<unsigned char>(bytes.at(i));
    const quint16 hi = (i + 1 < bytes.size()) ? static_cast<unsigned char>(bytes.at(i + 1)) : 0u;
    regs.push_back(static_cast<quint16>((hi << 8) | lo));
  }
  return regs;
}

QString asciiFromMbBytes(const QByteArray &bytes) {
  QByteArray trimmed = bytes;
  const int n = trimmed.indexOf('\0');
  if (n >= 0) trimmed = trimmed.left(n);
  return QString::fromLatin1(trimmed).trimmed();
}

QByteArray asciiToMbBytes(const QString &text, int byteCount) {
  QByteArray bytes(byteCount, '\0');
  const QByteArray src = text.toLatin1().left(byteCount);
  for (int i = 0; i < src.size(); ++i) bytes[i] = src.at(i);
  return bytes;
}

bool readUInt16(const QVector<quint16> &regs, int offset, quint16 *out, QString *err) {
  if (!out || offset < 0 || offset >= regs.size()) { setErr(err, QStringLiteral("readUInt16 越界 offset=%1 size=%2").arg(offset).arg(regs.size())); return false; }
  *out = regs.at(offset); return true;
}

bool readInt16(const QVector<quint16> &regs, int offset, qint16 *out, QString *err) {
  quint16 v = 0; if (!readUInt16(regs, offset, &v, err) || !out) return false; *out = static_cast<qint16>(v); return true;
}

bool readUInt32WordSwapped(const QVector<quint16> &regs, int offset, quint32 *out, QString *err) {
  if (!out || offset < 0 || offset + 1 >= regs.size()) { setErr(err, QStringLiteral("readUInt32WordSwapped 越界 offset=%1 size=%2").arg(offset).arg(regs.size())); return false; }
  *out = (static_cast<quint32>(regs.at(offset + 1)) << 16) | regs.at(offset); return true;
}

bool readFloat32WordSwapped(const QVector<quint16> &regs, int offset, float *out, QString *err) {
  quint32 bits = 0; if (!readUInt32WordSwapped(regs, offset, &bits, err) || !out) return false; std::memcpy(out, &bits, sizeof(bits)); return true;
}

bool readFloat64WordSwapped(const QVector<quint16> &regs, int offset, double *out, QString *err) {
  if (!out) { setErr(err, QStringLiteral("readFloat64WordSwapped out指针为空")); return false; }
  if (offset < 0 || offset + 4 > regs.size()) { setErr(err, QStringLiteral("readFloat64WordSwapped 越界 offset=%1 size=%2").arg(offset).arg(regs.size())); return false; }
  quint64 bits{0};
  bits |= static_cast<quint64>(regs.at(offset)) << 0;
  bits |= static_cast<quint64>(regs.at(offset + 1)) << 16;
  bits |= static_cast<quint64>(regs.at(offset + 2)) << 32;
  bits |= static_cast<quint64>(regs.at(offset + 3)) << 48;
  std::memcpy(out, &bits, sizeof(bits)); return true;
}

QVector<int> slotMaskToLogicalSlots(quint16 mask) {
  QVector<int> slots; for (int bit = 0; bit < 16; ++bit) if (((mask >> bit) & 0x1u) != 0) slots.push_back(bit); return slots;
}

PlcMachineStateDecodedV26 decodeMachineState(quint16 mask) {
  PlcMachineStateDecodedV26 s; s.idle = mask & 0x0001u; s.auto_mode = mask & 0x0002u; s.manual = mask & 0x0004u; s.paused = mask & 0x0008u; s.fault = mask & 0x0010u; s.estop = mask & 0x0020u;
  QStringList parts; if (s.idle) parts << QStringLiteral("空闲"); if (s.auto_mode) parts << QStringLiteral("自动"); if (s.manual) parts << QStringLiteral("手动"); if (s.paused) parts << QStringLiteral("暂停"); if (s.fault) parts << QStringLiteral("错误"); if (s.estop) parts << QStringLiteral("急停");
  s.text = parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("|")); return s;
}

QStringList decodeInterlockBits(quint32 mask) {
  static const QStringList kBitNames = {
      QStringLiteral("龙门X轴异常"),            // 0
      QStringLiteral("龙门Y轴异常"),            // 1
      QStringLiteral("龙门Z轴异常"),            // 2
      QStringLiteral("X1测量工位移动轴异常"),    // 3
      QStringLiteral("X2测量工位移动轴异常"),    // 4
      QStringLiteral("X3测量工位移动轴异常"),    // 5
      QStringLiteral("R1旋转轴异常"),           // 6
      QStringLiteral("R2旋转轴异常"),           // 7
      QStringLiteral("R3旋转轴异常"),           // 8
      QStringLiteral("R4旋转轴异常"),           // 9
      QStringLiteral("抓料气缸异常"),           // 10
      QStringLiteral("X1测量工位夹抓气缸异常"),  // 11
      QStringLiteral("X2测量工位夹抓气缸异常"),  // 12
      QStringLiteral("X3测量工位夹抓气缸异常"),  // 13
      QStringLiteral("长度测量G1气缸异常"),      // 14
      QStringLiteral("长度测量G2气缸异常"),      // 15
      QStringLiteral("长度测量G3气缸异常"),      // 16
      QStringLiteral("长度测量G4气缸异常")       // 17
  };

  QStringList out;
  quint32 knownMask = 0;
  for (int bit = 0; bit < kBitNames.size(); ++bit) {
    const quint32 flag = (1u << bit);
    knownMask |= flag;
    if ((mask & flag) != 0) {
      out << kBitNames.at(bit);
    }
  }
  const quint32 unknown = (mask & ~knownMask);
  if (unknown != 0) {
    out << QStringLiteral("未知互锁位=0x%1").arg(QString::number(unknown, 16).toUpper());
  }
  return out;
}

QString plcModeText(qint16 mode) {
  switch (mode) { case 1: return QStringLiteral("手动"); case 2: return QStringLiteral("自动"); case 3: return QStringLiteral("单步"); default: return QStringLiteral("模式(%1)").arg(mode); }
}

} // namespace core::plc_codec_v26
