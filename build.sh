#!/bin/sh
BIN=/var/tmp/build
test $BIN -nt build.cc -a $BIN -nt core/thread.cc || g++ -iquotecore -iquoteio -std=c++11 -DASSERT core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o /var/tmp/build
$BIN $TARGET $BUILD $*
