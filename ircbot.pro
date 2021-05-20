TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp \
        tcpsyncclient.cpp

HEADERS += \
        tcpsyncclient.h

win32 {
LIBS += \
        -lboost_system-mt \
        -lboost_filesystem-mt \
        -lws2_32 \
        -lpthread
}
!win32 {
LIBS += \
        -lboost_system \
        -lboost_filesystem \
        -lpthread
}
