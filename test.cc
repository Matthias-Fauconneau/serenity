#include "thread.h"
#include "audio.h"
#include "window.h"
#include "text.h"

#if 0
#include <unistd.h>
#include <linux/input.h>
struct Test : PollDevice {
    Test():PollDevice("/dev/input/event0"_){registerPoll();}
    void event() override {
        for(input_event e; ::read(Device::fd, &e, sizeof(e)) > 0;) {
            log(e.type, e.code, e.value);
        }
    }
} test;
#else
struct Test : Text {
    Window window {this, int2(1050), "Test"_};

    bool mouseEvent(int2 cursor, int2, Event event, Button) {
        if(event==Press) setText(str(cursor));
        return true;
    }

    const uint rate = 44100;
    AudioOutput audio{{this,&Test::read32}, rate, 8192};

    uint t=0;

    uint read32(const mref<int2>& output) {
        for(uint i: range(output.size)) {
            output[i] = sin(2*PI*t*500/rate)*0x1p24;
            t++;
        }
        return output.size;
    }

    Test() : Text{"Hello World !"_} {
        log(__TIME__);
        window.show();
        audio.start();
    }
} test;
#endif
