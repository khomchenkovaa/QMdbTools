#include "qsql_mdbtools.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlResult>
#include <QSqlRecord>
#include <QSqlField>
#include <QtSql/private/qsqldriver_p.h>
#include <QtSql/private/qsqlresult_p.h>

#include <QDebug>

#if defined Q_OS_WIN
# include <qt_windows.h>
#else
# include <unistd.h>
#endif

#include <mdbsql.h>

Q_DECLARE_OPAQUE_POINTER(MdbHandle*)
Q_DECLARE_METATYPE(MdbHandle*)

Q_DECLARE_OPAQUE_POINTER(MdbSQL*)
Q_DECLARE_METATYPE(MdbSQL*)

QT_BEGIN_NAMESPACE

/************************************************************/
/*
static QString _q_escapeIdentifier(const QString &identifier)
{
    QString res = identifier;
    if (!identifier.isEmpty() && !identifier.startsWith(QLatin1Char('"')) && !identifier.endsWith(QLatin1Char('"'))) {
        res.replace(QLatin1Char('"'), QLatin1String("\"\""));
        res.prepend(QLatin1Char('"')).append(QLatin1Char('"'));
        res.replace(QLatin1Char('.'), QLatin1String("\".\""));
    }
    return res;
}
*/
/************************************************************/

static QVariant::Type qGetColumnType(int mdbType)
{
    switch (mdbType) {
    case MDB_BOOL:     return QVariant::Bool;
    case MDB_BYTE:     return QVariant::Char;
    case MDB_INT:      return QVariant::Int;
    case MDB_LONGINT:  return QVariant::LongLong;
    case MDB_MONEY:    return QVariant::Double;
    case MDB_FLOAT:    return QVariant::Double;
    case MDB_DOUBLE:   return QVariant::Double;
    case MDB_DATETIME: return QVariant::DateTime;
    case MDB_BINARY:   return QVariant::ByteArray;
    case MDB_TEXT:     return QVariant::String;
    case MDB_OLE:      return QVariant::ByteArray;
    case MDB_MEMO:     return QVariant::String;
    case MDB_REPID:    return QVariant::LongLong;
    case MDB_NUMERIC:  return QVariant::Double;
    case MDB_COMPLEX:  return QVariant::String;
    }
    return QVariant::String;
}

/************************************************************/
/*
static QVariant::Type qGetColumnType(const QString &tpName)
{
    const QString typeName = tpName.toLower();

    if (typeName == QLatin1String("bool"))
        return QVariant::Bool;
    if (typeName == QLatin1String("byte"))
        return QVariant::Char;
    if (typeName == QLatin1String("int"))
        return QVariant::Int;
    if (typeName == QLatin1String("longint")
        || typeName == QLatin1String("repid"))
        return QVariant::LongLong;
    if (typeName == QLatin1String("money")
        || typeName == QLatin1String("float")
        || typeName == QLatin1String("double")
        || typeName == QLatin1String("numeric"))
        return QVariant::Double;
    if (typeName == QLatin1String("datetime"))
        return QVariant::DateTime;
    if (typeName == QLatin1String("binary")
        || typeName == QLatin1String("ole"))
        return QVariant::ByteArray;
    return QVariant::String;
}
*/
/************************************************************/

static QSqlError qMakeError(MdbSQL *access, const QString &descr,
                            QSqlError::ErrorType type,
                            int errorCode)
{
    return QSqlError(descr,
                     QString::fromLocal8Bit(access->error_msg),
                     type, QString::number(errorCode));
}

/************************************************************/

class QMdbToolsDriverPrivate : public QSqlDriverPrivate
{
    Q_DECLARE_PUBLIC(QMdbToolsDriver)

public:
    QMdbToolsDriverPrivate()
        : QSqlDriverPrivate(QSqlDriver::UnknownDbms)
    {
        access = mdb_sql_init();
    }

    ~QMdbToolsDriverPrivate()
    {
        mdb_sql_exit(access);
    }

    MdbSQL *access = nullptr;
};

/************************************************************/

class QMdbToolsResultPrivate;

class QMdbToolsResult : public QSqlResult
{
    Q_DECLARE_PRIVATE(QMdbToolsResult)
    friend class QSQLiteDriver;

public:
    explicit QMdbToolsResult(const QMdbToolsDriver* db);
    ~QMdbToolsResult();

protected:
    QVariant data(int index) override;
    bool isNull(int index) override;
    bool reset(const QString &query) override;
    bool fetch(int index) override;
    bool fetchFirst() override;
    bool fetchLast() override;
    int size() override;
    int numRowsAffected() override;
    QSqlRecord record() const override;
};

/************************************************************/

class QMdbToolsResultPrivate: public QSqlResultPrivate
{
    Q_DECLARE_PUBLIC(QMdbToolsResult)

public:
    Q_DECLARE_SQLDRIVER_PRIVATE(QMdbToolsDriver)
    QMdbToolsResultPrivate(QMdbToolsResult *q, const QMdbToolsDriver *db)
        : QSqlResultPrivate(q, db)
    {

    }

