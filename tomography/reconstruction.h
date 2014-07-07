#pragma once
#include "project.h"
#include "random.h"

/// Base class for reconstruction
struct Reconstruction {
    Projection A; // Projection settings
    CLVolume x; // Current reconstruction estimate
    uint64 time = 0; // Cumulated OpenCL kernel time (when enabled)

    /// Initializes reconstruction for projection configuration \a A
    /// \note Estimate \a x is initialized with an uniform estimate \a value/√(x²+y²+z²) on the cylinder support
    Reconstruction(const Projection& A, string name, float value=0) : A(A), x(cylinder(VolumeF(A.volumeSize, 0, name), value/sqrt(float(sq(A.volumeSize.x)+sq(A.volumeSize.y)+sq(A.volumeSize.z))))) { assert_(x.size.x==x.size.y); }
    virtual ~Reconstruction() {}
    /// Executes one step of reconstruction
    /// \note That is one substep for subset reconstructions
    virtual void step() abstract;
};

/// Derived class for subset reconstruction
struct SubsetReconstruction : Reconstruction {
    const uint subsetSize; /// Number of projections per subsets
    const uint subsetCount; /// Total number of subsets
    /// Projection matrices and images for a subset
    struct Subset {
        CLBuffer<mat4> At; /// Array of matrix projecting voxel coordinates to image coordinates
        ImageArray b; /// Corresponding projection images
    };
    array<Subset> subsets;

    uint subsetIndex = 0; /// Next subset to be iterated
    buffer<uint> shuffle = shuffleSequence(subsetCount); /// Random

    SubsetReconstruction(const Projection& A, const ImageArray& b, const uint subsetSize, string name, float value=0) : Reconstruction(A, name, value), subsetSize(subsetSize), subsetCount(A.count/subsetSize) {
        assert_(subsetCount*subsetSize == A.count);
        subsets.reserve(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            subsets << Subset{ CLBuffer<mat4>(apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), "A"_), ImageArray(int3(b.size.xy(), subsetSize), 0, "b"_)};
            for(uint index: range(subsetSize)) copy(b, subsets[subsetIndex].b, int3(0,0,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)), int3(0,0,index), int3(b.size.xy(),1));
        }
    }
};

