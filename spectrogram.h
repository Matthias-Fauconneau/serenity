#include "widget.h"
#include "interface.h"
#include "process.h"

/// Computes pitch in Hz from MIDI \a key
inline float exp2(float x) { return __builtin_exp2f(x); }
inline float pitch(float key) { return 440*exp2((key-69)/12.0); }

struct Spectrogram : ImageView {
    uint N; // Discrete fourier transform size
    uint rate; // signal rate (to scale frequencies)
    uint bitDepth; // signal bit depth (to scale to color intensity)

    float* buffer; // Buffer of the last N samples (for overlap save)
    float* hann; // Window to apply to buffer at each update
    float* windowed; // Windowed buffer
    float* transform; // Fourier transform of the windowed buffer
    float* spectrum; // Magnitude of the complex Fourier coefficients
    struct fftwf_plan_s* plan;

    static constexpr uint F = 1056; // Size of the frequency plot in pixels (88x12)
    static constexpr uint T = 768; // Number of updates in one image (one pixel per update)

    uint t = 0;
    Lock imageLock;

    /// Initializes a running spectrogram with \a N bins
    Spectrogram(uint N, uint rate=44100, uint bitDepth=16);
    ~Spectrogram();

    /// Appends \a size frames (windowed overlap-save over N frames)
    void write(int16* data, uint size);
    /// Appends \a size frames (windowed overlap-save over N frames)
    void write(int32* data, uint size);
    /// Appends \a size frames (windowed overlap-save over N frames)
    void write(float* data, uint size);
    /// Updates spectrogram using written data
    void update();
    /// Renders spectrogram image
    void render(int2 position, int2 size) override;
};
