#include "thread.h"
#include "matrix.h"
#include "project.h"

//inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
//struct mat4x3 { float4 rows[3]; };
//inline float3 multiply(const mat4x3 M, float3 v) { return float3(M.rows[0].x*v.x + M.rows[0].y*v.y + M.rows[0].z*v.z + M.rows[0].w, M.rows[1].x*v.x + M.rows[1].y*v.y + M.rows[1].z*v.z + M.rows[1].w, M.rows[2].x*v.x + M.rows[2].y*v.y + M.rows[2].z*v.z + M.rows[2].w); }

struct Test {
    Test() {
        int3 volumeSize (64); //(512,512,896);
        int2 imageSize (64); //(504, 378);
        auto projections = evaluateProjections(volumeSize, imageSize, 64, 1, true);
        Projection p = projections[0];

        //translate(vec3(float(imageSize.x-1)/2,float(imageSize.y-1)/2,0)).
        float4 rayX = p.imageToWorldRay[0];
        float4 rayY = p.imageToWorldRay[1];
        float4 ray1 = p.imageToWorldRay[2];
        /*for(uint y: range(imageSize.y)) for(uint x: range(imageSize.x))*/ {
        float y=(imageSize.y-1)/2.f, x=(imageSize.x-1)/2.f;
            const float4 ray = p.imageToWorldRay * float4(x,y,1.f,1.f); //float(x) * rayX + float(y) * rayY + ray1;
            log(ray);
        }
        /*//for(uint z: range(volumeSize.z)) for(uint y: range(volumeSize.y)) for(uint x: range(volumeSize.x)) {
        uint z = volumeSize.z/2, y=volumeSize.y/2, x=volumeSize.x/2;
            float3 worldPoint(x,y,z);
            float3 viewRay = worldToView * worldPoint;
            viewRay /= viewRay.z;
            float3 worldRay = viewToWorld.normalMatrix() * viewRay;
            error(worldPoint, viewRay, normalize(worldRay)); //normalize(worldPoint-worldViewOrigin), normalize(worldRay));
        //}*/
#if 0
        const float radius = float(volumeSize.x-1)/2;
        const float scale = float(imageSize.x-1)/radius;
        const mat3 transform = mat3().scale(vec3(1,scale,scale)).rotateZ( -projection.angle );
        const float3 center = float3(volumeSize-int3(1))/2.f;
        const float radiusSq = sq(center.x);
        const float2 imageCenter = float2(imageSize-int2(1))/2.f;
        mat4x3 M;
        for(uint i: range(3)) M.rows[i] = {transform.row(i), - float(imageSize.x-1) * projection.offset[i]};

        const float halfHeight = float(volumeSize.z-1 -1 )/2;  //(N-1 [domain size] - epsilon)
        float3 dataCenter = {radius, radius, halfHeight};
        // Projection parameters
        mat3 rotation = mat3().rotateZ(projection.angle); // viewToWorld
        float3 offset = rotation * (radius * projection.offset);
        float extent = float(volumeSize.x-1)/sqrt(1-1/sq(projection.offset.x)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)
        float pixelSize = extent/float(imageSize.x-1); // Pixel size in voxels on origin plane
        float3 rayX = rotation * float3(0,pixelSize,0);
        float3 rayY = rotation * float3(0,0,pixelSize);
        float3 ray1 = rotation * float3(-radius*projection.offset.x,-pixelSize*float(imageSize.x-1)/2,-pixelSize*float(imageSize.y-1)/2);
        mat3 viewToWorld (rayX, rayY, ray1);

        for(uint z: range(volumeSize.z)) for(uint y: range(volumeSize.y)) for(uint x: range(volumeSize.x)) {
            float3 point = float3(x,y,z) - center;
            if(sq(point) < radiusSq) {
                /*float3 p = multiply(M, point); // Homogeneous projection coordinates
                assert_(p.x, p);
                float2 xy = float2(p.y / p.x, p.z / p.x) + imageCenter; // Perspective divide + Image coordinates offset
                float3 ray = xy.x * rayX + xy.y * rayY + ray1;
                log(float3(x,y,z), point, xy, ray, offset);
                assert_(sq(cross(normalize(ray), normalize(point-offset))) == 0, cross(normalize(ray), normalize(point-offset)), ray, point-offset, point, offset);*/
                /*mat3 worldToView = mat3().rotateZ(-projection.angle);
                vec3 p = worldToView * point - radius * projection.offset;
                float2 xy = float(imageSize.x-1) / extent *  float(volumeSize.x-1) * -projection.offset.x * float2(p.y / p.x, p.z / p.x) + imageCenter;
                float3 ray = xy.x * rayX + xy.y * rayY + ray1;
                log(float3(x,y,z), p, xy, ray, offset);
                log(normalize(ray), normalize(point-offset));
                assert_(sq(cross(normalize(ray), normalize(point-offset))) == 0, cross(normalize(ray), normalize(point-offset)), ray, p, point-offset, point, offset);*/
                float3 worldRay = point - offset;
                vec3 viewRay = viewToWorld.inverse() * worldRay;
                viewRay /= viewRay.z;
                float3 ray = viewRay.x * rayX + viewRay.y * rayY + ray1;
                log(normalize(viewRay), normalize(ray));
            }
        }
#endif
    }
} test;
