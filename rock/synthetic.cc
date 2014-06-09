#include "volume-operation.h"
#include "time.h"
#include "matrix.h"

mat3 randomRotation(Random& random) {
    float a = 2*PI*random();
    mat3 R; R.rotateZ(a);
    float b = 2*PI*random(), c=random();
    vec3 v (cos(b)*sqrt(c), sin(b)*sqrt(c), sqrt(1-sqrt(c)));
    mat3 H( 2*v.x*v-vec3(1,0,0), 2*v.y*v-vec3(0,1,0), 2*v.z*v-vec3(0,0,1) );
    return H*R;
}

// Oriented bounding box intersection
bool overlaps(const mat4& a, const mat4& b) {
    vec3 min = inf, max = -inf;
    for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) {
        vec3 t = a.inverse()*b*vec3(i?-1:1,j?-1:1,k?-1:1); // Projects B corners to A
        min = ::min(min, t), max = ::max(max, t);
    }
    return min < vec3(1) && max > vec3(-1);
}
bool intersects(const mat4& a, const mat4& b) { return overlaps(a, b) && overlaps(b, a); }

struct Synthetic : VolumeOperation {
    const int3 size = 128;

    string parameters() const { return "cylinder"_; }
    uint outputSampleSize(uint index) override { if(index) return 0; /*Extra outputs*/ return sizeof(uint8); }
    size_t outputSize(const Dict&, const ref<const Result*>&, uint index) override {
        if(index) return 0; //Extra outputs
        return (uint64)size.x*size.y*size.z;
    }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>&, const ref<Result*>& otherOutputs) override {
        Volume8& target = outputs.first();
        assert_(target.data.data, target.data.data, target.data.size, target.data.capacity);
        target.sampleCount = size;
        target.field = String("μ"_);
        target.maximum = 0xFF;
        target.data.clear(target.maximum-1);
        /// Synthesizes random oriented ellipsoids
        const uint maximumRadius = (size.x-1)/4;
        array<mat4> ellipsoids; // Oriented bounding boxes (mat4 might not be most efficient but is probably the easiest representation)
        for(Random random; ellipsoids.size < (uint)size.x; ) {
            vec3 radius = vec3(1)+vec3(random(),random(),random())*float(maximumRadius-1);
            if(max(max(radius.x,radius.y),radius.z) > 4*min(min(radius.x,radius.y),radius.z)) continue; // Limits aspect ratio
            const vec3 margin = vec3(max(max(radius.x,radius.y),radius.z))+vec3(1);
            vec3 center = margin+vec3(random(),random(),random())*(vec3(size) - 2.f*margin);
            mat4 ellipsoid;
            ellipsoid.translate(center);
            ellipsoid.scale(radius);
            ellipsoid = ellipsoid * randomRotation(random);
            for(const mat4& o: ellipsoids) if(intersects(ellipsoid, o)) goto break_; // Prevents intersections
            /*else*/ {
                // Rasterizes ellipsoids
                // Computes world axis-aligned bounding box of object's oriented bounding box
                vec3 O = ellipsoid[3].xyz(), min = O, max = O; // Initialize min/max to origin
                for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) { // Bounds each corner
                    vec3 corner = ellipsoid*vec3(i?-1:1,j?-1:1,k?-1:1);
                    min=::min(min, corner), max=::max(max, corner);
                }
                min = ::max(min, vec3(0)), max = ::min(max, vec3(size));
                mat4 worldToBox = ellipsoid.inverse();
                for(int z: range(min.z, max.z+1)) for(int y: range(min.y, max.y+1)) for(int x: range(min.x, max.x+1)) {
                    vec3 p = worldToBox*vec3(x,y,z);
                    if(sq(p) < 1) target(x,y,z) = 1;
                }
                ellipsoids << ellipsoid;
            }
            break_:;
        }
        /// Links each ellipsoid to its nearest neighbour (TODO: all non-intersecting links)
        struct Tube { vec3 A, B; float radius; };
        array<Tube> tubes;
        for(const mat4& ellipsoid: ellipsoids) {
            vec3 center = ellipsoid[3].xyz();
            mat4 nearest; float distance=inf;
            for(const mat4& o: ellipsoids) if(o[3].xyz()!=center && sq(o[3].xyz()-center)<distance) distance=sq(o[3].xyz()-center), nearest = o;
            //vec4 a = ellipsoid*vec4(1,1,1,0); float aVolume = a.x*a.y*a.z;
            //vec4 b = nearest*vec4(1,1,1,0); float vVolume = b.x*b.y*b.z;
            // Rasterizes tubes (throats)
            vec3 A = center, B = nearest[3].xyz();
            float tubeRadius = 1; //max(1., pow(min(aVolume,vVolume), 1./3));
            int3 min=clip(int3(0),int3(::min(A,B))-int3(tubeRadius),target.sampleCount);
            int3 max=clip(int3(0),int3(ceil(::max(A,B)))+int3(tubeRadius),target.sampleCount);
            for(int z: range(min.z, max.z)) for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
                vec3 P = vec3(x,y,z);
                float t = dot(B-A, P-A)/sq(B-A);
                if(t<=0 || t>=1) continue;
                //if(sq(P - (A+t*(B-A))) < sq((1-2*t*(1-t))*tubeRadius)) target(x,y,z) = 0; // Tapered cylinder
                if(sq(P - (A+t*(B-A))) < sq(tubeRadius)) target(x,y,z) = 1; // Straight cylinder
            }
            tubes << Tube{A, B, tubeRadius};
        }
        output(otherOutputs, "voxelSize"_, "size"_, [&]{return str(size.x)+"x"_+str(size.y)+"x"_+str(size.z) + " voxels"_;});
    }
};
template struct Interface<Operation>::Factory<Synthetic>;
