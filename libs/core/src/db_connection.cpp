#include "core/db.hpp"

#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

namespace core {

bool Db::open(const DbConfig &cfg, QString *err) {
  /*
  检查数据库驱动是否可用，如果不可用则返回错误信息，提示用户安装所需的数据库驱动包。
  QSqlDatabase::isDriverAvailable() 方法用于检查数据库驱动是否可用。
  如果数据库驱动不可用，则返回 false，否则返回 true。
  */
  if (!QSqlDatabase::isDriverAvailable(cfg.driver)) {

    /*
    err是一个指向QString的指针参数。空指针检查：if
    (err)用于验证调用者是否传递了一个有效的指针。
    如果err是nullptr（或NULL），这个条件就会失败，不会尝试设置错误信息。
    这种设计允许调用者灵活选择是否需要接收错误信息：
        如果需要错误信息，调用者可以传递一个QString变量的地址：
            QString errorMsg;
            db.open(config, &errorMsg);
        如果不需要错误信息，调用这可以传递nullptr:
            db.open(config, nullptr);
    通过先检查指针是否为空，再使用*err =
    ...赋值，可以避免空指针解引用导致的程序崩溃。
    */

    if (err)
      *err = QString("DB driver not available: %1 (install libqt5sql5-mysql)")
                 .arg(cfg.driver);
    return false;

    /*
    错误信息设置：
        当检测到所需的数据库驱动不可用时，它会设置一个错误信息字符串。
    字符串格式化：
        使用 QString::arg() 方法进行格式化。%1 是一个占位符，会被 cfg.driver
    的值替换
    */
  }

  /*
  连接池模式：
      连接池模式是一种数据库连接管理技术，用于在应用程序中重复使用数据库连接，而不是每次都创建新的连接。
      这可以提高应用程序的性能和响应时间，因为创建和关闭数据库连接是一个相对耗时的操作。
      连接池模式的工作原理是维护一个连接池，其中包含多个数据库连接。
      当应用程序需要与数据库交互时，它从连接池获取一个连接，执行操作后，将连接返回给连接池而不是关闭它。
      这样，下一次需要与数据库交互时，就可以直接使用连接池中的连接，而无需重新创建。
  */

  // 使用连接池模式，避免重复创建连接对象（连接池模式：QT框架管理数据库连接的一种常见且推荐的做法）
  // 调用此方法后，一个名为 "hmi_conn"的新连接就被添加到Qt的管理列表中，并赋值给
  // db_。
  const QString connName = "hmi_conn";
  if (QSqlDatabase::contains(
          connName)) // 静态函数
                     // QSqlDatabase::contains()会检查当前应用程序中是否已经存在一个名为
                     // "hmi_conn"的连接
  {
    db_ = QSqlDatabase::database(connName); // 从连接池中获取已存在的连接
  } else {
    db_ = QSqlDatabase::addDatabase(cfg.driver, connName); // 向连接池添加新连接
  }

  // 连接参数设置
  db_.setHostName(cfg.host);
  db_.setPort(cfg.port);
  db_.setDatabaseName(cfg.name);
  db_.setUserName(cfg.user);
  db_.setPassword(cfg.pass);
  if (!cfg.options.trimmed().isEmpty()) {
    db_.setConnectOptions(cfg.options);
  }

  /*
  .trimmed() 去除字符串两端的空白字符
  .isEmpty() 检查字符串是否为空
  整个条件判断：如果去除空白后的选项字符串不为空，则执行设置
  调用Qt的QSqlDatabase对象的setConnectOptions方法
  将配置中的选项字符串应用到数据库连接上
  */
  // !db_.open() 检查连接是否失败
  if (!db_.open()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }
  return true;
  /*
  if (err) 检查调用者是否提供了错误信息存储位置（非空指针）
  db_.lastError() 获取数据库操作产生的最后一个错误对象
  .text() 将错误对象转换为可读的错误信息字符串
  *err = ... 将错误信息存储到调用者提供的字符串变量中
  */
}

// ensureSchema
// 方法的主要作用是确保数据库中存在所需的数据表结构，如果不存在则创建它们。这是一种"幂等"操作，即无论执行多少次，结果都是一致的。

bool Db::beginTx(QString *err) {
  if (db_.transaction())
    return true;
  if (err)
    *err = db_.lastError().text();
  return false;
}

bool Db::commitTx(QString *err) {
  if (db_.commit())
    return true;
  if (err)
    *err = db_.lastError().text();
  return false;
}

bool Db::rollbackTx(QString *err) {
  if (db_.rollback())
    return true;
  if (err)
    *err = db_.lastError().text();
  return false;
}


} // namespace core
