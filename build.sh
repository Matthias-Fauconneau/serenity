#!/bin/sh
test -z "$CC" -a -e /usr/bin/clang++ && CC=clang++
test -z "$CC" -a -e /usr/bin/g++4.8 && CC=g++4.8
test -z "$CC" -a -e /usr/bin/g++ && CC=g++
BIN=/var/tmp/build
test $BIN -nt build.cc -a $BIN -nt core/thread.cc -a $BIN -nt core/memory.h -a $BIN -nt core/file.cc -a $BIN -nt core/core.h  || \
$CC -std=c++11 -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $BIN && \
$BIN $TARGET $BUILD $*
