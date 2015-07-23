#!/bin/sh
set -e
cd "$(dirname "$0")"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which clang++ 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++-4.8 2>/dev/null)"
test -z "$CC" -a ! -e "$(which $CC 2>/dev/null)" && CC="$(which g++ 2>/dev/null)"
D=/var/tmp/$(basename $(pwd)).$(basename $CC)
mkdir -p $D
test $D/build -nt core/core.h   -a $D/build -nt core/memory.h -a $D/build -nt core/array.h      -a $D/build -nt core/math.h -a $D/build -nt core/data.h \
   -a $D/build -nt core/string.h -a $D/build -nt core/string.cc -a $D/build -nt core/data.cc -a $D/build -nt core/file.h -a $D/build -nt core/file.cc \
   -a $D/build -nt core/trace.h -a $D/build -nt core/trace.cc -a $D/build -nt core/thread.h -a $D/build -nt core/thread.cc -a $D/build -nt build.h -a $D/build -nt build.cc || \
$CC -std=c++11 -DNO_PTHREAD -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lstdc++ -lm -o $D/build
$D/build $TARGET $BUILD $*
