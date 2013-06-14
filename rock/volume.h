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
typedef VolumeT<float> VolumeFloat;
typedef VolumeT<double> VolumeDouble;

struct Volume {
    Volume(){}

    explicit operator bool() const { return (bool)data; }
    uint64 size() const { return (uint64)sampleCount.x*sampleCount.y*sampleCount.z; }
    bool tiled() const { if(offsetX || offsetY || offsetZ) { assert(offsetX && offsetY && offsetZ); return true; } else return false; }
    void copyMetadata(const Volume& source) { margin=source.margin; maximum=source.maximum; squared=source.squared; floatingPoint=source.floatingPoint; }

    bool contains(int3 position) const { return position >= margin && position<sampleCount-margin; }
    uint64 index(uint x, uint y, uint z) const {
        assert(x<uint(sampleCount.x) && y<uint(sampleCount.y) && y<uint(sampleCount.y));
        return tiled() ? (offsetX[x]+offsetY[y]+offsetZ[z]) : (z*sampleCount.x*sampleCount.y+y*sampleCount.x+x);
    }

    template<Type T> operator const VolumeT<T>&() const { assert_(sampleSize==sizeof(T),sampleSize); return *(const VolumeT<T>*)this; }
    template<Type T> operator VolumeT<T>&() { assert_(sampleSize==sizeof(T),sampleSize); return *(VolumeT<T>*)this; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    buffer<uint> offsetX, offsetY, offsetZ; // Offset lookup tables for bricked volumes
    int3 sampleCount = 0; // Sample counts (along each dimensions)
    int3 margin = 0; // Margins to ignore when processing volume (for each dimensions)
    uint maximum=0; // Maximum value (to compute normalized values)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool squared=false; // Whether the sample are a squared magnitude
    bool floatingPoint=false;  // Whether the sample are stored as floats
};

template<Type T> struct VolumeT : Volume {
    operator const T*() const { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (const T*)data.data; }
    operator T*() { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (T*)data.data; }
    T operator()(uint x, uint y, uint z) const { return ((const T*)data.data)[index(x,y,z)]; }
    T& operator()(uint x, uint y, uint z) { return ((T*)data.data)[index(x,y,z)]; }
};

/// Serializes volume format (size, margin, range, layout)
string volumeFormat(const Volume& volume);
inline string str(const Volume& volume) { return volumeFormat(volume); }
/// Parses volume format (i.e sample format)
bool parseVolumeFormat(Volume& volume, const ref<byte>& format);

/// Returns maximum of data
uint maximum(const Volume16& source);
uint maximum(const Volume32& source);

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);

/// Sets volume margins to x*x (used as EDT borders)
template<Type T> void setBorders(VolumeT<T>& target);

/// Returns an image of a volume slice
Image slice(const Volume& volume, float z, bool cylinder=false);
Image slice(const Volume& volume, int z, bool cylinder=false);

