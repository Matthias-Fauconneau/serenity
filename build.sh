#!/bin/sh
BIN=/run/shm/build
test $BIN -nt build.cc -a $BIN -nt core/thread.cc -a $BIN -nt core/memory.h -a $BIN -nt core/file.cc || \
 g++-4.8 -g -march=native -iquotecore -iquoteio -std=c++0x -DASSERT core/memory.cc core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread \
 -o $BIN
$BIN $TARGET $BUILD $*
