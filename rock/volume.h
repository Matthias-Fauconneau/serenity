#pragma once
#include "image.h"

static_assert(sizeof(size_t)==8,"");

generic struct VolumeT;
typedef VolumeT<uint8> Volume8;
typedef VolumeT<uint16> Volume16;
generic struct bgr {
    T b,g,r;
    operator byte4() const { return byte4(b,g,r,0xFF); }
} packed;
typedef vec<bgr,uint8,3> byte3;
typedef VolumeT<byte3> Volume24;
typedef VolumeT<uint32> Volume32;
typedef VolumeT<float> VolumeFloat;
typedef VolumeT<short2> Volume2x16;
typedef VolumeT<short3> Volume3x16;
typedef VolumeT<uint64> Volume64;

struct Volume {
    Volume(){}

    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    bool tiled() const { if(offsetX || offsetY || offsetZ) { assert(offsetX && offsetY && offsetZ); return true; } else return false; }
    void copyMetadata(const Volume& source) {
        margin=source.margin; maximum=source.maximum; squared=source.squared; floatingPoint=source.floatingPoint; field=copy(source.field); origin=source.origin;
    }

    bool contains(int3 position) const { return position >= margin && position<sampleCount-margin; }
    size_t index(uint x, uint y, uint z) const {
        assert_(x<uint(sampleCount.x) && y<uint(sampleCount.y) && z<uint(sampleCount.z));
        return tiled() ? offsetX[x]+offsetY[y]+offsetZ[z] : uint64(z)*uint64(sampleCount.x*sampleCount.y)+y*sampleCount.x+x;
    }
    size_t index(int3 position) const { return index(position.x, position.y, position.z); }

    generic operator const VolumeT<T>&() const { assert_(sampleSize==sizeof(T),sampleSize,sizeof(T)); return *(const VolumeT<T>*)this; }
    generic operator VolumeT<T>&() { assert_(sampleSize==sizeof(T),sampleSize,sizeof(T)); return *(VolumeT<T>*)this; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    buffer<uint64> offsetX, offsetY, offsetZ; // Offset lookup tables for bricked volumes
    int3 sampleCount = 0; // Sample counts (along each dimensions)
    int3 margin=0; // Margins to ignore when processing volume (for each dimensions)
    uint maximum=0; // Maximum value (to compute normalized values)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool squared=false; // Whether the sample are a squared magnitude
    bool floatingPoint=false;  // Whether the sample are stored as floats
    String field; // Symbol of the sampled field
    int3 origin=0; // Coordinates of the origin in some reference coordinates
    bool cylinder=false; // Whether only data within the inscribed cylinder is valid
};

generic struct VolumeT : Volume {
    operator const ref<T>() const { assert(data.size==sizeof(T)*size()); return ref<T>((const T*)data.data, data.size/sizeof(T)); }
    operator const mref<T>() { assert(data.size==sizeof(T)*size()); return mref<T>((T*)data.data, data.size/sizeof(T)); }
    operator const T*() const { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (const T*)data.data; }
    operator T*() { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (T*)data.data; }
    mref<T> slice(size_t pos, size_t size) { return operator const mref<T>().slice(pos, size); }
    template<Type... Args> void clear(Args... args) { operator const mref<T>().clear(args...); }
    T operator()(uint x, uint y, uint z) const { return ((const T*)data.data)[index(x,y,z)]; }
    T& operator()(uint x, uint y, uint z) { return ((T*)data.data)[index(x,y,z)]; }
};

/// Serializes volume format (size, margin, range, layout)
String volumeFormat(const Volume& volume);
inline String str(const Volume& volume) { return volumeFormat(volume); }
generic String str(const VolumeT<T>& volume) { return volumeFormat(volume); }
/// Parses volume format (i.e sample format)
bool parseVolumeFormat(Volume& volume, const string& format);

/// Returns minimum of data
uint minimum(const Volume16& source);
/// Returns maximum of data
uint maximum(const Volume16& source);

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);
/// Interleaves 3 coordinates
uint64 zOrder(int3 coordinates);
/// Uninterleaves 3 coordinates
int3 zOrder(uint64 index);

/// Returns an 8bit image of a volume slice (for visualization)
Image slice(const Volume& volume, real z, bool cylinder, bool normalize, bool gamma, bool invert=false, bool binary=false);
Image slice(const Volume& volume, int z, bool cylinder, bool normalize, bool gamma, bool invert=false, bool binary=false);

/// Returns a 16bit image of a volume slice (for interoperation)
Image16 slice(const Volume& volume, int z);