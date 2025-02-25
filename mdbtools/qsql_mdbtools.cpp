#include "qsql_mdbtools.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlResult>
#include <QSqlRecord>
#include <QSqlField>
#include <QSqlIndex>
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

static QSqlError qMakeError(const QString &dbError, const QString &descr,
                            QSqlError::ErrorType type,
                            int errorCode)
{
    return QSqlError(descr, dbError,
                     type, QString::number(errorCode));
}

/************************************************************/

static QSqlField qMakeField(MdbColumn *col)
{
    QString colName   = QString::fromUtf8(col->name);
    QString tableName = QString::fromUtf8(col->table->name);
    QSqlField fld(colName, qGetColumnType(col->col_type), tableName);
    fld.setSqlType(col->col_type);
    fld.setLength(col->col_size);
    fld.setPrecision(col->col_prec);
    fld.setReadOnly(col->is_fixed);
    fld.setAutoValue(col->is_long_auto);
    return fld;
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

    MdbHandle *handle() const {
        return access->mdb;
    }

    MdbHandle *open(const QString &file) {
         auto fileName = qPrintable(file);
         return mdb_sql_open(access, const_cast<char*>(fileName));
    }

    void close() {
        mdb_sql_close(access);
    }

    bool hasError() const {
        return mdb_sql_has_error(access);
    }

    QString lastError() const {
        return QString::fromLocal8Bit(access->error_msg);
    }

    MdbSQL *access = Q_NULLPTR;
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
    QVariant handle() const override;
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

    inline void clearData() {
        data.clear();
    }

    inline void clearInfo() {
        recInf.clear();
        cols.clear();
    }

    MdbSQL *access() const {
        return drv_d_func() ? drv_d_func()->access : Q_NULLPTR;
    }

    MdbHandle *handle() const {
        return access() ? access()->mdb : Q_NULLPTR;
    }

    bool isRowValid(int idx) const {
        return (idx > QSql::BeforeFirstRow && idx < data.size());
    }

    bool isFieldIdxInRange(int idx) const {
        return (idx >= 0 && idx < recInf.count());
    }

    QSqlRecord recInf;
    QList<MdbColumn*> cols;
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
/// Returns the data for field index in the current row as a QVariant.
/// This function is only called if the result is in an active state and is positioned on a valid record and index is non-negative.
QVariant QMdbToolsResult::data(int index)
{
    Q_D(QMdbToolsResult);
    if (!d->isRowValid(at()))
        return QVariant();

    if (!d->isFieldIdxInRange(index)) {
        qWarning() << "QMdbToolsResult::data: column" << index << "out of range";
        return QVariant();
    }

    auto rec = d->data.at(at());

    const QSqlField info = d->recInf.field(index);
    switch (info.type()) {
    case QVariant::String:
        return rec.at(index).toString();
        break;
    default:
        return rec.at(index).toString();
        break;
    }
    return QVariant();
}

/************************************************************/
/// Returns true if the field at position index in the current row is null; otherwise returns false.
bool QMdbToolsResult::isNull(int index)
{
    Q_D(QMdbToolsResult);
    if (!d->isRowValid(at()))
        return true;
    if (!d->isFieldIdxInRange(index))
        return true;
    auto rec = d->data.at(at());
    return rec.at(index).isNull();
}

/************************************************************/
/// Sets the result to use the SQL statement query for subsequent data retrieval.
/// \return true if the query was successful and ready to be used, or false otherwise
bool QMdbToolsResult::reset(const QString &query)
{
    Q_D(QMdbToolsResult);
    setActive(false);
    setAt(QSql::BeforeFirstRow);
    d->clearData();

    auto sql = d->access();
    mdb_sql_run_query(sql, const_cast<char *>(qUtf8Printable(query)));

    if (mdb_sql_has_error(sql)) {
        setLastError(qMakeError(QString::fromLocal8Bit(sql->error_msg),
                                QString::fromUtf8("Cannot run query"),
                                QSqlError::StatementError, -11));
        mdb_sql_reset(sql);
        return false;
    }

    d->clearInfo();
    auto table = sql->cur_table;
    for (uint i = 0; i < sql->num_columns; i++) {
         MdbSQLColumn *sqlCol = static_cast<MdbSQLColumn *>(g_ptr_array_index(sql->columns, i));
         MdbColumn *col = Q_NULLPTR;
         for (uint j=0; j < table->num_cols; ++j) {
             MdbColumn *tblCol = static_cast<MdbColumn*>(g_ptr_array_index(table->columns, j));
             if (!g_ascii_strcasecmp(sqlCol->name, tblCol->name)) {
                 col = tblCol;
                 break;
             }
         }
         d->cols << col;
         if (col) {
             auto fld = qMakeField(col);
             d->recInf.append(fld);
         } else {
             QString colName   = QString::fromUtf8(sqlCol->name);
             QString tableName = QString::fromUtf8(table->name);
             QSqlField fld(colName, QVariant::String, tableName);
             fld.setSqlType(MDB_TEXT);
             fld.setReadOnly(true);
             d->recInf.append(fld);
         }
    }

    while(mdb_fetch_row(table)) {
        QVariantList values;
        for (uint i=0; i<sql->num_columns; ++i) {
            guint32 ole_len = 0;
            auto col = d->cols.at(i);
            int type = (col ? col->col_type : MDB_TEXT);
            switch (type) {
            case MDB_OLE:
                ole_len = mdb_get_int32(col->bind_ptr, 0);
                if (ole_len) {
                    size_t size = 0;
                    auto val = mdb_ole_read_full(sql->mdb, col, &size);
                    auto rawData = QByteArray::fromRawData(static_cast<char *>(val), size);
                    values << QString::fromUtf8(rawData);
                    g_free(val);
                } else {
                    values << QVariant();
                }
                break;
            default: {
                auto val = sql->bound_values[i];
                values << QString::fromUtf8(static_cast<char *>(val));
            }   break;
            }
        }
        d->data << values;
    }

    mdb_sql_reset(sql);
    setActive(true);
    setSelect(true);

    return true;
}

/************************************************************/
/// Positions the result to an arbitrary (zero-based) row index.
/// \return true to indicate success, or false to signify failure.
bool QMdbToolsResult::fetch(int index)
{
    Q_D(QMdbToolsResult);
    if (!driver()->isOpen())
        return false;
    if (index == at())
        return true;
    if (d->isRowValid(index)) {
        setAt(index);
        return true;
    }
    return false;
}

/************************************************************/
/// Positions the result to the first record (row 0) in the result.
/// \return true to indicate success, or false to signify failure.
bool QMdbToolsResult::fetchFirst()
{
    return fetch(0);
}

/************************************************************/
/// Positions the result to the last record (last row) in the result.
/// \return true to indicate success, or false to signify failure.
bool QMdbToolsResult::fetchLast()
{
    return fetch(size() - 1);
}

/************************************************************/
/// Returns the size of the SELECT result, or -1 if it cannot be determined or if the query is not a SELECT statement.
int QMdbToolsResult::size()
{
    Q_D(QMdbToolsResult);
    return d->data.size();
}

/************************************************************/
/// Returns the number of rows affected by the last query executed, or -1 if it cannot be determined or if the query is a SELECT statement.
int QMdbToolsResult::numRowsAffected()
{
    return -1;
}

/************************************************************/
/// Returns the current record if the query is active; otherwise returns an empty QSqlRecord.
QSqlRecord QMdbToolsResult::record() const
{
    Q_D(const QMdbToolsResult);
    if (!isActive() || !isSelect())
        return QSqlRecord();
    return d->recInf;
}

/************************************************************/
/// Returns the low-level handle for this result set (MdbSQL*) wrapped in a QVariant
QVariant QMdbToolsResult::handle() const
{
    return QVariant::fromValue(d_func()->access());
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
/// Returns true if the driver supports feature; otherwise returns false.
bool QMdbToolsDriver::hasFeature(DriverFeature f) const
{
    switch (f) {
    case DriverFeature::Transactions:
        return false;
    case DriverFeature::QuerySize:
        return true;
    case DriverFeature::BLOB:
        return false;
    case DriverFeature::Unicode:
        return true;
    case DriverFeature::PreparedQueries:
    case DriverFeature::NamedPlaceholders:
    case DriverFeature::PositionalPlaceholders:
    case DriverFeature::LastInsertId:
    case DriverFeature::BatchOperations:
    case DriverFeature::SimpleLocking:
    case DriverFeature::LowPrecisionNumbers:
    case DriverFeature::EventNotifications:
    case DriverFeature::FinishQuery:
    case DriverFeature::MultipleResultSets:
    case DriverFeature::CancelQuery:
        return false;
    }
    return false;
}

/************************************************************/
/// \brief Open a database connection on database db (file name).
/// MdbTools have no user name, password, host, port or connection options. Just file names.
/// \return return true on success and false on failure.
bool QMdbToolsDriver::open(const QString &db, const QString &, const QString &, const QString &, int, const QString &)
{
    Q_D(QMdbToolsDriver);
    if (isOpen())
        close();

    MdbHandle *handle = d->open(db);

    if (d->hasError()) {
        setLastError(qMakeError(d->lastError(),
                                tr("Error opening database"),
                     QSqlError::ConnectionError, -1));
        setOpenError(true);
        return false;
    }

    /* read the catalog */
    if (!mdb_read_catalog (handle, MDB_ANY)) {
        setLastError(qMakeError(d->lastError(),
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
/// Close the database connection.
void QMdbToolsDriver::close()
{
    Q_D(QMdbToolsDriver);
    if (isOpen()) {
        d->close();
        if (d->hasError()) {
            setLastError(qMakeError(d->lastError(), tr("Error closing database"),
                         QSqlError::ConnectionError, -2));
        }
        setOpen(false);
        setOpenError(false);
    }
}

/************************************************************/
/// \return a QSqlResult object
QSqlResult *QMdbToolsDriver::createResult() const
{
    return new QMdbToolsResult(this);
}

/************************************************************/
/// Returns a list of the names of the tables in the database.
QStringList QMdbToolsDriver::tables(QSql::TableType type) const
{
    auto mdb = d_func()->handle();

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
/// Returns the low-level database handle (MdbHandle*) wrapped in a QVariant
QVariant QMdbToolsDriver::handle() const
{
    return QVariant::fromValue(d_func()->handle());
}

/************************************************************/
/// Returns a QSqlRecord populated with the names of the fields in table tableName.
/// If no such table exists, an empty record is returned.
QSqlRecord QMdbToolsDriver::record(const QString &tbl) const
{
    auto mdb = d_func()->handle();

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
         auto fld = qMakeField(col);
         res.append(fld);
    }
    mdb_free_tabledef(table);

    return res;
}

/************************************************************/
/// Returns the primary index for table tableName.
/// Returns an empty QSqlIndex if the table doesn't have a primary index.
QSqlIndex QMdbToolsDriver::primaryIndex(const QString &tblname) const
{
    if (!isOpen())
        return QSqlIndex();

    QString table = tblname;
    if (isIdentifierEscaped(table, QSqlDriver::TableName))
        table = stripDelimiters(table, QSqlDriver::TableName);

    // TODO implement me
    return QSqlIndex();
}

/************************************************************/

QT_END_NAMESPACE
