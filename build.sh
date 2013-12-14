#!/bin/sh
test /var/tmp/build.rock -nt build.cc || g++ -iquotecore -iquoteio -std=c++11 -DASSERT core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o /var/tmp/build.rock
/var/tmp/build.rock $TARGET $BUILD $*
