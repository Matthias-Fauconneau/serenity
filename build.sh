#!/bin/sh
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which clang++ 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++-4.8 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++ 2>/dev/null)"
D=/var/tmp/$(basename $(pwd)).$(basename $CC)
mkdir -p $D
test $D/build -nt build.cc -a $D/build -nt core/thread.cc -a $D/build -nt core/thread.h -a $D/build -nt core/file.cc \
   -a $D/build -nt core/memory.h -a $D/build -nt core/core.h || \
$CC -std=c++11 -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $D/build && \
$D/build $TARGET $BUILD $*
