#include "synthetic.h"
#include "random.h"
#include "file.h"
#include "matrix.h"
#include "projection.h"

/// Ray-cylinder intersection
/// \note Handles intersection with both the cylinder side and caps
/// \param halfHeight Half height of the cylinder centered on zero.
/// \param radius Radius of the cylinder
/// \param origin Origin of the ray
/// \param ray Direction of the ray
/// \param tmin First intersection with the cylinder
/// \param tmin Last intersection with the cylinder
inline void intersects(const float halfHeight, const float radius, const float3 origin, const float3 ray, float& tmin, float& tmax) {
    float2 plusMinusHalfHeightMinusOriginZ = float2(1,-1) * (halfHeight-1.f/2/*fix out of bounds*/) - origin.z;
    float radiusSq = sq(radius);
    float c = sq(origin.xy()) - radiusSq + 1 /*fix out of bounds*/;
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

/// Ray-sphere intersection
/// \a param radius Radius of the sphere centered on zero.
/// \param origin Origin of the ray
/// \param ray Direction of the ray
/// \param tmin First intersection with the sphere
/// \param tmin Last intersection with the sphere
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

/// Returns the bounding points \a U, \a L along axis \a for a sphere at \a center of radius \a radius.
void boundsForAxis(const vec2 a, const vec3 center, float radius, vec3& U, vec3& L) {
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

/// Axis-aligned rectangle with 2D floating-point coordinates
struct RectF { vec2 min, max; };

/// Returns the rectangle bounding the perspective projection for a sphere at \a center of radius \a radius.
RectF getBoundingRect(const vec3& center, float radius, const mat4& projMatrix) {
    vec3 maxXHomogenous, minXHomogenous; boundsForAxis(vec2(1, 0),  center, radius, maxXHomogenous, minXHomogenous); // Bounds along X axis
    vec3 maxYHomogenous, minYHomogenous; boundsForAxis(vec2(0, 1), center, radius, maxYHomogenous, minYHomogenous); // Bounds along Y axis
    // Projects each bounds to device coordinates
    float maxX = dot(maxXHomogenous, projMatrix.row(0)) / dot(maxXHomogenous, projMatrix.row(3));
    float minX = dot(minXHomogenous, projMatrix.row(0)) / dot(minXHomogenous, projMatrix.row(3));
    float maxY = dot(maxYHomogenous, projMatrix.row(1)) / dot(maxYHomogenous, projMatrix.row(3));
    float minY = dot(minYHomogenous, projMatrix.row(1)) / dot(minYHomogenous, projMatrix.row(3));
    return {vec2(minX, minY), vec2(maxX, maxY)};
}

PorousRock::PorousRock(int3 size) : size(size) {
    assert(size.x == size.y);
    assert(sum(apply(ref<GrainType>(types), [](GrainType t){return t.probability;})) == 1);

    Random random;
    for(GrainType& type: types) {
        type.grains = buffer<vec4>(type.relativeGrainCount * grainCount, 0);
        while(type.grains.size < type.grains.capacity) {
            const float r = minimumGrainRadiusVx+random()*(maximumGrainRadiusVx-minimumGrainRadiusVx); // Uniform distribution of radius (TODO: gaussian)
            const vec2 xy = vec2(random(), random()) * vec2(size.xy());
            if(sq(xy-vec2(volumeRadius)) > sq(innerRadius-r)) continue;
            type.grains.append( vec4(xy, r+random()*((size.z-1)-2*r), r) );
            if(&type==&types[2] && r > largestGrain.w) largestGrain = type.grains.last(); // Only last type as heavier largest grains  might be overriden
        }
    }
}

VolumeF PorousRock::volume() {
    VolumeF volume (size, airAttenuation, "x0"_);

    for(uint y: range(size.y)) for(uint x: range(size.x)) { // Rasterizes container cylinder shell
        float r2 = sq(vec2(x,y)-vec2(size.xy()-1)/2.f);
        if(sq(innerRadius-1./2) < r2 && r2 < sq(outerRadius+1./2)) {
            float c = 0; // Coverage
            if(r2 < sq(innerRadius+1./2)) { // Approximate analytic coverage at inner edge
                float r = sqrt(r2);
                c = r - (innerRadius-1./2);
            }
            else if(sq(outerRadius-1./2) < r2) { // Approximate analytic coverage at outer edge
                float r = sqrt(r2);
                c = (outerRadius+1./2) - r;
            }
            else c = 1; // Full coverage within both edges
            for(uint z: range(size.z)) volume(x,y,z) = (1-c) * volume(x,y,z) + c *  containerAttenuation; // Coverage resolution by alpha blending (exact)
        }
    }

    Time time;
    for(const GrainType& type: types) { // Rasterizes each grain type in order so that grains heavier overrides lighter grains
        float* volumeData = volume.data;
        const int XY = size.x*size.x;
        const int X = size.x;
        for(vec4 grain: type.grains) { // Rasterizes grains as spheres
            float cx = grain.x, cy=grain.y, cz=grain.z, r=grain.w;
            float innerR2 = sq(r-1.f/2);
            float outerRadius = r+1.f/2;
            float outerR2 = sq(outerRadius);
            float v = type.attenuation;
            int iz = cz-r-1./2, iy = cy-r-1./2, ix = cx-r-1./2; // ⌊Sphere bounding box⌋
            float fz = cz-iz, fy=cy-iy, fx=cx-ix; // Relative center coordinates
            uint grainSize = ceil(2*(r+1./2)); // Bounding box size
            float* volume0 = volumeData + iz*XY + iy*X + ix; // Pointer to bounding box samples
            for(uint z=0; z<grainSize; z++) {
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
                        if(r2 <= innerR2) volumeZY[x] = v; // Full coverage
                        else if(r2 < outerR2) {
                            float c = outerRadius - sqrt(r2); // Approximate analytical coverage
                            volumeZY[x] = (1-c) * volumeZY[x] + c * v; // Coverage resolution by alpha blending (approximate)
                        }
                    }
                }
            }
        }
    }
    log(time);
    return volume;
}

