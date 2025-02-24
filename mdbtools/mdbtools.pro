QT  = core core-private sql-private

TEMPLATE = lib

CONFIG += c++11 plugin

target.path = $$[QT_INSTALL_PLUGINS]/sqldrivers/
INSTALLS += target

PLUGIN_CLASS_NAME = QMdbToolsDriverPlugin

PLUGIN_TYPE = sqldrivers
# load(qt_plugin)

DEFINES += QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_ASCII

# to find file glib.h
INCLUDEPATH += /usr/include/glib-2.0

# to find file glibconfig.h
INCLUDEPATH += /usr/lib/x86_64-linux-gnu/glib-2.0/include

HEADERS += \
    qsql_mdbtools.h

SOURCES += \
        main.cpp \
        qsql_mdbtools.cpp

OTHER_FILES += mdbtools.json

unix:!macx: LIBS += -lmdbsql -lmdb -lglib-2.0
