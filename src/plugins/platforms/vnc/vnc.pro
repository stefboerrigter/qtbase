TARGET = qvnc

CONFIG += qpa/genericunixfontdatabase

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = QVNCIntegrationPlugin
load(qt_plugin)

QT += core-private gui-private platformsupport-private network

SOURCES =   main.cpp \
            qvncintegration.cpp \
            qvncscreen.cpp \
            qvncserver.cpp \
            qvnccursor.cpp \
            websocket.cpp \
            d3des.c
HEADERS =   qvncintegration.h \
            qvncscreen.h \
            qvncserver.h \
            qvnccursor.h \
            websocket.h \
            d3des.h

OTHER_FILES += vnc.json
