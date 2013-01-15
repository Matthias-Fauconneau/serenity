#include "widget.h"

struct Spectrogram : Widget {
    float* buffer;
    float* hann;
    float* windowed;
    float* spectrum;
    const uint T = 1050;
    uint N = audio.rate;
    const uint Y = 1050;
    uint t=0;
    float max=0x1p24f;
    Image spectrogram __(T,Y);

    Spectrogram();
    void viewFrame(float* data, uint size);
    int2 sizeHint() { return int2(T,Y); }
    void render(int2 position, int2) { blit(position,spectrogram); }
};
