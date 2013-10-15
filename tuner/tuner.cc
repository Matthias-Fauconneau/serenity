#include "tuner.h"
#include "thread.h"
#include "audio.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include <fftw3.h> //fftw3f
#define CAPTURE 0 //DEBUG
#if !CAPTURE
#include "sequencer.h"
#include "sampler.h"
#include "time.h"
#include "keyboard.h"
#include "text.h"
#endif

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
    for(uint i: range(N-size)) signal[i] = signal[i+size]; // Shifts buffer (FIXME: ring buffer)
    for(uint i: range(size)) signal[N-size+i] = float(data[i*2]+data[i*2+1])/(2<<30); // Appends new data
    return write(size);
}
uint Spectrum::write(int32* data, uint size) {
    for(uint i: range(N-size)) signal[i] = signal[i+size]; // Shifts buffer (FIXME: ring buffer)
    for(uint i: range(size)) signal[N-size+i] = float(data[i*2]+data[i*2+1])/(2<<30); // Appends new data
    return write(size);
}
uint Spectrum::write(uint size) {
    if(dts>pts+1) log("Skipped frames", (int)(dts-pts), dts, pts);
    for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
    fftwf_execute(fftw); // Transforms
    for(int i: range(N/2)) { // Converts to "power" spectrum
        float energy = sq(transform[i]) + sq(transform[N-1-i]);
        spectrum[i] = energy / N;
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
    float instantMaximumBandPower=0;
    int dx = round(size.x/88.f);
    int margin = (size.x-88*dx)/2;
    for(uint k: range(88)) { // Integrate bands over single keys
        int x0 = k*dx+margin; // Key start
        if(x0+dx<0 || x0>size.x) continue;
        const float offset = 0; //0.5; // FIXME
        uint n0 = floor(pitch(21+k-0.5+offset)/sampleRate*N);
        uint n1 = ceil(pitch(21+k+0.5+offset)/sampleRate*N);
        assert(n1>n0);
        float sum=0;
        for(uint n=n0; n<n1; n++) sum += spectrum[n]; //FIXME: Weight n0 and n1 with overlap
        sum /= (n1-n0);
        if(sum > instantMaximumBandPower) {
            instantMaximumBandPower = sum;
            maximumFrequency = pitch(21+k);
        }
        float h = sum*(size.y-1)/maximumBandPower;
        int y0 = min(int(h), size.y-1); // Clips as maximum is smoothed
        for(int y: range(size.y-1-y0+1,size.y)) for(int x: range(max(0,x0),min(x0+dx,size.x))) framebuffer(x, y) = 0xFF;
        if(h<size.y-1) for(int x: range(max(0,x0),min(x0+dx,size.x))) framebuffer(x, size.y-1-y0) = sRGB(h-y0);
    }
    const float alpha = 1./(sampleRate/periodSize); maximumBandPower = (1-alpha)*maximumBandPower + alpha*instantMaximumBandPower;
    for(uint k: range(1,88)) {
        int x = k*dx+margin; // Key start
        if(x>=0 && x<size.x) for(uint y: range(size.y)) framebuffer(x, y).g = 0xFF;
    }
    pts++;
}

struct Tuner : Widget {
    const uint sampleRate = 48000;
#if CAPTURE
    const uint periodSize = 1024; // Offset between each STFT [48fps]
    AudioInput input{{&spectrum,&Spectrum::write}, sampleRate, periodSize}; //CA0110 driver doesn't work
#else
    Thread thread{-20};
    Sequencer input{thread};
    Sampler sampler;
    const uint periodSize = Sampler::periodSize; // Offset between each STFT [24fps]
    Timer timer;
    AudioOutput audio{{this, &Tuner::read}, sampleRate, Sampler::periodSize, thread};
#endif
    Spectrum spectrum {sampleRate, periodSize};
    Keyboard keyboard;
    VBox layout{{ &spectrum, &keyboard, this }};
    Window window {&layout, int2(1024,600),"Tuner"_};
    Tuner() {
        spectrum.contentChanged.connect(&window,&Window::render);
        window.localShortcut(Escape).connect([]{exit();});
        window.backgroundCenter=window.backgroundColor=0; //window.clearBackground = false;
        window.show();
#if CAPTURE
        input.start();
#else
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));
        sampler.enableReverb = false;
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        thread.spawn();
        audio.start();
#endif
    }
#if !CAPTURE
    uint read(int32* output, uint size) {
        uint read = sampler.read(output, size);
        spectrum.write(output, read);
        return read;
    }
#endif
    void render(int2 position, int2 size) {
        //TODO: display fundamental offset to reference
        if(spectrum.maximumFrequency)
            Text(str(spectrum.maximumFrequency, sampleRate/spectrum.maximumFrequency),32,1).render(position, size);
    }
} application;
