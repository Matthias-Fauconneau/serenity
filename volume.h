#pragma once
#include "image.h"

struct Volume {
    Volume(){}
    Volume(uint sampleSize, uint x, uint y, uint z): data((uint64)sampleSize*x*y*z),x(x),y(y),z(z),sampleSize(sampleSize) {}

    uint64 size() const { return x*y*z; }
    operator bool() { return data; }
    operator const struct Volume16&() const { assert_(sampleSize==sizeof(uint16)); return (struct Volume16&)*this; }
    operator const struct Volume32&() const { assert_(sampleSize==sizeof(uint32)); return (struct Volume32&)*this; }
    operator struct Volume16&() { assert_(sampleSize==sizeof(uint16)); return *(struct Volume16*)this; }
    operator struct Volume32&() { assert_(sampleSize==sizeof(uint32)); return *(struct Volume32*)this; }
    void copyMetadata(const Volume& source) { num=source.num, den=source.den; marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    // Offset lookup tables for bricked volumes
    buffer<uint> offsetX;
    buffer<uint> offsetY;
    buffer<uint> offsetZ;
    uint x=0, y=0, z=0; // Sample count in each dimensions
    uint marginX=0, marginY=0, marginZ=0; // Margins to trim when processing volume
    uint num=1, den=1; // Scale to apply to compute normalized values (data*numerator/denominator)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool own=false;
};
inline string str(const Volume& volume) {
    return str(volume.x)+"x"_+str(volume.y)+"x"_+str(volume.z)+" "_
            +str(volume.marginX)+"+"_+str(volume.marginY)+"+"_+str(volume.marginZ)+" "_
            +hex(volume.num)+":"_+hex(volume.den)+" "_
            +str(volume.size()*volume.sampleSize/1024/1024)+"MB"_;
}

struct Volume16 : Volume {
    Volume16(uint x, uint y, uint z) : Volume(sizeof(uint16),x,y,z) {}
    operator const uint16*() const { return (uint16*)data.data; }
    operator uint16*() { return (uint16*)data.data; }
};
inline string str(const Volume16& volume) { return str((Volume&)volume); }

struct Volume32 : Volume {
    Volume32(uint x, uint y, uint z): Volume(sizeof(uint32),x,y,z) {}
    operator const uint32*() const { return (uint32*)data.data; }
    operator uint32*() { return (uint32*)data.data; }
};
inline string str(const Volume32& volume) { return str((Volume&)volume); }

/// Returns maximum of data
uint maximum(const Volume16& source);
uint maximum(const Volume32& source);

/// Converts a 32bit volume to 16bit
void pack(Volume16& target, const Volume32& source);

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);

/// Tiles a volume recursively into bricks (using 3D Z ordering)
void tile(Volume16& target, const Volume16& source);

/// Clips volume data to a cylinder and sets zero samples to 1
void clip(Volume16& target);

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(Volume16& target, const Volume16& source);

/// Returns an image of a volume slice
Image slice(const Volume& volume, uint z);

/// Returns the square root of an image of a volume slice
Image squareRoot(const Volume& volume, uint z);
