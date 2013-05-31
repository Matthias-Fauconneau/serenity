#!/bin/sh
test -e /dev/shm/build || /ptmp/gcc-4.8.0/bin/g++ -std=c++11 thread.cc string.cc file.cc data.cc trace.cc build.cc -lpthread -o /dev/shm/build
/dev/shm/build $*
