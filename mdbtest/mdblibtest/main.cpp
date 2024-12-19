#include <QCoreApplication>

#include <mdbsql.h>

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

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    MdbSQL *access = mdb_sql_init();

    const QString mdbFile = "Books_be.mdb";
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
            qDebug() << "table" << tbl << "has" << table->num_cols << "columns:";
            for (uint i = 0; i < table->num_cols; i++) {
                 MdbColumn *col = static_cast<MdbColumn *>(g_ptr_array_index(table->columns, i));
                 qDebug() << col->name << QString("%1 (%2)").arg(typeName(col->col_type)).arg(col->col_size);
            }
            mdb_free_tabledef(table);
        }
    }

    qDebug() << "sysTables" << Qt::endl << sysTables << Qt::endl;
    qDebug() << "views"     << Qt::endl << views     << Qt::endl;

    mdb_sql_close(access);
    if (mdb_sql_has_error(access)) {
        qDebug() << "Error closing database. MdbSql error occured";
        qDebug() << QString::fromLocal8Bit(access->error_msg);
        return 4;
    }

    mdb_sql_exit(access);

    return 0;
}
