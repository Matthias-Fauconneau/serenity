#!/bin/sh
test -e /dev/shm/build || /ptmp/gcc-4.8.0/bin/g++ -std=c++11 core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o /dev/shm/build
/dev/shm/build $*
