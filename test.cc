#include "zip.h"
#include "thread.h"
#include "math.h"

struct Test {
 Map zip {"Piano.zip"};
 Test() {
  log(sum(listZIPFile(zip).values)/1024/1024);
 }
} test;
