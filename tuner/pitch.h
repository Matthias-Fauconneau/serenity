#pragma once
#include "fft.h"
#include "list.h"
#include "array.h"
#include "math.h"
#include "string.h"
#include "data.h"

struct PitchEstimator : FFT {
    using FFT::FFT;
	// Parameters
    const uint fMin = 8, fMax = N/16; // 15 ~ 6000 Hz
    const uint iterationCount = 4; // Number of least square iterations
    const float initialInharmonicity = 0; //1./cb(24); // Initial inharmonicity
    static constexpr float noiseThreshold = 2;
    static constexpr float highPeakThreshold = 8;
    const uint medianError = 4;
    const uint maxHarmonicCount = 16;
    // Conditions for median F0 override
    const uint lastHarmonicRank = 39;
    const uint lastHarmonicFrequency = 868;
    const uint peakRank = 6;
    const uint peakFrequency = 113;
    const uint minNum = 26;
    const uint minDen = 79;
    // Conditions for F1 override
    const uint minHighPeakRank = 27;
    const uint minHighPeak = 506;
    const uint minHighPeakNum = 83;
    const uint minHighPeakDen = 637; // 640

    struct Peak {
        uint f;
        bool operator ==(const Peak& o) const { return f == o.f; }
    };
    uint minF, maxF;
    list<Peak, 16> peaks;

    float harmonicEnergy=0;
    float F0=0, B=0; // F0.n+F0.B.n^2 fit of all harmonics (1st harmonic is F0.(1+B))
    buffer<float> filteredSpectrum {fMax}; // Filtered power spectrum
    uint F1, nHigh, medianF0;

    struct Candidate {
        float f0; float B; float energy; array<uint> peaks;
        Candidate(float f0=0, float B=0, float energy=0, array<uint>&& peaks={}):
            f0(f0),B(B),energy(energy),peaks(move(peaks)){}
    };
	list<Candidate, 10> candidates;

    /// Returns first partial (f1=f0*sqrt(1+B)~f0*(1+B))
    /// \a fMin Minimum fundamental frequency for harmonic evaluation
    float estimate();
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline String strKey(int key) { return (string[]){"A","A#","B","C","C#","D","D#","E","F","F#","G","G#"}[(key+2*12+3)%12]+str(key/12-2); }
inline int parseKey(TextData& s) {
    int key=24;
	if(!"cdefgabCDEFGAB"_.contains(s.peek())) return -1;
	key += "c#d#ef#g#a#b"_.indexOf(lowerCase(s.next()));
    if(s.match('#')) key++;
    key += 12*s.mayInteger(4);
    return key;
}
inline int parseKey(const string name) { TextData s(name); return parseKey(s); }