    inline void clearValues() {
        fieldCache.fill(QVariant());
        fieldCacheIdx = 0;
    }

    inline void clearData() {
        curIdx = -1;
        data.clear();
    }

    MdbSQL *access() const {
        return drv_d_func() ? drv_d_func()->access : Q_NULLPTR;
    }

    MdbHandle *handle() const {
        return access() ? access()->mdb : Q_NULLPTR;
    }

    bool isCurIdxValid() const {
        return (curIdx >= 0 && curIdx < data.size());
    }

    QSqlRecord rInf;
    QVector<QVariant> fieldCache;
    int fieldCacheIdx = 0;

    int curIdx = -1;
    QList<QVariantList> data;
};

/************************************************************/

QMdbToolsResult::QMdbToolsResult(const QMdbToolsDriver *db)
    : QSqlResult(*new QMdbToolsResultPrivate(this, db))
{

}

/************************************************************/

QMdbToolsResult::~QMdbToolsResult()
{

}

/************************************************************/

QVariant QMdbToolsResult::data(int index)
{
    Q_D(QMdbToolsResult);
    if (!d->isCurIdxValid())
        return QVariant();
    if (index >= d->rInf.count() || index < 0) {
        qWarning() << "QODBCResult::data: column" << index << "out of range";
        return QVariant();
    }
    if (index < d->fieldCacheIdx)
        return d->fieldCache.at(index);

    auto rec = d->data.at(d->curIdx);

    for (int i = d->fieldCacheIdx; i <= index; ++i) {
        const QSqlField info = d->rInf.field(i);
        switch (info.type()) {
        case QVariant::String:
            d->fieldCache[i] = rec.at(i).toString();
            break;
        default:
            d->fieldCache[i] = rec.at(i).toString();
            break;
        }
    }
    d->fieldCacheIdx = index + 1;
    return d->fieldCache[index];
}

/************************************************************/

bool QMdbToolsResult::isNull(int index)
{
    Q_D(QMdbToolsResult);
    if (index < 0 || index >= d->fieldCache.size())
        return true;
    if (index <= d->fieldCacheIdx) {
        // since there is no good way to find out whether the value is NULL
        // without fetching the field we'll fetch it here.
        // (data() also sets the NULL flag)
        data(index);
    }
    return d->fieldCache.at(index).isNull();
}

/************************************************************/

bool QMdbToolsResult::reset(const QString &query)
{
    Q_D(QMdbToolsResult);
    setActive(false);
    setAt(QSql::BeforeFirstRow);
    d->clearData();

    auto sql = d->access();
    mdb_sql_run_query(sql, const_cast<char *>(qUtf8Printable(query)));

    if (mdb_sql_has_error(sql)) {
        setLastError(qMakeError(sql, QString::fromUtf8("Cannot run query"), QSqlError::StatementError, -11));
        mdb_sql_reset(sql);
        return false;
    }

    d->rInf.clear();
    for (uint i = 0; i < sql->num_columns; i++) {
         MdbSQLColumn *col = static_cast<MdbSQLColumn *>(g_ptr_array_index(sql->columns, i));
         QString colName  = QString::fromUtf8(col->name);
         QSqlField fld(colName);
         d->rInf.append(fld);
    }

    while(mdb_fetch_row(sql->cur_table)) {
        QVariantList values;
        for (uint j=0; j<sql->num_columns; j++) {
            auto val = sql->bound_values[j];
            values << QString::fromUtf8(static_cast<char *>(val));
        }
        d->data << values;
    }

    mdb_sql_reset(sql);
    setActive(true);

    return true;
}

/************************************************************/

bool QMdbToolsResult::fetch(int index)
{
    Q_D(QMdbToolsResult);
    if (!driver()->isOpen())
        return false;
    if (index == at())
        return true;
    d->clearValues();
    if (index > QSql::BeforeFirstRow && index < d->data.size()) {
        d->curIdx = index;
        setAt(index);
        return true;
    }
    return false;
}

/************************************************************/

bool QMdbToolsResult::fetchFirst()
{
    Q_D(QMdbToolsResult);
    d->clearValues();
    return fetch(0);
}

/************************************************************/

bool QMdbToolsResult::fetchLast()
{
    Q_D(QMdbToolsResult);
    d->clearValues();
    return fetch(size() - 1);
}

/************************************************************/

int QMdbToolsResult::size()
{
    Q_D(QMdbToolsResult);
    return d->data.size();
}

/************************************************************/

int QMdbToolsResult::numRowsAffected()
{
    return -1;
}

/************************************************************/

QSqlRecord QMdbToolsResult::record() const
{
    Q_D(const QMdbToolsResult);
    if (!isActive() || !isSelect())
        return QSqlRecord();
    return d->rInf;
}

/************************************************************/

QMdbToolsDriver::QMdbToolsDriver(QObject * parent)
    : QSqlDriver(*new QMdbToolsDriverPrivate, parent)
{
}

/************************************************************/

QMdbToolsDriver::~QMdbToolsDriver()
{
    QMdbToolsDriver::close();
}

