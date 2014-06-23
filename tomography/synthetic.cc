#include "random.h"
#include "file.h"
#include "view.h"
#include "text.h"
#include "layout.h"
#include "window.h"

VolumeF porousRock(int3 size) {
    assert(size.x == size.y);

    const float airDensity = 0.001; // Houndsfield ?
    VolumeF volume (size, airDensity);

    const vec2 center = vec2(size.xy()-int2(1))/2.f;
    const float volumeRadius = center.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const float outerRadius = (1-2./100) * volumeRadius;
    const float containerDensity = 5.6; // Pure iron
    for(uint y: range(size.y)) for(uint x: range(size.x)) {
        float r2 = sq(vec2(x,y)-vec2(size.xy()-1)/2.f);
        if(sq(innerRadius-1./2) < r2 && r2 < sq(outerRadius+1./2)) {
            float c = 0;
            if(r2 < sq(innerRadius+1./2)) {
                float r = sqrt(r2);
                c = r - (innerRadius-1./2);
            }
            else if(sq(outerRadius-1./2) < r2) {
                float r = sqrt(r2);
                c = (outerRadius+1./2) - r;
            }
            else c = 1;
            for(uint z: range(size.z)) volume(x,y,z) = (1-c) * volume(x,y,z) + c *  containerDensity;
        }
    }

    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    const float maximumRadius = 16; // vx
    const float rate = 1./cb(maximumRadius); // 1/vx
    const uint grainCount = rate*size.z*size.y*size.x;
    Random random; // Unseeded sequence (for repeatability)
    Time time;
    for(GrainType type: types) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        float* volumeData = volume.data;
        const uint XY = size.x*size.x;
        const uint X = size.x;
        for(uint count=0; count < type.probability * grainCount;) {
            const float r = random()*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
            const vec2 xy = vec2(random(), random()) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-r)) continue;
            count++;
            float cx = xy.x, cy=xy.y, cz=r+random()*((size.z-1)-2*r);
            float innerR2 = sq(r-1.f/2);
            float outerRadius = r+1.f/2;
            float outerR2 = sq(outerRadius);
            float v = type.density;
            // Rasterizes grains as spheres
            int iz = cz-r, iy = cy-r, ix = cx-r;
            float fz = cz-iz, fy=cy-iy, fx=cx-ix;
            uint grainSize = ceil(2*r);
            float* volume0 = volumeData + iz*XY + iy*X + ix;
            for(uint z=0; z<grainSize; z++) { // Grains may be cut by cylinder caps
                float* volumeZ = volume0 + z*XY;
                float rz = float(z) - fz;
                float RZ = rz*rz;
                for(uint y=0; y<grainSize; y++) {
                    float* volumeZY = volumeZ + y*X;
                    float ry = float(y) - fy;
                    float RZY = RZ + ry*ry;
                    for(uint x=0; x<grainSize; x++) {
                        float rx = float(x) - fx;
                        float r2 = RZY + rx*rx;
                        if(r2 <= innerR2) volumeZY[x] = v;
                        else if(r2 < outerR2) {
                            float c = outerRadius - sqrt(r2);
                            volumeZY[x] = (1-c) * volumeZY[x] + c * v; // Alpha blending
                        }
                    }
                }
            }
        }
    }
    log(time);
    return volume;
}

inline void intersects(const float halfHeight, const float radius, const float3 origin, const float3 ray, float& tmin, float& tmax) {
    float2 plusMinusHalfHeightMinusOriginZ = float2(1,-1) * (halfHeight-1.f/2/*fix OOB*/) - origin.z; // sq(origin.xy()) - sq(radius) + 1 /*fix OOB*/, sq(radius), center.
    float radiusSq = sq(radius);
    float c = sq(origin.xy()) - radiusSq + 1 /*fix OOB*/;
    // Intersects cap disks
    const float2 capT = plusMinusHalfHeightMinusOriginZ / ray.z; // top bottom
    const float4 capXY = origin.xyxy() + capT.xxyy() * ray.xyxy(); // topX topY bottomX bottomY
    const float4 capXY2 = capXY*capXY;
    const float2 capR2 = capXY2.xz() + capXY2.yw(); // topR² bottomR²
    // Intersect cylinder side
    const float a = sq(ray.xy());
    const float b = 2*dot(origin.xy(), ray.xy());
    const float sqrtDelta = sqrt(b*b - 4 * a * c);
    const float2 sideT = (-b + float2(sqrtDelta,-sqrtDelta)) / (2*a); // t±
    const float2 sideZ = abs(origin.z + sideT * ray.z); // |z±|
    tmin=inf, tmax=-inf;
    if(capR2[0] <= radiusSq) tmin=min(tmin, capT[0]), tmax=max(tmax, capT[0]); // top
    if(capR2[1] <= radiusSq) tmin=min(tmin, capT[1]), tmax=max(tmax, capT[1]); // bottom
    if(sideZ[0] <= halfHeight) tmin=min(tmin, sideT[0]), tmax=max(tmax, sideT[0]); // side+
    if(sideZ[1] <= halfHeight) tmin=min(tmin, sideT[1]), tmax=max(tmax, sideT[1]); // side-
}

inline bool intersects(const float radius, const float3 origin, const float3 ray, float& tmin, float& tmax) {
    float a = sq(ray);
    float b = 2 * dot(ray, origin);
    float c = sq(origin) - sq(radius);
    float d2 = sq(b) - 4 * a * c;
    if(d2 < 0) return false;
    float d = sqrt(d2);
    float q = b < 0 ? (-b - d)/2 : (-b + d)/2;
    tmin = q / a, tmax = c / q;
    if(tmin > tmax) swap(tmin, tmax);
    return true;
}

