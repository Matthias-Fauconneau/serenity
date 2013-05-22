#pragma once
#include "image.h"

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

    uint64 size() const { return x*y*z; }
    explicit operator bool() const { return data; }
    operator const Volume8&() const { assert_(sampleSize==sizeof(uint8),sampleSize); return (Volume8&)*this; }
    operator const Volume16&() const { assert_(sampleSize==sizeof(uint16),sampleSize); return (Volume16&)*this; }
    operator const Volume24&() const { assert_(sampleSize==sizeof(bgr),sampleSize); return (Volume24&)*this; }
    operator const Volume32&() const { assert_(sampleSize==sizeof(uint32),sampleSize); return (Volume32&)*this; }
    operator const VolumeASCII&() const { assert_(sampleSize==sizeof(Line),sampleSize); return (VolumeASCII&)*this; }
    operator Volume8&() { assert_(sampleSize==sizeof(uint8),sampleSize); return *(Volume8*)this; }
    operator Volume16&() { assert_(sampleSize==sizeof(uint16),sampleSize); return *(Volume16*)this; }
    operator Volume24&() { assert_(sampleSize==sizeof(bgr),sampleSize); return *(Volume24*)this; }
    operator Volume32&() { assert_(sampleSize==sizeof(uint32),sampleSize); return *(Volume32*)this; }
    operator VolumeASCII&() { assert_(sampleSize==sizeof(Line),sampleSize); return *(VolumeASCII*)this; }
    void copyMetadata(const Volume& source) { marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ; maximum=source.maximum; squared=source.squared; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    buffer<uint> offsetX, offsetY, offsetZ; // Offset lookup tables for bricked volumes
    uint x=0, y=0, z=0; // Sample count in each dimensions
    uint marginX=0, marginY=0, marginZ=0; // Margins to trim when processing volume
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

/// Clears volume margins
template<Type T> void clearMargins(VolumeT<T>& target, uint value=0);

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
