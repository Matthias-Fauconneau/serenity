#include "thread.h"

struct Test {
 Test() {
     float a = 1./3;
     log(bin(cast<uint8>(raw(a))));
     log(bin(cast<uint8>(raw(a+a))));
     log(bin(cast<uint8>(raw(a+a+a))));
 }
} test;
