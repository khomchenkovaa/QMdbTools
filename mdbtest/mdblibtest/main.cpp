#include <QCoreApplication>

#include <mdbsql.h>
#include "glib/gstrfuncs.h"

#include <QtSql>
#include <QDebug>

QString typeName(int mdbType) {
    switch (mdbType) {
    case MDB_BOOL:     return "BOOL";
    case MDB_BYTE:     return "BYTE";
    case MDB_INT:      return "INT";
    case MDB_LONGINT:  return "LONGINT";
    case MDB_MONEY:    return "MONEY";
    case MDB_FLOAT:    return "FLOAT";
    case MDB_DOUBLE:   return "DOUBLE";
    case MDB_DATETIME: return "DATETIME";
    case MDB_BINARY:   return "BINARY";
    case MDB_TEXT:     return "TEXT";
    case MDB_OLE:      return "OLE";
    case MDB_MEMO:     return "MEMO";
    case MDB_REPID:    return "REPID";
    case MDB_NUMERIC:  return "NUMERIC";
    case MDB_COMPLEX:  return "COMPLEX";
    }
    return "Unknown";
}

QStringList objects(MdbHandle *mdb, QSql::TableType type) {
    QStringList res;
    if (!mdb) {
        res << QString::fromLatin1("MdbHandle is 0");
        return res;
    }
    /* loop over each entry in the catalog */
    for (unsigned int i=0; i < mdb->num_catalog; i++) {
        MdbCatalogEntry *entry = static_cast<MdbCatalogEntry *>(g_ptr_array_index (mdb->catalog, i));

        if ((type & QSql::Tables) && mdb_is_user_table(entry)) {
            res << QString::fromUtf8(entry->object_name);
        }

        if ((type & QSql::SystemTables) && mdb_is_system_table(entry)) {
            res << QString::fromUtf8(entry->object_name);
        }

        if ((type & QSql::Views) && (entry->object_type == MDB_QUERY)) {
            res << QString::fromUtf8(entry->object_name);
        }
    }
    return res;
}

MdbTableDef *tableDef(MdbHandle *mdb, const QString &tableName) {
    auto table = mdb_read_table_by_name(mdb, const_cast<char *>(qUtf8Printable(tableName)), MDB_TABLE);
    if (!table) {
        qDebug() << QString("Error: Table %1 does not exist in this database.").arg(tableName);
        return table;
    }

    /* read table */
    mdb_read_columns(table);
    mdb_rewind_table(table);

    return table;
}

QVariant qGetValue(MdbSQL *sql, MdbColumn *col, uint colNum) {
    if (!col) {
        return QString::fromUtf8(static_cast<char *>(sql->bound_values[colNum]));;
    }
    // bool cannot be null
    if (col->col_type == MDB_BOOL) {
        return (col->cur_value_len ? false : true);
    }
    // null value
    if (col->cur_value_len == 0) {
        return QVariant();
    }
    // not null value
    switch (col->col_type) {
    case MDB_BYTE:
        return mdb_get_byte(sql->mdb->pg_buf, col->cur_value_start);
    case MDB_INT:
        return mdb_get_int16(sql->mdb->pg_buf, col->cur_value_start);
    case MDB_LONGINT:
        return (qint32)mdb_get_int32(sql->mdb->pg_buf, col->cur_value_start);
    case MDB_FLOAT:
        return mdb_get_single(sql->mdb->pg_buf, col->cur_value_start);
    case MDB_DOUBLE:
        return mdb_get_double(sql->mdb->pg_buf, col->cur_value_start);
    case MDB_DATETIME:
        {
            struct tm tmp_t = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            mdb_date_to_tm(mdb_get_double(sql->mdb->pg_buf, col->cur_value_start), &tmp_t);
            const char *format = mdb_col_get_prop(col, "Format");
            if (format && !strcmp(format, "Short Date")) {
                return QDate(tmp_t.tm_year + 1900, tmp_t.tm_mon + 1, tmp_t.tm_mday);
            } else {
                QDate date(tmp_t.tm_year + 1900, tmp_t.tm_mon + 1, tmp_t.tm_mday);
                QTime time(tmp_t.tm_hour, tmp_t.tm_min, tmp_t.tm_sec);
                return QDateTime(date, time);
            }
        }
        break;
    case MDB_OLE:
        if (mdb_get_int32(col->bind_ptr, 0)) {
            size_t size = 0;
            auto val = mdb_ole_read_full(sql->mdb, col, &size);
            auto rawData = QByteArray::fromRawData(static_cast<char *>(val), size);
            auto result = QString::fromUtf8(rawData);
            g_free(val);
            return result;
        }
        break;
    default:
        return QString::fromUtf8(static_cast<char *>(sql->bound_values[colNum]));
    }
    return QVariant();
}

