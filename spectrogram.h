#include "widget.h"
#include "interface.h"
#include "process.h"

/// Computes pitch in Hz from MIDI \a key
inline float exp2(float x) { return __builtin_exp2f(x); }
inline float log2(float x) { return __builtin_log2f(x); }
inline float pitch(float key) { return 440*exp2((key-69)/12); }
inline float key(float pitch) { return 69+log2(pitch/440)*12; }

struct Spectrogram : Widget {
    ref<float> signal;
    uint duration; // in samples (signal.size/2)
    uint periodSize; // Offset between each STFT (overlap = transformSize-periodSize)
    uint periodCount; //duration/periodSize
    uint N; // Discrete fourier transform size
    uint sampleRate; // signal sampling rate (to scale frequencies)

    buffer<float> hann; // Hann window
    buffer<float> windowed; // Windowed frame of signal
    buffer<float> transform; // Fourier transform of the windowed buffer
    buffer<float> spectrum; // Magnitude of the complex Fourier coefficients
    struct fftwf_plan_s* plan;

    static constexpr uint F = 1056; // Size of the frequency plot in pixels (88x12)
    Image image; // Rendered image of the spectrogram
    buffer<bool> cache; // Flag whether the period was transformed and rendered (FIXME: bits)

    uint time=0; //in periods of periodSize

    /// Setups STFT on \a signal every \a T samples with \a N bins
    Spectrogram(const ref<float>& signal, uint sampleRate=44100, uint periodSize=1024, uint transformSize=16384);
    ~Spectrogram();

    /// Renders spectrogram image
    void render(int2 position, int2 size) override;
    /// User input
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
};
