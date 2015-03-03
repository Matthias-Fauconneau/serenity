#include "thread.h"
#include "math.h"
#include "pitch.h"
#include "audio.h"
#include "data.h"
#include "time.h"
#include "ui/layout.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "profile.h"

struct SpectrumPlot : Widget {
    static constexpr bool logx = false, logy = true; // Whether to use log scale on x/y axis
    const float resolution; // Resolution in bins / Hz
    static constexpr uint iMin = 0;
    const uint iMax; // Displayed range in bins
    // Additional analysis data
    const buffer<float> spectrum; // Displayed power spectrum
    const buffer<float> unfilteredSpectrum; // Displayed power spectrum
    const float periodPower;
    list<PitchEstimator::Peak, 16> peaks;
    list<PitchEstimator::Candidate, 2> candidates;
    const uint F1, nHigh;
    const uint expectedKey;
    const float expectedF;

    SpectrumPlot(float resolution, uint iMax, buffer<float>&& spectrum, buffer<float>&& unfilteredSpectrum, float periodPower,
         list<PitchEstimator::Peak, 16>&& peaks, list<PitchEstimator::Candidate, 2>&& candidates, uint F1, uint nHigh,
         uint expectedKey, float expectedF) :
        resolution(resolution), iMax(iMax), spectrum(move(spectrum)), unfilteredSpectrum(move(unfilteredSpectrum)), periodPower(periodPower),
        peaks(move(peaks)), candidates(move(candidates)), F1(F1), nHigh(nHigh), expectedKey(expectedKey), expectedF(expectedF) {}
    float x(float f) { return logx ? (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)) : (float)(f-iMin)/(iMax-iMin); }
    void render(int2 position, int2 size) {
        assert(iMin < iMax && iMax <= spectrum.size);
        if(iMin >= iMax) return;
        float sMax = -inf; for(uint i: range(iMin, iMax)) sMax = max(sMax, spectrum[i]);
        float sMin = periodPower;

        {float y = (logy ? (log2(PitchEstimator::noiseThreshold*sMin) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (2*sMin / sMax)) * size.y;
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5, green);}
        {float y = (logy ? (log2(PitchEstimator::highPeakThreshold*sMin) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (2*sMin / sMax)) * size.y;
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5, green);}

        for(uint i: range(iMin, iMax)) { // Unfiltered energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (unfilteredSpectrum[i] ? (log2(unfilteredSpectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) :
                              (unfilteredSpectrum[i] / sMax)) * size.y;
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y, white, 0.5);
        }

        for(uint i: range(iMin, iMax)) { // Energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (spectrum[i] ? (log2(spectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (spectrum[i] / sMax)) * size.y;
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y, white);
        }

        for(PitchEstimator::Peak peak: peaks) { // Peaks
            float f = peak.f;
            float x = this->x(f+0.5)*size.x;
            line(position.x+x,position.y,position.x+x,position.y+size.y, white, 1./4);
            Text("·",16,white).render(int2(position.x+x,position.y+16));
        }

        { // F1
            float f = F1;
            float x = this->x(f+0.5)*size.x;
            Text(str(nHigh),16,white).render(int2(position.x+x,position.y+16));
        }

        for(uint i: range(candidates.size)) {
            const auto& candidate = candidates[i];
            bool best = i==candidates.size-1;
            vec3 color(!best,best,0);

            Text label(dec(candidate.f0)+' '+dec(round(1000*12*2*log2(1+3*(candidate.B>-1?candidate.B:0))))+" m\t", 16,  color);
            label.render(int2(position.x+size.x-label.sizeHint().x,position.y+16+(i)*48+32));
            for(uint n: range(candidate.peaks.size)) {
                uint f = candidate.peaks[n];
                float x = this->x(f+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, color, 1./2);
                Text(dec(n+1),16,color).render(int2(position.x+x,position.y+16+(i)*48+(n%2)*16));
            }
        }

        for(uint i=1;;i++) {
            uint f = round(expectedF*i);
            if(f>=spectrum.size) break;
            float x = this->x(f+0.5)*size.x;
            float v = i==1 ? 1 : min(1., 2 * (spectrum[f] ? (log2(spectrum[f]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0));
            v = max(1.f/2, v);
            line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(0,0,1,v));
            Text(dec(i),16,white).render(int2(position.x+x,position.y));
        }
    }
};

