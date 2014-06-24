#include "random.h"
#include "file.h"
#include "volume.h"
#include "matrix.h"
#include "projection.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "view.h"

const float airDensity = 0.001; // Houndsfield ?
const float containerDensity = 5.6; // Pure iron
const float maximumRadius = 32; // vx
const float rate = 1./cb(maximumRadius); // 1/vx
const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};

VolumeF porousRock(int3 size) {
    assert(size.x == size.y);

    VolumeF volume (size, airDensity);

    const vec2 center = vec2(size.xy()-int2(1))/2.f;
    const float volumeRadius = center.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const float outerRadius = (1-2./100) * volumeRadius;
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

    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    const uint grainCount = rate*size.z*size.y*size.x;
    Random random; // Unseeded sequence (for repeatability)
    Time time;
    for(GrainType type: types) { // Rasterize each grain type in order so that lighter grains overrides heavier grains
        float* volumeData = volume.data;
        const uint XY = size.x*size.x;
        const uint X = size.x;
        for(uint count=0; count < type.probability * grainCount; count++) {
            const float r0 = random();
            const float r1 = random();
            const float r2 = random();
            const float r3 = random();
            const float r = r0*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
            const vec2 xy = vec2(r1, r2) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-r)) continue;
            float cx = xy.x, cy=xy.y, cz=r+r3*((size.z-1)-2*r);
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

void getBoundsForAxis(const vec2 a, const vec3 center, float radius, vec3& U, vec3& L){
    const vec2 projectedCenter = vec2(dot(a, center.xy()), center.z); // Project center from xyz to az
    float t = sqrt(sq(projectedCenter) - sq(radius)); // Hypothenuse length (distance to the tangent points of the sphere (points where a vector from the camera are tangent to the sphere) (calculated a-z space))
    float cLength = norm(projectedCenter);
    float costheta = t / cLength;
    float sintheta = radius / cLength;
    vec2 bounds_az0 = costheta * (mat2(costheta, -sintheta, sintheta, costheta) * projectedCenter);
    U = vec3(bounds_az0.x * a.x, bounds_az0.x * a.y, bounds_az0.y);
    vec2 bounds_az1 = costheta * (mat2(costheta, sintheta, -sintheta, costheta) * projectedCenter);
    L = vec3(bounds_az1.x * a.x, bounds_az1.x * a.y, bounds_az1.y);
}

struct RectF { vec2 min, max; };
RectF getBoundingBox(const vec3& center, float radius, const mat4& projMatrix) {
    vec3 maxXHomogenous, minXHomogenous, maxYHomogenous, minYHomogenous;
    getBoundsForAxis(vec2(1, 0),  center, radius, maxXHomogenous, minXHomogenous);
    getBoundsForAxis(vec2(0, 1), center, radius, maxYHomogenous, minYHomogenous);
    // We only need one coordinate for each point, so we save computation by only calculating x(or y) and w
    float maxX = dot(maxXHomogenous, projMatrix.row(0)) / dot(maxXHomogenous, projMatrix.row(3));
    float minX = dot(minXHomogenous, projMatrix.row(0)) / dot(minXHomogenous, projMatrix.row(3));
    float maxY = dot(maxYHomogenous, projMatrix.row(1)) / dot(maxYHomogenous, projMatrix.row(3));
    float minY = dot(minYHomogenous, projMatrix.row(1)) / dot(minYHomogenous, projMatrix.row(3));
    return {vec2(minX, minY), vec2(maxX, maxY)};
}

void porousRock(const ImageF& target, const Projection& A, uint index) {
    Time totalTime;
    const vec3 volumeCenter = vec3(A.volumeSize-int3(1))/2.f;
    const float volumeRadius = volumeCenter.x;
    const float innerRadius = (1-4./100) * volumeRadius;
    const struct GrainType { const float probability; /*relative concentration*/ const float density; } types[] = {/*Rutile*/{0.7, 4.20}, /*Siderite*/{0.2, 3.96}, /*NaMontmorillonite*/{0.1, 2.65}};
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);
    const float maximumRadius = 32; // vx
    const float rate = 1./cb(maximumRadius); // 1/vx
    int3 size = A.volumeSize;
    const uint grainCount = rate*size.z*size.y*size.x;
    //struct Grain { vec3 center; float radius; uint type; };
    //buffer<Grain> grains(grainCount, 0);
    mat4 worldToView = A.worldToView(index);
    const int3 projectionSize = A.projectionSize;
    const float2 imageCenter = float2(projectionSize.xy()-int2(1))/2.f;
    mat4 viewToWorld = A.worldToScaledView(index).inverse();
    const float3 viewOrigin = viewToWorld[3].xyz();
    mat4 projectionMatrix; projectionMatrix(3,2) = 1;  projectionMatrix(3,3) = 0;
    projectionMatrix = projectionMatrix * mat4().scale(vec3(vec2(float(A.projectionSize.x-1)/A.extent),1/A.distance)).scale(1.f/A.halfVolumeSize);
    //log(worldToView, viewOrigin);
    uint typeCount = ref<GrainType>(types).size;
    Random random; // Unseeded sequence (for repeatability)
    buffer<float> randomSeq (grainCount*4 *2 /*Just to be sure*/);
    for(float& v: randomSeq) v= random(); // Cannot be kept consistent and parallelized (FIXME: do once)
    uint randomIndex = 0;
    static buffer<buffer<Intersection>> intersections (target.size.y*target.size.x, target.size.y*target.size.x, 2*grainCount);
    for(auto& e: intersections) e.size = 0;
    Time rasterizationTime;
    for(uint index: range(typeCount)) { // Rasterizes each grain type in order so that lighter grains overrides heavier grains
        GrainType type = types[index];
        //for(uint count=0; count < type.probability * grainCount;) {
        uint count = type.probability * grainCount;
        const float* random = randomSeq.data + randomIndex;
        parallel(count, [&](const uint, const uint grainIndex) {
            const float radius = random[grainIndex*4+0]*maximumRadius; // Uniform distribution of radius between [0, maximumRadius[
            const vec2 xy = vec2(random[grainIndex*4+1], random[grainIndex*4+2]) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-radius)) return;
            vec3 C(xy.x, xy.y, radius+random[grainIndex*4+3]*((size.z-1)-2*radius));
            C -= volumeCenter; // Translates from data [0..size] to world [±size]
            vec3 vC = worldToView * C; // Transforms from world to homogeneous view coordinates
            RectF r = getBoundingBox(vC, radius, projectionMatrix);
            r.min += imageCenter, r.max += imageCenter;
            for(uint y: range(max(0,int(floor(r.min.y))), min(target.size.y, int(ceil(r.max.y))))) {
                for(uint x: range(max(0,int(floor(r.min.x))), min(target.size.x, int(ceil(r.max.x))))) {
                    const float3 ray = normalize( (x-float(target.size.x-1)/2) * viewToWorld[0].xyz() + (y-float(target.size.y-1)/2) * viewToWorld[1].xyz() + 1.f * viewToWorld[2].xyz() ); // FIXME: store view->image translation in viewToWorld[2] (as in project.cl)
                    float tmin, tmax;
                    if(intersects(radius, C-viewOrigin, ray, tmin, tmax) && tmax>tmin /*i.e tmax != tmin*/) {
                        buffer<Intersection>& list = intersections[y*target.size.x+x];
                        uint i = __sync_fetch_and_add(&list.size, 2);
                        list[i] = Intersection{tmin, int(1+index)};
                        list[i+1] = Intersection{tmax, -int(1+index)};
                    }
                }
            }
        });
        randomIndex += count * 4;
    }
    rasterizationTime.stop();
    Time integrationTime;
    const float halfHeight = (A.volumeSize.z-1)/2;
    const float outerRadius = (1-2./100) * volumeRadius;
    parallel(target.size.y, [&](const uint, const uint y) {
        int counts[typeCount]; mref<int>(counts,typeCount).clear(0);
        for(uint x: range(target.size.x)) {
            const float3 ray = normalize( (x-float(target.size.x-1)/2) * viewToWorld[0].xyz() + (y-float(target.size.y-1)/2) * viewToWorld[1].xyz() + 1.f * viewToWorld[2].xyz()); // FIXME: store view->image translation in viewToWorld[2] (as in project.cl)
            float densityRayIntegral = 0;
            float outer[2]; intersects(halfHeight, outerRadius, viewOrigin, ray, outer[0], outer[1]);
            float inner[2]; intersects(halfHeight, innerRadius, viewOrigin, ray, inner[0], inner[1]);

            if(inner[0]<inf) {
                assert_(inner[1]>-inf);
                {assert_(outer[0]<inf);
                float length = inner[0] - outer[0];
                assert_(length>=0, length, inner[0], outer[0], "0"); //FIXME
                if(length>=0)
                    densityRayIntegral += length * containerDensity;}
                assert_(outer[1]>-inf);
                {float length = outer[1] - inner[1];
                //assert_(length>=0 && length<inf, length, outer[0], inner[0], inner[1], outer[1], "1"); //FIXME
                    if(length>0) densityRayIntegral += length * containerDensity;}

                float lastT = inner[0];
                quicksort(intersections[y*target.size.x+x]);
                for(const Intersection& intersection: intersections[y*target.size.x+x]) {
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
            } else if(outer[0]<inf) {
                assert_(outer[1]>-inf);
                float length = outer[1] - outer[0];
                assert_(length>=0, length, outer[1], outer[0], "outer"); //FIXME
                if(length>=0)
                    densityRayIntegral += length * containerDensity;
            }
            target(x,y) = densityRayIntegral / A.volumeSize.x;
        }
    });
    integrationTime.stop();
    totalTime.stop();
    //log(rasterizationTime, integrationTime, totalTime);
}

