#pragma once
#include "image.h"

/// Returns coordinates for the given Z-order curve index
int3 zOrder(uint index);
/// Returns Z-order curve index for the given coordinates
uint zOrder(int3 coordinates);

template<Type T> struct VolumeT;
typedef VolumeT<uint8> Volume8;
typedef VolumeT<uint16> Volume16;
struct bgr { uint8 b,g,r; operator byte4() const { return byte4(b,g,r,0xFF); } } packed;
typedef VolumeT<bgr> Volume24;
typedef VolumeT<uint32> Volume32;
typedef char Line[20];
typedef VolumeT<Line> VolumeASCII;

struct Volume {
    Volume(){}

    uint64 size() const { return sampleCount.x*sampleCount.y*sampleCount.z; }
    explicit operator bool() const { return (bool)data; }
    template<Type T> operator const VolumeT<T>&() const { assert_(sampleSize==sizeof(T),sampleSize); return *(const VolumeT<T>*)this; }
    template<Type T> operator VolumeT<T>&() { assert_(sampleSize==sizeof(T),sampleSize); return *(VolumeT<T>*)this; }
    void copyMetadata(const Volume& source) { margin=source.margin; maximum=source.maximum; squared=source.squared; }
    bool contains(int3 position) const { return position >= margin && position<sampleCount-margin; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    buffer<uint> offsetX, offsetY, offsetZ; // Offset lookup tables for bricked volumes
    int3 sampleCount = 0; // Sample counts (along each dimensions)
    int3 margin = 0; // Margins to ignore when processing volume (for each dimensions)
    uint maximum=0; // Maximum value (to compute normalized values)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool squared=false; // Whether the sample are a squared magnitude
};

template<Type T> struct VolumeT : Volume {
    operator const T*() const { return (T*)data.data; }
    operator T*() { return (T*)data.data; }
};

/// Serializes volume format (size, margin, range, layout)
string volumeFormat(const Volume& volume);
inline string str(const Volume& volume) { return volumeFormat(volume); }
/// Parses volume format (i.e sample format)
void parseVolumeFormat(Volume& volume, const ref<byte>& format);

/// Returns maximum of data
uint maximum(const Volume16& source);
uint maximum(const Volume32& source);

/// Converts a 32bit volume to 16bit
void pack(Volume16& target, const Volume32& source);

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);

/// Tiles a volume recursively into bricks (using 3D Z ordering)
void tile(Volume16& target, const Volume16& source);

/// Sets volume margins to x*x (used as EDT borders)
template<Type T> void setBorders(VolumeT<T>& target);

/// Copies a cropped version of the volume
void crop(Volume16& target, const Volume16& source, uint x1, uint y1, uint z1, uint x2, uint y2, uint z2);

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(Volume16& target, const Volume16& source);

/// Converts volume data to ASCII (one voxel per line, explicit coordinates)
void toASCII(Volume& target, const Volume& source);

/// Returns an image of a volume slice
Image slice(const Volume& volume, float z, bool cylinder=false);
Image slice(const Volume& volume, int z, bool cylinder=false);

/// Maps intensity to either red or green channel depending on binary classification
void colorize(Volume24& target, const Volume32& binary, const Volume16& intensity);
