#if 0
#include "process.h"
struct LogTest {
    LogTest(){ log("Hello World"_); }
} test;
#endif

#if 1
#include "asound.h"
const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
struct SoundTest {
    AudioOutput audio __({this, &SoundTest::read}, 48000, 4096);
    SoundTest() { audio.start(); }
    float step=2*PI*440/48000;
    float amplitude=0x1p12;
    float angle=0;
    bool read(int16* output, uint periodSize) {
        for(uint i : range(periodSize)) {
            float sample = amplitude*sin(angle);
            output[2*i+0] = sample, output[2*i+1] = sample;
            angle += step;
        }
        return true;
    }
} test;
#endif

#if 0
#include "window.h"
#include "html.h"
struct HTMLTest {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"HTML"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(&exit);
        page.contentChanged.connect(&window, &Window::render);
        page.go(""_);
    }
} test;
#endif
