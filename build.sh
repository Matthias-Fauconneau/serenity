#!/bin/sh
D=/var/tmp/$(basename $(pwd))
mkdir -p $D
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which clang++ 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++-4.8 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++ 2>/dev/null)"
test $D/build -nt build.cc -a $D/build -nt core/thread.cc -a $D/build -nt core/memory.h -a $D/build -nt core/file.cc -a $D/build -nt core/core.h  || \
$CC -std=c++11 -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $D/build && \
$D/build $TARGET $BUILD $*
#-march=native