/************************************************************/

bool QMdbToolsDriver::hasFeature(DriverFeature f) const
{
    switch (f) {
    case Unicode:
        return true;
    case BLOB:
    case Transactions:
    case LastInsertId:
    case PreparedQueries:
    case PositionalPlaceholders:
    case SimpleLocking:
    case FinishQuery:
    case LowPrecisionNumbers:
    case EventNotifications:
    case QuerySize:
    case BatchOperations:
    case MultipleResultSets:
    case CancelQuery:
    case NamedPlaceholders:
        return false;
    }
    return false;
}

/************************************************************/
// MdbTools have no user name, passwords, hosts, ports or connect options. Just file names.
bool QMdbToolsDriver::open(const QString &db, const QString &, const QString &, const QString &, int, const QString &)
{
    Q_D(QMdbToolsDriver);
    if (isOpen())
        close();

    auto fileName = qPrintable(db);

    MdbHandle *handle = mdb_sql_open(d->access, const_cast<char*>(fileName));

    if (mdb_sql_has_error(d->access)) {
        setLastError(qMakeError(d->access,
                                tr("Error opening database"),
                     QSqlError::ConnectionError, -1));
        setOpenError(true);
        return false;
    }

    /* read the catalog */
    if (!mdb_read_catalog (handle, MDB_ANY)) {
        setLastError(qMakeError(d->access,
                                tr("File does not appear to be an Access database"),
                     QSqlError::ConnectionError, -2));
        setOpenError(true);
        return false;
    }

    setOpen(true);
    setOpenError(false);
    return true;
}

/************************************************************/

void QMdbToolsDriver::close()
{
    Q_D(QMdbToolsDriver);
    if (isOpen()) {
        mdb_sql_close(d->access);
        if (mdb_sql_has_error(d->access)) {
            setLastError(qMakeError(d->access, tr("Error closing database"),
                         QSqlError::ConnectionError, -2));
        }
        setOpen(false);
        setOpenError(false);
    }
}

/************************************************************/

QSqlResult *QMdbToolsDriver::createResult() const
{
    return new QMdbToolsResult(this);
}

/************************************************************/

QStringList QMdbToolsDriver::tables(QSql::TableType type) const
{
    auto mdb = d_func()->access->mdb;

    QStringList res;
    if (!isOpen())
        return res;

    if (!mdb) {
        res << QString::fromLatin1("MdbHandle is 0");
        return res;
    }

    /* loop over each entry in the catalog */
    for (unsigned int i=0; i < mdb->num_catalog; i++) {
        MdbCatalogEntry *entry = static_cast<MdbCatalogEntry *>(g_ptr_array_index (mdb->catalog, i));

        if ((type & QSql::Tables) && mdb_is_user_table(entry)) {
            res << QString::fromLocal8Bit(entry->object_name);
        }

        if ((type & QSql::SystemTables) && mdb_is_system_table(entry)) {
            res << QString::fromLocal8Bit(entry->object_name);
        }

        if ((type & QSql::Views) && (entry->object_type == MDB_QUERY)) {
            res << QString::fromLocal8Bit(entry->object_name);
        }
    }
    return res;
}

/************************************************************/

QSqlRecord QMdbToolsDriver::record(const QString &tbl) const
{
    auto mdb = d_func()->access->mdb;

    if (!isOpen())
        return QSqlRecord();

    QString tableName = tbl;
    if (isIdentifierEscaped(tableName, QSqlDriver::TableName))
        tableName = stripDelimiters(tableName, QSqlDriver::TableName);

    auto table = mdb_read_table_by_name(mdb, const_cast<char *>(qUtf8Printable(tableName)), MDB_TABLE);
    if (!table) {
        qDebug() << QString::fromLocal8Bit("Error: Table %1 does not exist in this database.").arg(tableName);
        return QSqlRecord();
    }

    /* read table */
    mdb_read_columns(table);
    mdb_rewind_table(table);

    QSqlRecord res;
    for (uint i = 0; i < table->num_cols; i++) {
         MdbColumn *col = static_cast<MdbColumn *>(g_ptr_array_index(table->columns, i));
         QString colName  = QString::fromUtf8(col->name);
         QSqlField fld(colName, qGetColumnType(col->col_type), tableName);
         fld.setSqlType(col->col_type);
         fld.setLength(col->col_size);
         fld.setPrecision(col->col_prec);
         fld.setReadOnly(col->is_fixed);
         fld.setAutoValue(col->is_long_auto);
         res.append(fld);
    }
    mdb_free_tabledef(table);

    return res;
}

/************************************************************/
/*
QSqlIndex QMdbToolsDriver::primaryIndex(const QString &tblname) const
{
    if (!isOpen())
        return QSqlIndex();

    QString table = tblname;
    if (isIdentifierEscaped(table, QSqlDriver::TableName))
        table = stripDelimiters(table, QSqlDriver::TableName);

    QSqlQuery q(createResult());
    q.setForwardOnly(true);
    return qGetTableInfo(q, table, true);
}
*/
/************************************************************/

QT_END_NAMESPACE
