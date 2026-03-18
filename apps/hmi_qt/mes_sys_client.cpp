#include "mes_sys_client.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QString norm(const QString &v) { return v.trimmed(); }

bool isSuccessFlag(const QString &v) {
  const QString s = v.trimmed().toLower();
  return s == QStringLiteral("1") || s == QStringLiteral("true") ||
         s == QStringLiteral("y") || s == QStringLiteral("yes") ||
         s == QStringLiteral("ok");
}

struct RawHttpResult {
  bool transport_ok = false;
  int http_code = 0;
  QString error;
  QString body;
  QJsonObject root;
};

RawHttpResult postParamsSync(const core::AppConfig &cfg, const QString &urlText,
                             const QList<QPair<QString, QString>> &params) {
  RawHttpResult out;
  const QString resolved = urlText.trimmed();
  if (resolved.isEmpty()) {
    out.error = QStringLiteral("MES 接口地址为空");
    return out;
  }

  QUrl url(resolved);
  if (!url.isValid() || url.scheme().trimmed().isEmpty()) {
    out.error = QStringLiteral("MES 接口地址非法: %1").arg(resolved);
    return out;
  }

  QUrlQuery query(url);
  QByteArray formBody;
  bool first = true;
  for (const auto &kv : params) {
    query.addQueryItem(kv.first, kv.second);
    if (!first)
      formBody += '&';
    first = false;
    formBody += QUrl::toPercentEncoding(kv.first);
    formBody += '=';
    formBody += QUrl::toPercentEncoding(kv.second);
  }
  url.setQuery(query);

  QNetworkAccessManager nam;
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/x-www-form-urlencoded; charset=utf-8"));
  req.setRawHeader("Accept", "application/json");
  if (!cfg.mes.auth_token.trimmed().isEmpty()) {
    req.setRawHeader("Authorization",
                     QByteArray("Bearer ") + cfg.mes.auth_token.toUtf8());
  }

  QNetworkReply *reply = nam.post(req, formBody);
  QTimer timeout;
  timeout.setSingleShot(true);
  QEventLoop loop;
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
    out.error = QStringLiteral("请求超时");
    if (reply)
      reply->abort();
    loop.quit();
  });
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timeout.start(cfg.mes.timeout_ms);
  loop.exec();
  timeout.stop();

  if (!reply)
    return out;

  out.http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QString netErr =
      (reply->error() == QNetworkReply::NoError) ? QString() : reply->errorString();
  out.body = QString::fromUtf8(reply->readAll());
  if (out.error.isEmpty() && !netErr.isEmpty())
    out.error = netErr;
  out.transport_ok = out.error.isEmpty() && out.http_code >= 200 && out.http_code < 300;

  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(out.body.toUtf8(), &pe);
  if (pe.error == QJsonParseError::NoError && doc.isObject()) {
    out.root = doc.object();
  }

  reply->deleteLater();
  return out;
}

} // namespace

MesSysClient::MesSysClient(const core::AppConfig &cfg) : cfg_(cfg) {}

MesSysHeartbeatResult MesSysClient::heartbeat(const QString &sysid) const {
  MesSysHeartbeatResult out;
  const RawHttpResult http = postParamsSync(
      cfg_, core::resolveMesInterfaceUrl(cfg_.mes, QStringLiteral("MES_SYS_HEARTBEAT")),
      {{QStringLiteral("SYSID"), norm(sysid)}});
  out.transport_ok = http.transport_ok;
  out.http_code = http.http_code;
  out.error = http.error;
  out.raw_body = http.body;
  out.code = http.root.value(QStringLiteral("code")).toString();
  out.message = http.root.value(QStringLiteral("message")).toString();
  out.server_time = http.root.value(QStringLiteral("data")).toString();
  out.business_ok = out.transport_ok && out.code.trimmed().isEmpty();
  if (!out.transport_ok && out.error.isEmpty())
    out.error = QStringLiteral("HTTP %1").arg(out.http_code);
  if (out.transport_ok && !out.business_ok && out.error.isEmpty())
    out.error = out.message.isEmpty() ? QStringLiteral("Heartbeat 返回失败") : out.message;
  return out;
}

MesSysOpCheckUserResult MesSysClient::opCheckUser(const QString &sysid,
                                                  const QString &userId) const {
  MesSysOpCheckUserResult out;
  const RawHttpResult http = postParamsSync(
      cfg_, core::resolveMesInterfaceUrl(cfg_.mes, QStringLiteral("MES_SYS_OP_CHECK_USER")),
      {{QStringLiteral("SYSID"), norm(sysid)},
       {QStringLiteral("UserID"), norm(userId)}});
  out.transport_ok = http.transport_ok;
  out.http_code = http.http_code;
  out.error = http.error;
  out.raw_body = http.body;
  out.code = http.root.value(QStringLiteral("code")).toString();
  out.message = http.root.value(QStringLiteral("message")).toString();
  const QJsonObject data = http.root.value(QStringLiteral("data")).toObject();
  out.success_flag = data.value(QStringLiteral("success")).toVariant().toString();
  out.business_ok = out.transport_ok && out.code.trimmed().isEmpty() &&
                    isSuccessFlag(out.success_flag);
  if (!out.transport_ok && out.error.isEmpty())
    out.error = QStringLiteral("HTTP %1").arg(out.http_code);
  if (out.transport_ok && !out.business_ok && out.error.isEmpty()) {
    out.error = out.message.isEmpty()
                    ? QStringLiteral("OpCheckUser 返回 success=%1").arg(out.success_flag)
                    : out.message;
  }
  return out;
}

MesSysOpCheckTechStateResult MesSysClient::opCheckTechState(
    const QString &sysid, const QString &userId, const QString &techId,
    const QString &deviceId) const {
  MesSysOpCheckTechStateResult out;
  const RawHttpResult http = postParamsSync(
      cfg_, core::resolveMesInterfaceUrl(cfg_.mes, QStringLiteral("MES_SYS_OP_CHECK_TECH_STATE")),
      {{QStringLiteral("SYSID"), norm(sysid)},
       {QStringLiteral("UserID"), norm(userId)},
       {QStringLiteral("TechID"), norm(techId)},
       {QStringLiteral("DeviceID"), norm(deviceId)}});
  out.transport_ok = http.transport_ok;
  out.http_code = http.http_code;
  out.error = http.error;
  out.raw_body = http.body;
  out.code = http.root.value(QStringLiteral("code")).toString();
  out.message = http.root.value(QStringLiteral("message")).toString();
  const QJsonObject data = http.root.value(QStringLiteral("data")).toObject();
  out.success_flag = data.value(QStringLiteral("success")).toVariant().toString();
  out.op_no = data.value(QStringLiteral("OpNo")).toVariant().toString();
  out.tech_state = data.value(QStringLiteral("techState")).toArray();
  out.business_ok = out.transport_ok && out.code.trimmed().isEmpty() &&
                    isSuccessFlag(out.success_flag);
  if (!out.transport_ok && out.error.isEmpty())
    out.error = QStringLiteral("HTTP %1").arg(out.http_code);
  if (out.transport_ok && !out.business_ok && out.error.isEmpty()) {
    out.error = out.message.isEmpty()
                    ? QStringLiteral("OpCheckTechState 返回 success=%1").arg(out.success_flag)
                    : out.message;
  }
  return out;
}
