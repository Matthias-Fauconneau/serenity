#if 0
#include "project.h"
#include "window.h"

struct TamDanielson : Widget {
    const int3 volumeSize = int3(512);
    const int3 projectionSize = int3(512);
    Window window {this, "Tam Danielson Window"_, projectionSize.xy()};
    uint viewIndex = 0;
    void render() override {
        target.buffer.clear(byte4(0,0,0,0xFF));
        for(uint viewIndex: range(projectionSize.z)) {
            for(uint index: range(projectionSize.z)) {
                if(index == viewIndex) continue;
                const float2 imageCenter = float2(projectionSize.xy()-int2(1))/2.f;
                const bool doubleHelix = false;
                const uint numberOfRotations = 1;
                const float4 world = Projection(volumeSize, projectionSize, doubleHelix, numberOfRotations).worldToScaledView(index).inverse()[3];
                const float4 view = Projection(volumeSize, projectionSize, doubleHelix, numberOfRotations).worldToScaledView(viewIndex) * world;
                float2 image = view.xy() / view.z + imageCenter; // Perspective divide + Image coordinates offset
                int2 integer = int2(round(image));
                if(integer>=int2(0) && integer<target.size()) target(integer.x, integer.y) = 0xFF;
            }
        }
    }
    bool mouseEvent(int2 cursor, int2 size, Event, Button button) {
        if(button) { viewIndex = clip(0, int(cursor.x*(projectionSize.z-1)/(size.x-1)), int(projectionSize.z-1)); render(); putImage(target); return true; }
        return false;
    }
} app;
#endif

#include "thread.h"
#include "matrix.h"
#include "project.h"

inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
//struct mat4x3 { float4 rows[3]; };
//inline float3 multiply(const mat4x3 M, float3 v) { return float3(M.rows[0].x*v.x + M.rows[0].y*v.y + M.rows[0].z*v.z + M.rows[0].w, M.rows[1].x*v.x + M.rows[1].y*v.y + M.rows[1].z*v.z + M.rows[1].w, M.rows[2].x*v.x + M.rows[2].y*v.y + M.rows[2].z*v.z + M.rows[2].w); }

struct Test {
    Test() {
        const uint N = 64;
        int3 volumeSize (N);
        int3 projectionSize (N);
        Projection A(volumeSize, projectionSize);
        for(uint index: range(A.count)) {
            float3 center = vec3(volumeSize-int3(1))/2.f;
            struct mat4 worldToScaledView = A.worldToScaledView(index);
            const float2 imageCenter = float2(projectionSize.xy()-int2(1))/2.f;
            struct mat4 worldToDevice = A.worldToDevice(index);
            struct mat4 viewToWorld = worldToScaledView.inverse();
            float3 origin = viewToWorld[3].xyz(); // imageToWorld * vec2(size/2, 0, 1)
            mat4 imageToWorld = viewToWorld; // image coordinates [size, 1] to world coordinates [±1]
            imageToWorld[2] += imageToWorld * vec4(-vec2(projectionSize.xy()-1)/2.f,0,0); // Stores view to image coordinates translation in imageToWorld[2] as we know it will be always be called with z=1
            imageToWorld[3] = vec4(origin, 1); // Stores origin in 4th column unused by ray direction transformation (imageToWorld*(x,y,1,0)), allows to get origin directly as imageToWorld*(0,0,0,1) instead of imageToWorld*(size/2,0,1)

            const float radius = center.x;
            const float radiusSq = sq(radius);
#if 1
            const float halfHeight = center.z;  //(N-1 [domain size] - epsilon)
            float2 plusMinusHalfHeightMinusOriginZ = float2(1,-1) * (center.z-1.f/2/*fix OOB*/) - origin.z;
            const float c = sq(origin.xy()) - sq(center.x) + 1;
            const float3 dataOrigin = center + origin;
            for(uint y: range(projectionSize.y)) for(uint x: range(projectionSize.x)) {
                const float3 ray = normalize(float(x) * imageToWorld[0].xyz() + float(y) * imageToWorld[1].xyz() + imageToWorld[2].xyz());
                // Intersects cap disks
                const float2 capT = plusMinusHalfHeightMinusOriginZ / ray.z; // top bottom
                const float4 capXY = vec4(origin.x,origin.y,origin.x,origin.y) + vec4(capT.x,capT.x,capT.y,capT.y) * vec4(ray.x,ray.y,ray.x,ray.y); // topX topY bottomX bottomY
                const float4 capXY2 = capXY*capXY;
                const float2 capR2 = vec2(capXY2[0],capXY2[2]) + vec2(capXY2[1],capXY2[3]); // topR² bottomR²
                // Intersect cylinder side
                const float a = dot(ray.xy(), ray.xy());
                const float b = 2*dot(origin.xy(), ray.xy());
                const float sqrtDelta = sqrt(b*b - 4 * a * c);
                const float2 sideT = (-b + float2(sqrtDelta,-sqrtDelta)) / (2*a); // t±
                const float2 sideZ = abs(origin.z + sideT * ray.z); // |z±|
                float tmin=inf, tmax=-inf;
                if(capR2[0] < radiusSq) tmin=min(tmin, capT[0]), tmax=max(tmax, capT[0]); // top
                if(capR2[1] < radiusSq) tmin=min(tmin, capT[1]), tmax=max(tmax, capT[1]); // bottom
                if(sideZ[0] < halfHeight) tmin=min(tmin, sideT[0]), tmax=max(tmax, sideT[0]); // side+
                if(sideZ[1] < halfHeight) tmin=min(tmin, sideT[1]), tmax=max(tmax, sideT[1]); // side-
                float3 position = dataOrigin + tmin * ray; // [-size/2, size/2] -> [0, ]
                while(tmin < tmax) { // Uniform ray sampling with trilinear interpolation
                    float3 world = position - center;
                    float4 view = world.x * worldToDevice[0] + world.y * worldToDevice[1] + world.z * worldToDevice[2] + worldToDevice[3]; // Homogeneous view coordinates
                    float2 image = view.xy() / view.w + imageCenter; // Perspective divide + Image coordinates offset
                    float delta = norm(image-float2(x,y));
                    assert_(delta < 0x1p-20, log2(delta), world, image, float2(x,y));
                    tmin+=1; position += ray;
                }
            }
#endif
#if 1
            for(uint z: range(volumeSize.z)) for(uint y: range(volumeSize.y)) for(uint x: range(volumeSize.x)) {
                float3 world = float3(x,y,z) - center;
                if(sq(world.xy()) < radiusSq) {
                    float4 view = world.x * worldToDevice[0] + world.y * worldToDevice[1] + world.z * worldToDevice[2] + worldToDevice[3]; // Homogeneous view coordinates
                    float2 image = view.xy() / view.w + imageCenter; // Perspective divide + Image coordinates offset
                    float3 ray = image.x * imageToWorld[0].xyz() + image.y * imageToWorld[1].xyz() + imageToWorld[2].xyz();
                    float delta = norm(cross(normalize(world-origin), normalize(ray)));
                    assert_(delta < 0x1p-20, log2(delta), x,y,z, world, view, image, ray);
                }
            }
#endif
        }
    }
} test;
