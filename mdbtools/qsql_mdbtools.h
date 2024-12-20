#ifndef QSQL_MDBTOOLS_H
#define QSQL_MDBTOOLS_H

#include <QtSql/qsqldriver.h>

#ifdef QT_PLUGIN
#define Q_EXPORT_SQLDRIVER_MDBTOOLS
#else
#define Q_EXPORT_SQLDRIVER_MDBTOOLS Q_SQL_EXPORT
#endif

QT_BEGIN_NAMESPACE

class QSqlResult;
class QMdbToolsDriverPrivate;

class Q_EXPORT_SQLDRIVER_MDBTOOLS QMdbToolsDriver : public QSqlDriver
{
    Q_DECLARE_PRIVATE(QMdbToolsDriver)
    Q_OBJECT
    friend class QMdbToolsResult;

public:
    explicit QMdbToolsDriver(QObject *parent = nullptr);
    ~QMdbToolsDriver();

    bool hasFeature(DriverFeature f) const override;
    bool open(const QString & db,
                   const QString & user,
                   const QString & password,
                   const QString & host,
                   int port,
                   const QString & connOpts) override;
    void close() override;
    QSqlResult *createResult() const override;
    QStringList tables(QSql::TableType) const override;

    QSqlRecord record(const QString& tablename) const override;
//    QSqlIndex primaryIndex(const QString &table) const override;
};

QT_END_NAMESPACE

#endif // QSQL_MDBTOOLS_H
