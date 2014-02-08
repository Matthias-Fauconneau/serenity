#include "thread.h"
#include "audio.h"

struct Test {
    Test() {
        log("Hello World!"_);
        const uint rate = 48000;
        AudioOutput audio{{this,&Test::read16}, {this,&Test::read}, rate, 8192};
    }
    uint read(const mref<int2>& output) {
        log("read", output.data, output.size);
        return 0;
    }
    uint read16(const mref<short2>& output) {
        log("read2", output.data, output.size);
        return 0;
    }
} test;