float PorousRock::project(const ImageF& target, const Projection& A, uint index) {
    Time totalTime;

    mat4 worldToView = A.worldToView(index);
    const int3 projectionSize = A.projectionSize;
    const float2 imageCenter = float2(projectionSize.xy()-int2(1))/2.f;
    mat4 viewToWorld = A.worldToScaledView(index).inverse();
    const float3 viewOrigin = viewToWorld[3].xyz();
    mat4 imageToWorld = viewToWorld;
    imageToWorld[2] += imageToWorld * vec4(-vec2(target.size-int2(1))/2.f,0,0); // Stores view to image coordinates translation in imageToWorld[2] as we know it will be always be called with z=1
    mat4 projectionMatrix; projectionMatrix(3,2) = 1;  projectionMatrix(3,3) = 0;
    projectionMatrix = projectionMatrix * mat4().scale(vec3(vec2(float(A.projectionSize.x-1)/A.extent),1/A.distance)).scale(vec3(1.f/A.volumeRadius));

    uint typeCount = ref<GrainType>(types).size;

    if(intersections.size != size_t(target.size.y*target.size.x*grainCount*2)) { // Reuses same allocation when possible to avoid unneeded page clearing
        intersectionCounts = buffer<size_t>(target.size.y*target.size.x*grainCount*2); // Conservative allocation (to allow a ray to intersect all grains)
        intersections = buffer<Intersection>(target.size.y*target.size.x*grainCount*2); // Conservative allocation (to allow a ray to intersect all grains)
    }
    intersectionCounts.clear();

    Time rasterizationTime;
    for(uint index: range(typeCount)) {
        const GrainType& type = types[index];
        parallel(type.grains, [&](const uint, const vec4& grain) { // First rasterizes grains to per pixel intersection lists in order to avoid intersecting all grains with all pixels. Parallel processing is scheduled at a granularity ! of one grain per job.
            size_t* const intersectionCounts = this->intersectionCounts.begin();
            Intersection* const intersections = this->intersections.begin();
            const size_t stride = grainCount*2;
            float radius = grain.w;
            vec3 C = grain.xyz() - volumeCenter; // Translates from data [0..size] to world [±size]
            vec3 vC = worldToView * C; // Transforms from world to homogeneous view coordinates
            RectF r = getBoundingRect(vC, radius, projectionMatrix);
            r.min += imageCenter, r.max += imageCenter;
            for(uint y: range(max(0,int(floor(r.min.y))), min(target.size.y, int(ceil(r.max.y))))) {
                for(uint x: range(max(0,int(floor(r.min.x))), min(target.size.x, int(ceil(r.max.x))))) {
                    const float3 ray = normalize(float(x) * imageToWorld[0].xyz() + float(y) * imageToWorld[1].xyz() + imageToWorld[2].xyz());
                    float tmin, tmax;
                    if(intersects(radius, C-viewOrigin, ray, tmin, tmax) && tmax>tmin /*i.e tmax != tmin (tangent intersection)*/) {
                        uint index = y*target.size.x+x;
                        size_t i = __sync_fetch_and_add(intersectionCounts+index, 2); // Atomically increments the intersection count
                        intersections[index*stride+i] = Intersection{tmin, int(1+index)};
                        intersections[index*stride+i+1] = Intersection{tmax, -int(1+index)};
                    }
                }
            }
        });
    }
    rasterizationTime.stop();

    Time integrationTime;
    const float halfHeight = (A.volumeSize.z-1)/2.f;
    const float outerRadius = (1-2./100) * volumeRadius;
    float maxAttenuation = 0;
    parallel(target.size.y, [&](const uint, const uint y) { // Resolves pixels (parallel processing at one line per job).
        const size_t stride = grainCount*2;
        int counts[typeCount]; mref<int>(counts,typeCount).clear(0);
        for(uint x: range(target.size.x)) {
            const float3 ray = normalize(float(x) * imageToWorld[0].xyz() + float(y) * imageToWorld[1].xyz() + imageToWorld[2].xyz());
            float attenuationSum = 0;
            float outer[2]; intersects(halfHeight, outerRadius, viewOrigin, ray, outer[0], outer[1]); // Outer cylinder
            float inner[2]; intersects(halfHeight, innerRadius, viewOrigin, ray, inner[0], inner[1]); // Inner cylinder

            if(inner[0]<inf) { // Penetrates inner cylinder
                attenuationSum += (inner[0] - outer[0]) * containerAttenuation; // Integrates attenuation within cylinder shell (first intersection)
                assert_((outer[1] - inner[1]) >= 0); // FIXME
                if((outer[1] - inner[1]) >= 0) attenuationSum += (outer[1] - inner[1]) * containerAttenuation; // Integrates attenuation within cylinder shell (last intersection)

                float lastT = inner[0];
                int index = y*target.size.x+x;
                mref<Intersection> intersections = this->intersections.slice(index*stride, intersectionCounts[index]);
                quicksort(intersections); // Sorts intersections list by distance along ray
                for(const Intersection& intersection: intersections) {
                    float t = intersection.t;
                    float length = t - lastT;
                    lastT = t;
                    if(length > 0) {
                        float attenuation = airAttenuation;
                        for(uint type: range(typeCount)) if(counts[type]) attenuation = types[type].attenuation; // In order so that heavier grains overrides lighter grains
                        attenuationSum += length * attenuation;
                    }
                    int index =  intersection.index;
                    if(index > 0) counts[index-1]++;
                    if(index < 0) counts[-index-1]--;
                }
                attenuationSum += (inner[1] - lastT) * airAttenuation;
            } else if(outer[0]<inf) {
                attenuationSum += (outer[1] - outer[0]) * containerAttenuation;
            }
            maxAttenuation = ::max(maxAttenuation, attenuationSum);
            target(x,y) = attenuationSum;
        }
    });
    integrationTime.stop();
    totalTime.stop();
    //log(rasterizationTime, integrationTime, totalTime, maxAttenuation);
    assert_(maxAttenuation>0.17 && maxAttenuation < 1, maxAttenuation); // Ensures attenuation is not too high (receiving few photons incurs high noise) nor too low (for contrast)
    return maxAttenuation;
}
