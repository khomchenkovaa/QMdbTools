#include <QCoreApplication>

#include <QtSql>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QSqlDatabase db = QSqlDatabase::addDatabase ("QMDBTOOLS");
    db.setDatabaseName("Books_be.mdb");
    if (db.open ()) {
        auto tables = db.tables();
        if( tables.isEmpty() ) {
            qDebug() << "No tables found!";
        } else {
            qDebug() << "Selected tables:" << tables;
        }
    } else {
        qDebug() << db.lastError ();
        return 1;
    }

    return 0;
}
