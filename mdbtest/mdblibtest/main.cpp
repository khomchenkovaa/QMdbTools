#include <QCoreApplication>

#include <mdbsql.h>

#include <QtSql>
#include <QDebug>

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

    auto tables    = objects(access->mdb, QSql::Tables);
    auto sysTables = objects(access->mdb, QSql::SystemTables);
    auto views     = objects(access->mdb, QSql::Views);

    qDebug() << "tables"    << Qt::endl << tables    << Qt::endl;
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