struct Intersection { float t; int index; };
inline bool operator <(const Intersection& a, const Intersection& b) { return a.t < b.t /*|| (a.t == b.t && a.density > b.density)*/; } // Sort by t /*, push before pop*/ checked before insert
inline String str(const Intersection& a) { return str(a.t, a.index); }

ImageF porousRock(const Projection& A, uint index) {
    const vec3 center = vec3(A.volumeSize-int3(1))/2.f;
    const float volumeRadius = center.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    const float maximumRadius = 16; // vx
    const float rate = 1./cb(maximumRadius); // 1/vx
    int3 size = A.volumeSize;
    const uint grainCount = rate*size.z*size.y*size.x;
    struct Grain { vec3 center; float radius; uint type; };
    buffer<Grain> grains(grainCount, 0);
    uint typeCount = ref<GrainType>(types).size;
    Random random; // Unseeded sequence (for repeatability)
    for(uint index: range(typeCount)) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        GrainType type = types[index];
        for(uint count=0; count < type.probability * grainCount && grains.size < grains.capacity;) {
            const float r = random()*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
            const vec2 xy = vec2(random(), random()) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-r)) continue;
            count++;
            vec3 p(xy.x, xy.y, r+random()*((size.z-1)-2*r));
            p -= center;
            grains.append( Grain{p, r, index} );
        }
    }

    const float airDensity = 0.001; // Houndsfield ?
    ImageF image (A.projectionSize.xy());
    mat4 imageToWorld = A.imageToWorld(index);
    const float halfHeight = (A.volumeSize.z-1)/2;
    const float outerRadius = (1-2./100) * volumeRadius;
    const float containerDensity = 5.6; // Pure iron
    const float3 origin = imageToWorld[3].xyz();
    array<Intersection> intersections (2*grainCount); // Conservative bound on intersection count
    int counts[typeCount]; mref<int>(counts,typeCount).clear(0);
    for(uint y: range(image.size.y)) for(uint x: range(image.size.x)) {
        const float3 ray = normalize(float(x) * imageToWorld[0].xyz() + float(y) * imageToWorld[1].xyz() + imageToWorld[2].xyz());
        float densityRayIntegral = 0;
        float outer[2]; intersects(halfHeight, outerRadius, origin, ray, outer[0], outer[1]);
        float inner[2]; intersects(halfHeight, innerRadius, origin, ray, inner[0], inner[1]);
        if(outer[0]<inf) {
            float length = inner[0] - outer[0];
            if(length>=0 && length<inf) //assert_(length>=0, length, inner[0], outer[0], "0"); //FIXME
                densityRayIntegral += length * containerDensity;
        }
        if(outer[1]>-inf) {
            float length = outer[1] - inner[1];
            if(length>=0 && length<inf) //assert_(length>=0, length, outer[0], inner[0], inner[1], outer[1], "1"); //FIXME
                densityRayIntegral += length * containerDensity;
        }
        intersections.size = 0;
        for(const Grain& grain: grains) {
            float tmin, tmax;
            if(intersects(grain.radius, grain.center-origin, ray, tmin, tmax) && tmax>tmin /*i.e tmax != tmin*/) {
                intersections.insertSorted( Intersection{tmin, int(1+grain.type)} );
                intersections.insertSorted( Intersection{tmax, -int(1+grain.type)} );
            }
        }
        if(inner[0]<inf && inner[1]>-inf) {
            float lastT = inner[0];
            for(const Intersection& intersection: intersections) {
                float t = intersection.t;
                float length = t - lastT;
                lastT = t;
                if(length > 0) {
                    float density = airDensity;
                    for(uint type: range(typeCount)) if(counts[type]) density = types[type].density; // In order so that lighter grains overrides heavier grains
                    densityRayIntegral += length * density;
                }
                int index =  intersection.index;
                if(index > 0) counts[index-1]++;
                if(index < 0) counts[-index-1]--;
            }
            float length = inner[1] - lastT;
            densityRayIntegral += length * airDensity;
        }
        image(x,y) = densityRayIntegral / A.volumeSize.x;
    }
    return image;
}

struct Analytic : Widget {
   const Projection& A;
   const int upsampleFactor;
   Value& index;

   Analytic(const Projection& A, const int upsampleFactor, Value& index=VolumeView::staticIndex) : A(A), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    int2 sizeHint() override { return upsampleFactor * A.projectionSize.xy(); }
    void render() override {
        ImageF image = porousRock(A, index.value);
        while(image.size < this->target.size()) image = upsample(image);
        float max = convert(target, image);
        Text(str(index.value, max),16,green).render(this->target, 0);
        putImage(target);
    }
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(button) { index.value = clip(0, int(cursor.x*(A.projectionSize.z-1)/(size.x-1)), int(A.projectionSize.z-1)); index.render(); return true; }
        return false;
    }
};

struct App {
    const int N = fromInteger(arguments()[0]);
    VolumeF hostVolume = normalize(porousRock(N));
    CLVolume volume {hostVolume};
    SliceView sliceView {volume, 512/N};
    Projection A {volume.size, volume.size};
    Value projectionIndex {20};
    VolumeView volumeView {volume, A, 512/N, projectionIndex};
    Analytic analyticView {A, 512/N, projectionIndex};
    HBox layout {{ &sliceView, &volumeView, &analyticView }};
    Window window {&layout, str(N)};
    //App() { //writeFile("Data/"_+strx(hostVolume.size)+".ref"_, cast<byte>(hostVolume.data)); }
} app;
