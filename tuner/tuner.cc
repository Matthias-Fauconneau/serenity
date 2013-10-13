#include "tuner.h"
#include "thread.h"
#include "audio.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include <fftw3.h> //fftw3f
#include "ffmpeg.h"
#include "time.h"

FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }

Spectrum::Spectrum(uint sampleRate, uint periodSize) : sampleRate(sampleRate), periodSize(periodSize) {
    signal = buffer<float>(N);
    hann = buffer<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = buffer<float>(N);
    transform = buffer<float>(N);
    spectrum = buffer<float>(N/2);
    fftw = fftwf_plan_r2r_1d(N, windowed.begin(), transform.begin(), FFTW_R2HC, FFTW_ESTIMATE);
}

uint Spectrum::write(int16* data, uint size) {
    if(dts>pts+1) log("Skipped frames", (int)(dts-pts), dts, pts);
    for(uint i: range(N-size)) signal[i] = signal[i+size]; // Shifts buffer (FIXME: ring buffer)
    for(uint i: range(size)) signal[N-size+i] = ((int)data[i*2]+data[i*2+1])/2; // Appends new data
    for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
    fftwf_execute(fftw); // Transforms
    for(int i: range(N/2)) { // Converts to "power" spectrum
        float energy = sq(transform[i]) + sq(transform[N-1-i]);
        spectrum[i] = energy;
    }
    dts++;
    contentChanged();
    return size;
}

// TODO: Railsback law
inline float pitch(float key) { return 440*exp2((key-69)/12); }
inline float key(float pitch) { return 69+log2(pitch/440)*12; }

void Spectrum::render(int2, int2 size) {
    if(!dts) return;
    float max=0;
    int shift = 88*12-size.x;
    /*for(uint x: range(size.x)) { // Integrate bands over single pixels
        int f = shift + x; // Align right
        if(f<0) continue; // size.x > 88·12px
        uint n0 = floor(pitch(21+(f-6)/12.0)/sampleRate*N);
        uint n1 = ceil(pitch(21+(f-6+1)/12.0)/sampleRate*N);
        if(n1==n0) continue;
        assert(n1>n0);
        float sum=0;
        for(uint n=n0; n<n1; n++) { //FIXME: Weight n0 and n1 with overlap
            float a = spectrum[n]; // Squared amplitude
            sum += a;
        }
        sum /= n1-n0;
        max = ::max(max, sum);
        float h = sum*(size.y-1)/maximumBandPower;
        int y0 = min(int(h), size.y-1); // Clips as spectrum is normalized for unit area (not top peak)
        for(int y: range(size.y-1-y0+1,size.y)) framebuffer(x, y) = 0xFF; //byte4(0,0,0,0xFF);
        if(h<size.y-1) framebuffer(x, size.y-1-y0) = sRGB[clip(0,(int)round(((h-y0))*0xFF),0xFF)];
    }*/
    for(uint k: range(88)) { // Integrate bands over single keys
        int x0 = k*12-6-shift; // Key start
        if(x0+12<0 || x0>size.x) continue;
        const float offset = 0.5; // FIXME
        uint n0 = floor(pitch(21+k-0.5+offset)/sampleRate*N);
        uint n1 = ceil(pitch(21+k+0.5+offset)/sampleRate*N);
        assert(n1>n0);
        float sum=0;
        for(uint n=n0; n<n1; n++) sum += spectrum[n]; //FIXME: Weight n0 and n1 with overlap
        sum /= (n1-n0);
        max = ::max(max, sum);
        float h = sum*(size.y-1)/maximumBandPower;
        int y0 = min(int(h), size.y-1); // Clips as maximum is smoothed
        for(int y: range(size.y-1-y0+1,size.y)) {
            for(int x: range(x0,x0+12)) {
                if(x<0 || x>=size.x) continue;
                framebuffer(x, y) = 0xFF;
            }
        }
        if(h<size.y-1) {
            int y = size.y-1-y0;
            int v = sRGB(h-y0);
            for(int x: range(x0,x0+12)) {
                if(x<0 || x>=size.x) continue;
                framebuffer(x, y) = v;
            }
        }
    }
    const float alpha = 1./(sampleRate/periodSize); maximumBandPower = (1-alpha)*maximumBandPower + alpha*max;
    for(uint k: range(1,88)) {
        int x = k*12-6-shift; // Key start
        if(x<0 || x>=size.x) continue;
        /*if(k%12==3||k%12==8) line; // B/C, E/F
        else if(x%12==1||x%12==4||x%12==6||x%12==9||x%12==11) fill; // black*/
        for(uint y: range(size.y)) framebuffer(x, y).g = 0xFF;
    }
    pts++;
}

struct Tuner : Widget {
    const uint sampleRate = 48000;
    const uint periodSize = 2048; // Offset between each STFT [24fps]
    Spectrum spectrum {sampleRate, periodSize};
#define CAPTURE 0 //DEBUG
#if CAPTURE
    AudioInput input{{&spectrum,&Spectrum::write}, sampleRate, periodSize}; //24fps (CA0110 driver doesn't work)
#else
    AudioFile file;
    Timer timer;
#endif
    //Keyboard keyboard;
    VBox layout{{ &spectrum /*, &keyboard, this*/ }};
    Window window {&layout, int2(1024,600),"Tuner"_};
    Tuner() {
        spectrum.contentChanged.connect(&window,&Window::render);
        window.localShortcut(Escape).connect([]{exit();});
        window.backgroundCenter=window.backgroundColor=0; //window.clearBackground = false;
        window.show();
#if CAPTURE
        input.start();
#else
        file.openPath("test.ogg"_); timer.timeout.connect(this,&Tuner::update); update();
#endif
    }
#if !CAPTURE
    void update() {
        int16 buffer[periodSize*2];
        uint read = file.read(buffer, periodSize);
        spectrum.write(buffer, read);
        timer.setRelative(periodSize*1000/sampleRate); //TODO: AudioInput Poll
    }
#endif
    void render(int2 position, int2 size) {
        //TODO: display fundamental offset to reference
    }
} application;
