QT -= gui
QT += sql

CONFIG += c++11 console
CONFIG -= app_bundle

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# to find file glib.h
INCLUDEPATH += /usr/include/glib-2.0

# to find file glibconfig.h
INCLUDEPATH += /usr/lib/x86_64-linux-gnu/glib-2.0/include

SOURCES += \
        main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += Books_be.mdb

unix:!macx: LIBS += -lmdbsql -lmdb
