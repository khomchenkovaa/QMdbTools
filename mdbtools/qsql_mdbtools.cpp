#include "qsql_mdbtools.h"

#include <QCoreApplication>
#include <QSqlError>
#include <QSqlResult>
#include <QSqlRecord>
#include <QSqlField>
#include <QtSql/private/qsqldriver_p.h>

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
static QString qTypeName(int mdbType) {
    switch (mdbType) {
    case MDB_BOOL:     return QLatin1String("bool");
    case MDB_BYTE:     return QLatin1String("byte");
    case MDB_INT:      return QLatin1String("int");
    case MDB_LONGINT:  return QLatin1String("longint");
    case MDB_MONEY:    return QLatin1String("money");
    case MDB_FLOAT:    return QLatin1String("float");
    case MDB_DOUBLE:   return QLatin1String("double");
    case MDB_DATETIME: return QLatin1String("datetime");
    case MDB_BINARY:   return QLatin1String("binary");
    case MDB_TEXT:     return QLatin1String("text");
    case MDB_OLE:      return QLatin1String("ole");
    case MDB_MEMO:     return QLatin1String("memo");
    case MDB_REPID:    return QLatin1String("repid");
    case MDB_NUMERIC:  return QLatin1String("numeric");
    case MDB_COMPLEX:  return QLatin1String("complex");
    }
    return QLatin1String("unknown");
}
*/
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

class QMdbToolsResult : public QSqlResult
{
    friend class QSQLiteDriver;

    struct QMdbToolsResultData {
        int curIdx = -1;
        QList<QVariantList> data;
        const QMdbToolsDriver* db = Q_NULLPTR;
    };

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

private:
    QMdbToolsResultData d;
};

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

QMdbToolsResult::QMdbToolsResult(const QMdbToolsDriver *db)
    : QSqlResult(db)
{
    d.db = db;
}

/************************************************************/

QMdbToolsResult::~QMdbToolsResult()
{

}

/************************************************************/

QVariant QMdbToolsResult::data(int index)
{
    if (d.curIdx == -1 && d.curIdx >= d.data.size())
        return QVariant();
    auto rec = d.data.at(d.curIdx);
    if (index >= 0 && index < rec.size()) {
        return rec.at(index);
    }
    return QVariant();
}

/************************************************************/

bool QMdbToolsResult::isNull(int index)
{
    auto val = data(index);
    return !val.isValid();
}

/************************************************************/

bool QMdbToolsResult::reset(const QString &query)
{
    auto sql = d.db->d_func()->access;
    mdb_sql_run_query(sql, const_cast<char *>(qUtf8Printable(query)));

    if (mdb_sql_has_error(sql)) {
        setLastError(qMakeError(sql, QString::fromUtf8("Cannot run query"), QSqlError::StatementError, -11));
        mdb_sql_reset(sql);
        return false;
    }

    // TODO

    mdb_sql_reset(sql);
    return true;
}

/************************************************************/

bool QMdbToolsResult::fetch(int index)
{
    Q_UNUSED(index)
    return false;
}

/************************************************************/

bool QMdbToolsResult::fetchFirst()
{
    return false;
}

/************************************************************/

bool QMdbToolsResult::fetchLast()
{
    return false;
}

/************************************************************/

int QMdbToolsResult::size()
{
    return 0;
}

/************************************************************/

int QMdbToolsResult::numRowsAffected()
{
    return 0;
}

/************************************************************/

QSqlRecord QMdbToolsResult::record() const
{
    if (!isActive() || !isSelect())
        return QSqlRecord();
    return QSqlRecord();
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
    case BLOB:
    case Transactions:
    case Unicode:
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
/*
static QSqlIndex qGetTableInfo(QSqlQuery &q, const QString &tableName, bool onlyPIndex = false)
{
    QString schema;
    QString table(tableName);
    int indexOfSeparator = tableName.indexOf(QLatin1Char('.'));
    if (indexOfSeparator > -1) {
        schema = tableName.left(indexOfSeparator).append(QLatin1Char('.'));
        table = tableName.mid(indexOfSeparator + 1);
    }
    q.exec(QLatin1String("PRAGMA ") + schema + QLatin1String("table_info (") + _q_escapeIdentifier(table) + QLatin1Char(')'));

    QSqlIndex ind;
    while (q.next()) {
        bool isPk = q.value(5).toInt();
        if (onlyPIndex && !isPk)
            continue;
        QString typeName = q.value(2).toString().toLower();
        QString defVal = q.value(4).toString();
        if (!defVal.isEmpty() && defVal.at(0) == QLatin1Char('\'')) {
            const int end = defVal.lastIndexOf(QLatin1Char('\''));
            if (end > 0)
                defVal = defVal.mid(1, end - 1);
        }

        QSqlField fld(q.value(1).toString(), qGetColumnType(typeName), tableName);
        if (isPk && (typeName == QLatin1String("integer")))
            // INTEGER PRIMARY KEY fields are auto-generated in sqlite
            // INT PRIMARY KEY is not the same as INTEGER PRIMARY KEY!
            fld.setAutoValue(true);
        fld.setRequired(q.value(3).toInt() != 0);
        fld.setDefaultValue(defVal);
        ind.append(fld);
    }
    return ind;
}
*/
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
