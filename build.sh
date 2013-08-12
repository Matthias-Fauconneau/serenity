#!/bin/sh
test -e /dev/shm/build || g++ -iquotecore -iquoteio -std=c++11 core/thread.cc core/string.cc core/file.cc core/data.cc core/trace.cc build.cc -lpthread -o /dev/shm/build
/dev/shm/build $*
