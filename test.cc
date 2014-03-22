#include "thread.h"
#include "audio.h"

struct Test {
    Test() {
        AudioControl volume;
        volume = volume - 1;
    }
} test;
