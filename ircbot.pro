TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp \
        tcpsyncclient.cpp

HEADERS += \
        tcpsyncclient.h

LIBS += \
        -lboost_system-mt \
        -lws2_32 \
        -lpthread

