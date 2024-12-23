#include <QCoreApplication>

#include <QtSql>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QStringList tables;

    QSqlDatabase db = QSqlDatabase::addDatabase ("QMDBTOOLS");
    db.setDatabaseName("Books_be.mdb");
    if (db.open ()) {
        tables = db.tables();
        if( tables.isEmpty() ) {
            qDebug() << "No tables found!";
            return 2;
        } else {
            qDebug() << "Selected tables:" << tables;
        }
    } else {
        qDebug() << db.lastError ();
        return 1;
    }

    const auto sql = QString("select * from %1").arg(tables.first());
    QSqlQuery query;
    if (query.exec(sql)) {
        qDebug() << query.size() << "records" << (query.isSelect() ? "select" : "???");
        while (query.next()) {
            QStringList values;
            int colCount = query.record().count();
            for (int i=0; i<colCount; ++i) {
                values << query.value(i).toString();
            }
            qDebug() << values;
        }
    } else {
        qDebug() << query.lastError();
    }

    return 0;
}