bool runQuery(MdbSQL *sql, const QString &query) {
    mdb_sql_run_query(sql, const_cast<char *>(qUtf8Printable(query)));

    if (mdb_sql_has_error(sql)) {
        qDebug() << "Error:" << sql->error_msg;
        mdb_sql_reset(sql);
        return false;
    }


    QList<MdbColumn*> cols;
    for (uint i=0; i < sql->num_columns; ++i) {
        MdbSQLColumn *sqlCol = static_cast<MdbSQLColumn *>(g_ptr_array_index(sql->columns, i));
        MdbColumn *col = Q_NULLPTR;                
        auto table = sql->cur_table;
        for (uint j=0; j < table->num_cols; ++j) {
            MdbColumn *tblCol = static_cast<MdbColumn*>(g_ptr_array_index(table->columns, j));
            if (!g_ascii_strcasecmp(sqlCol->name, tblCol->name)) {
                col = tblCol;
                break;
            }
        }
        cols << col;
    }

    QStringList columns;
    for (int i=0; i < cols.size(); ++i) {
        MdbColumn *col = cols.at(i);
        columns << (col ? QString::fromUtf8(col->name) : QString("unknown %1").arg(i));
    }
    qDebug() << "Query:" << query;
    qDebug() << columns;

    while(mdb_fetch_row(sql->cur_table)) {
        QVariantList values;
        for (uint i=0; i < sql->num_columns; ++i) {
            MdbColumn *col = cols.at(i);
            values << qGetValue(sql, col, i);
        }
        qDebug() << values;
    }

    mdb_sql_reset(sql);
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    MdbSQL *access = mdb_sql_init();

//    const QString mdbFile = "Books_be.mdb";
    const QString mdbFile = "SpectraDB.mdb";
    auto fileName = qPrintable(mdbFile);

    MdbHandle *handle = mdb_sql_open(access, const_cast<char*>(fileName));

    if (!handle) {
        qDebug() << "Error opening database. MdbHandle is empty";
        qDebug() << QString::fromLocal8Bit(access->error_msg);
        return 1;
    }

    if (mdb_sql_has_error(access)) {
        qDebug() << "Error opening database. MdbSql error occured";
        qDebug() << QString::fromLocal8Bit(access->error_msg);
        return 2;
    }

    /* read the catalog */
    if (!mdb_read_catalog (access->mdb, MDB_ANY)) {
        qDebug() << "File does not appear to be an Access database";
        return 3;
    }

    const auto tables    = objects(access->mdb, QSql::Tables);
    const auto sysTables = objects(access->mdb, QSql::SystemTables);
    const auto views     = objects(access->mdb, QSql::Views);

    for (const auto &tbl : tables) {
        auto table = tableDef(handle, tbl);
        if (table) {
            qDebug()  << Qt::endl << "table" << tbl << "has" << table->num_cols << "columns:";
            for (uint i = 0; i < table->num_cols; i++) {
                 MdbColumn *col = static_cast<MdbColumn *>(g_ptr_array_index(table->columns, i));
                 qDebug() << col->name << QString("%1 (%2)").arg(typeName(col->col_type)).arg(col->col_size);
            }
            mdb_free_tabledef(table);
        }
    }

    qDebug() << "sysTables" << Qt::endl << sysTables << Qt::endl << Qt::endl;
    qDebug() << "views"     << Qt::endl << views     << Qt::endl << Qt::endl;

//    runQuery(access, "select * from Authors");
//    runQuery(access, "select * from Hooks");
//    runQuery(access, "select * from Books");
    runQuery(access, "select * from Spectra");
//    runQuery(access, "select SavedProtocol, SpectrumID from Spectra");
//    runQuery(access, "select Header, SpectrumID from Spectra");

    mdb_sql_close(access);
    if (mdb_sql_has_error(access)) {
        qDebug() << "Error closing database. MdbSql error occured";
        qDebug() << QString::fromLocal8Bit(access->error_msg);
        return 4;
    }

    mdb_sql_exit(access);

    return 0;
}