float maxabs(const ref<float> v) { float y=0; for(float x: v) if(abs(x)>y) y=abs(x); return y; }
struct WaveformPlot : Widget {
    const buffer<float> signal;
    const float maximum;
    WaveformPlot(buffer<float>&& signal) : signal(move(signal)), maximum(maxabs(this->signal)) {}
    void render(int2 position, int2 size) {
        const uint x0 = position.x, y0 = position.y + size.y/2;
#if 0
        for(uint x: range(size.x)) {
            ref<float> chunk = signal(round(float(x)*signal.size/size.x),round(float(x+1)*signal.size/size.x));
            const uint dy = round(abs(mean(chunk))/maximum*size.y);
            fill(x0+x, y0-dy, x0+x+1, y0+dy, white);
        }
#else
        for(uint x: range(size.x-1)) {
            const float dy0 = mean(signal(round(float(x)*signal.size/size.x),round(float(x+1)*signal.size/size.x)))/maximum*size.y;
            const float dy1 = mean(signal(round(float(x+1)*signal.size/size.x),round(float(x+2)*signal.size/size.x)))/maximum*size.y;
            line(x0+x, y0-dy0, x0+x+1, y0-dy1, white);
        }
#endif
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation : Poll {
        // Input
        const uint lowKey=parseKey(arguments().value(0,"A0"))-12, highKey=parseKey(arguments().value(1,"A7"))-12;
        AudioFile audio {"/Samples/"+strKey(lowKey+12)+'-'+strKey(highKey+12)+".flac"};
        const uint rate = audio.rate;
        uint t=0;

        // Analysis
        static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
        const uint periodSize = 4096;
        buffer<float> signal {N};
        PitchEstimator estimator {N};
        int expectedKey = highKey+1;

        // Results
        uint success = 0, fail=0, tries = 0, total=0;

        PitchEstimation() { queue(); }
        void event() {
            // Prepares new period
            buffer<int2> period (periodSize);
            if(t>(highKey-lowKey)*2*rate || audio.read(period) < period.size) {
                log(fail, '/', tries, dec(round(100.*fail/tries))+'%');
                return;
            }
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) signal[N-periodSize+i] = period[i][0] * 0x1p-24; // Left channel only
            t+=periodSize;
            if(t<N) { queue(); return; } // Fills complete window
            for(uint i: range(N)) estimator.windowed[i] = estimator.window[i] * signal[i];
            float f = estimator.estimate();

            float confidenceThreshold = 1./10; //9-10 Relative harmonic energy (i.e over current period energy)
            float ambiguityThreshold = 1./21; // 1- Energy of second candidate relative to first
            float threshold = 1./24; // 19-24
            float offsetThreshold = 1./2;
            if(f < 13) { // Strict threshold for ambiguous bass notes
                threshold = 1./21;
                offsetThreshold = 0.43;
            }

            float confidence = estimator.harmonicEnergy  / estimator.periodEnergy;
            float ambiguity = estimator.candidates.size==2 && estimator.candidates[1].key
                    && estimator.candidates[0].f0*(1+estimator.candidates[0].B)!=f ?
                        estimator.candidates[0].key / estimator.candidates[1].key : 0;
            if(confidence > confidenceThreshold/2) {
                int key = round(pitchToKey(f*rate/N));
                if(key==expectedKey-1) expectedKey--; // Next key
                float keyF0 = keyToPitch(key)*N/rate;
                const float offsetF0 = f > 0 ? 12*log2(f/keyF0) : 0;
                const float expectedF = keyToPitch(expectedKey)*N/rate;
                const float f1 = f;
                const float f2 = estimator.F0*(2+estimator.B*cb(2));
                bool confident = confidence > confidenceThreshold && 1-ambiguity > ambiguityThreshold && confidence*(1-ambiguity) > threshold
                        && abs(offsetF0)<offsetThreshold;
                log(dec(t/rate,2,'0')+'.'+dec((t*10/rate)%10)+"\t"+strKey(expectedKey)+"\t"+strKey(key)+"\t"+dec(round(f*rate/N),4)+" Hz\t"
                    +dec(round(100*offsetF0),2) +" c\t"
                    +dec(round(confidence?1./confidence:0),2)+"\t"+dec(round(1-ambiguity?1./(1-ambiguity):0),2)+"\t"
                    +dec(round((confidence*(1-ambiguity))?1./(confidence*(1-ambiguity)):0),2)+"\t"
                    +dec(round(100*12*log2(f2/(2*f1))))+" c\t"
                    +dec(estimator.medianF0)+"\t"
                    +(expectedKey == key ? (confident ? 'O' : 'o') : (confident ? 'X' : 'x')));

                if(confident) {
                    if(expectedKey==key) success++;
                    else {
                        if(fail<1) {
                            SpectrumPlot* spectrum = new SpectrumPlot(
                                        (float)N/rate, min(uint(expectedF*16), estimator.fMax),
                                        copy(estimator.filteredSpectrum), copy(estimator.spectrum),
                                        estimator.periodPower, move(estimator.peaks), move(estimator.candidates),
                                        estimator.F1, estimator.nHigh,
                                        expectedKey, expectedF);
                            WaveformPlot* waveform = new WaveformPlot(copy(signal));
                            Widget* box = new VBox ({spectrum, waveform});
                            Window& window = *(new Window(box, int2(0, 1680/2), strKey(spectrum->expectedKey)));
                            window.backgroundColor=window.backgroundCenter=0; additiveBlend = true;
                            window.show();
                        }
                        fail++;
                    }
                    tries++;
                }
            }
            total++;
            queue();
        }
} app;
