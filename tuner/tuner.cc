#include "tuner.h"
#include "thread.h"
#include "ffmpeg.h"
#include <fftw3.h> //fftw3f
#include "time.h"

FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }

Spectrogram::Spectrogram() {
    hann = buffer<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = buffer<float>(N);
    transform = buffer<float>(N);
    spectrum = buffer<float>(N/2);
    fftw = fftwf_plan_r2r_1d(N, windowed.begin(), transform.begin(), FFTW_R2HC, FFTW_ESTIMATE);
}

void Spectrogram::write(float* data, uint size) {
    assert_(size==N); //FIXME: ==periodSize + overlap (+ring buffer)
    for(uint i: range(N)) windowed[i] = hann[i]*data[i]; // Multiplies window
    fftwf_execute(fftw); // Transforms
    totalEnergy=0;
    for(int i: range(N/2)) { // Converts to "power" spectrum
        float energy = sq(transform[i]) + sq(transform[N-1-i]);
        spectrum[i] = energy;
        totalEnergy += energy;
    }
    totalEnergy /= N/2;
}

void Spectrogram::render(int2 position, int2 size) {
    //TODO
}

struct Tuner {
    AudioFile file; //TODO: ALSA AudioInput
    Spectrogram spectrum;
    //Keyboard keyboard;
    //Display fundamental offset to reference;
    Timer timer;
    Tuner() {
        file.openPath("test.ogg"_);
    }
    void update() {
        float buffer[spectrum.periodSize];
        uint read = file.read(buffer, spectrum.periodSize);
        spectrum.write(buffer, read);
        timer.setRelative(spectrum.periodSize*1000/spectrum.sampleRate); //TODO: AudioInput Poll
    }
} application;
