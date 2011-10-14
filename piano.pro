CONFIG -= qt
QMAKE_CC = gcc-4.7.0-alpha20111008
QMAKE_CXX = $$QMAKE_CC
QMAKE_CFLAGS += -Wno-sign-compare
QMAKE_CXXFLAGS += -std=c++0x -Wextra
SOURCES = piano.cpp resample.c
HEADERS = common.h
LIBS = -lasound
OBJECTS_DIR=build
MOC_DIR=build
DESTDIR=build
