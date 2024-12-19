#include <qsqldriverplugin.h>
#include <qstringlist.h>

#include "qsql_mdbtools.h"

QT_BEGIN_NAMESPACE

class QMdbToolsDriverPlugin : public QSqlDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QSqlDriverFactoryInterface" FILE "mdbtools.json")

public:
    QMdbToolsDriverPlugin();

    QSqlDriver* create(const QString &) override;
};

QMdbToolsDriverPlugin::QMdbToolsDriverPlugin()
    : QSqlDriverPlugin()
{
}

QSqlDriver* QMdbToolsDriverPlugin::create(const QString &name)
{
    if (name == QLatin1String("QMDBTOOLS")) {
        QMdbToolsDriver* driver = new QMdbToolsDriver();
        return driver;
    }
    return 0;
}

QT_END_NAMESPACE

#include "main.moc"
