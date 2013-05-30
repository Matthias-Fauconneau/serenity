#include "capsule.h"
#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "math.h"

struct Capsule { vec3 a, b; float radius; };
template<> inline string str(const Capsule& o) { return str(o.a,o.b,o.radius); }

float sqrDistance(Capsule A, Capsule B) {
	vec3 u = A.b - A.a, v = B.b - B.a, w = A.a - B.a;
	float a = dot(u,u), b = dot(u,v), c = dot(v,v), d = dot(u,w), e = dot(v,w);
    float D = a*c - b*b;
	float sc, sN, sD = D; // sc = sN / sD, default sD = D >= 0
	float tc, tN, tD = D; // tc = tN / tD, default tD = D >= 0
	if(D < __FLT_EPSILON__) sN=0, sD=1, tN=e, tD=c; // the lines are almost parallel, force using point P0 on segment A to prevent possible division by 0.0 later
    else { // get the closest points on the infinite lines
		sN = (b*e - c*d), tN = (a*e - b*d);
		if(sN < 0) sN = 0, tN = e, tD = c;  // sc < 0 => the s=0 edge is visible
        else if (sN > sD) sN = sD, tN = e + b, tD = c; // sc > 1 => the s=1 edge is visible
    }
    if(tN < 0) { // tc < 0 => the t=0 edge is visible
        tN = 0;
        if(-d < 0) sN = 0; else if (-d > a) sN = sD; else sN = -d, sD = a; // recompute sc for this edge
    }
    else if (tN > tD) { // tc > 1 => the t=1 edge is visible
        tN = tD;
        if ((-d + b) < 0) sN = 0; else if ((-d + b) > a) sN = sD; else sN = (-d + b), sD = a; // recompute sc for this edge
    }
    sc = (abs(sN) < __FLT_EPSILON__ ? 0.0 : sN / sD), tc = (abs(tN) < __FLT_EPSILON__ ? 0.0 : tN / tD); // finally do the division to get sc and tc
    vec3 dP = w + (sc * u) - (tc * v);  // get the difference of the two closest points = A(sc) - B(tc)
    return sqr( dP ); // return the closest distance
}

/// Returns a list of random capsules inside a cube [0, size] separated from a given minimal distance and with a given maximal length and radius
array<Capsule> randomCapsules(vec3 size, float minimumDistance, float maximumLength, float maximumRadius) {
    maximumRadius = min(maximumRadius, min(size.x, min(size.y, size.z))/2-minimumDistance);
    Random random;
    array<Capsule> capsules;
    while(capsules.size<maximumRadius) {
        float radius = 1+random()*(maximumRadius-1);
        radius = round(radius);
        vec3 margin = minimumDistance+radius;
        vec3 ends[2]; for(vec3& end: ends) end = margin+vec3(random(),random(),random())*(size - 2.f*margin);
        float length = random()*maximumLength;
        vec3 origin=(ends[0]+ends[1])/2.f, axis=ends[1]-ends[0];
        float scale = length/norm(axis);
        if(scale>1) continue; // Might not fit cube
        Capsule a = {origin-scale/2*axis, origin+scale/2*axis, radius};
        if(norm(a.b-a.a)>maximumLength) continue;
        for(Capsule b: capsules) if(sqr(a.radius+minimumDistance+b.radius)>sqrDistance(a,b)) goto break_; // Discards candidates intersecting any other capsule
        /*else*/ capsules << a;
        break_: ;
    }
    return capsules;
}

/// Rasterizes capsules inside a volume (voxels outside any surface are assigned the maximum value, voxels inside any surface are assigned zero)
void rasterize(Volume16& target, const array<Capsule>& capsules) {
    const int X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    uint16* const targetData = target;
    clear(targetData, target.size(), (uint16)target.maximum); // Sets target to maximum value
    parallel(capsules.size, [&](uint, uint i) {
        Capsule p=capsules[i]; vec3 a=p.a, b=p.b;
        vec3 min = ::min(a,b)-vec3(p.radius), max = ::max(a,b)+vec3(p.radius); // Bounding box
        float length = norm(b-a), sqRadius=p.radius*p.radius;
        for(int z=floor(min.z); z<ceil(max.z); z++) {
            assert_(z>=0 && z<Z, z);
            uint16* const targetZ= targetData + z*XY;
            for(int y=floor(min.y); y<ceil(max.y); y++) {
                assert_(y>=0 && y<Y);
                uint16* const targetZY= targetZ + y*X;
                for(int x=floor(min.x); x<ceil(max.x); x++) {
                    assert_(z>=0 && z<X);
                    uint16* const targetZYX= targetZY + x;
                    vec3 P = vec3(x,y,z);
                    float l = dot(P-a, normalize(b-a));
                    if((0 < l && l < length && sqr(P-(a+l*normalize(b-a))) <= sqRadius) || sqr(P-a)<=sqRadius || sqr(P-b)<=sqRadius) targetZYX[0] = 0;
                }
            }
        }
    } );
}

class(Capsules, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return 2; }
    uint64 outputSize(const Dict&, const ref<shared<Result>>&, uint) override { return 512*512*512*outputSampleSize(0); }
    void execute(const Dict& args, array<Volume>& outputs, const ref<Volume>&) {
        Volume& target = outputs.first();
        target.sampleCount = 512;
        target.maximum = (1<<(target.sampleSize*8))-1;
        array<Capsule> capsules = randomCapsules(vec3(target.sampleCount), 1, 255, 255);
        rasterize(target, capsules);
        Sample analytic;
        for(Capsule p : capsules) {
            if(p.radius>=analytic.size) analytic.grow(p.radius+1);
            analytic[p.radius] += PI*p.radius*p.radius*(4./3*p.radius + norm(p.b-p.a));
        }
        writeFile(args.at("name"_)+".analytic.tsv"_, toASCII(analytic), args.at("resultFolder"_));
    }
};
