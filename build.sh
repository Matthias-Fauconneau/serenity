#!/bin/sh
test -e /var/tmp/build || g++ -iquotecore -iquoteio -std=c++11 -DASSERT core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o /var/tmp/build
/var/tmp/build $TARGET $BUILD $*
