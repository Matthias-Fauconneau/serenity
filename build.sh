#!/bin/sh
D=/var/tmp/$(basename $(pwd))
mkdir -p $D
test -z "$CC" -a -e `which clang++` && CC=`which clang++`
test -z "$CC" -a -e `which g++-4.8` && CC=`which g++-4.8`
test -z "$CC" -a -e `which g++` && CC=`which g++`
test $D/build -nt build.cc -a $D/build -nt core/thread.cc -a $D/build -nt core/memory.h -a $D/build -nt core/file.cc -a $D/build -nt core/core.h  || \
$CC -std=c++11 -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $D/build && \
$D/build $TARGET $BUILD $*