struct Analytic : Widget {
   const Projection& A;
   const int upsampleFactor;
   Value& index;

   Analytic(const Projection& A, const int upsampleFactor, Value& index) : A(A), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    int2 sizeHint() override { return upsampleFactor * A.projectionSize.xy(); }
    void render() override {
        ImageF image (A.projectionSize.xy());
        porousRock(image, A, index.value);
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

const int N = fromInteger(arguments()[0]);
VolumeF hostVolume = normalize(porousRock(N));
CLVolume volume {hostVolume};
SliceView sliceView {volume, 512/N};
Projection A {N, N};
Value projectionIndex {20};
VolumeView volumeView {volume, A, 512/N, projectionIndex};
Analytic analyticView {A, 512/N, projectionIndex};
HBox layout {{ &sliceView, &volumeView, &analyticView }};
struct App {
    App() {
        writeFile("Data/"_+strx(hostVolume.size)+".ref"_, cast<byte>(hostVolume.data));
        VolumeF projectionData (A.projectionSize);
        for(uint index: range(projectionData.size.z)) {
            log(index);
            ImageF target = ::slice(projectionData, index);
            porousRock(target, A, index);
        }
        writeFile("Data/"_+strx(projectionData.size)+".proj"_, cast<byte>(projectionData.data));
    }
} app;
Window window {&layout, str(N)};
