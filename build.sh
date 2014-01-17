#!/bin/sh
BIN=/var/tmp/build
test $BIN -nt build.cc -a $BIN -nt core/thread.cc -a $BIN -nt core/memory.h -a $BIN -nt core/file.cc || \
g++ -std=c++11 -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $BIN || \
g++-4.8 -std=c++0x -march=native -iquotecore core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o $BIN
$BIN $TARGET $BUILD $*
