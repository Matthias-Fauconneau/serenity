#include "thread.h"

#if AUDIO
#include "audio.h"
struct AudioTest {
    const uint rate = 48000;
    AudioOutput audio{{this,&AudioTest::read32}, rate, 8192};
    AudioTest() {
        log(__TIME__);
        audio.start();
    }
    uint t=0;
    uint read32(const mref<int2>& output) {
        for(uint i: range(output.size)) {
            output[i] = sin(2*PI*t*500/rate)*0x1p24;
            t++;
        }
        return output.size;
    }
} test;
#endif

#if FB
#include <linux/fb.h>
#include "image.h"
struct DisplayTest {
    DisplayTest() {
        Device fb("/dev/fb0"_);
        fb_var_screeninfo var; fb.ioctl(FBIOGET_VSCREENINFO, &var);
        fb_fix_screeninfo fix; fb.ioctl(FBIOGET_FSCREENINFO, &fix);
        log(var.xres_virtual, var.yres_virtual, fix.line_length, var.bits_per_pixel);
        Map buffer(fb.fd, 0, var.yres_virtual * fix.line_length, Map::Prot(Map::Read|Map::Write));
        ((mref<byte>)buffer).clear(0);
    }
};
#endif

#if 1
#include "window.h"
#include "text.h"
struct WindowTest {
    Text text {"Hello World !"_};
    Window window {&text, int2(-1), "Test"_};
    WindowTest() { window.oxygenBackground=false; window.show(); }
} test;
#endif
