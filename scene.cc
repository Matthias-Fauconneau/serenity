#include "scene.h"
#include "data.h"

//include "parse.h"
// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
template<> inline uint parse<uint>(TextData& s) { return s.integer(false); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny(" \t"); value[index] = parse<Type V::_T>(s); }
    return value;
}
template<> inline uint3 parse<uint3>(TextData& s) { return parseVec<uint3>(s); }
template<> inline uint4 parse<uint4>(TextData& s) { return parseVec<uint4>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }

Scene parseScene(ref<byte> file) {
    TextData s (file);
    while(s.match('#')) s.until('\n');
    if(s.match("ply\n")) {
#if 0
        s.skip("format ascii 1.0\n");
        s.skip("comment "); s.until('\n');
        s.skip("element vertex ");
        const uint vertexCount = s.integer();
        s.skip('\n');
        while(s.match("property")) s.until("\n");
        s.skip("element face ");
        const uint faceCount = s.integer();
        s.skip('\n');
        while(s.match("property")) s.until("\n");
        s.skip("end_header\n");
        buffer<vec3> vertices (vertexCount);
        for(size_t i : range(vertexCount)) {
            vertices[i] = parse<vec3>(s);
            s.until('\n');
        }

        vec3 min = inff, max = -inff;
        for(vec3 p: vertices) { min = ::min(min, p); max = ::max(max, p); }
        log(min, max);

        buffer<uint4> indices (faceCount);
        for(size_t i : range(faceCount)) {
            uint length = s.integer();
            if(length==3) {
                indices[i] = uint4(parse<uint3>(s), 0);
                indices[i][3] = indices[i][2]; // FIXME
            } else {
                assert_(length == 4, length);
                indices[i] = parse<uint4>(s);
            }
            s.until('\n');
        }

        const vec3 viewpoint (0,0,-64);
        buffer<Scene::Face> faces (faceCount, 0);
        for(uint4 face: indices) {
            float reflect = faces.size%2==0;
            log(faces.size);
            const vec3 A = vertices[face[0]], B = vertices[face[1]], C = vertices[face[2]];//, D = vertices[face[3]];
            const vec3 N = normalize(cross(B-A, C-A));
            const vec3 color = reflect==0 ? (N+vec3(1))/2.f : 0;
            faces.append({{vertices[face[0]], vertices[face[1]], vertices[face[2]], vertices[face[3]]},{0,1,1,0},{0,0,1,1},0,0,buffer<half>(),color,reflect});
            //faces.append({{vertices[face[3]], vertices[face[2]], vertices[face[1]], vertices[face[0]]},{0,1,1,0},{0,0,1,1},Image8()});
        }
        return {viewpoint, ::move(faces)};
#else
        error("Unsupported");
#endif
    } else {
        Scene scene;

        scene.viewpoint = parse<vec3>(s);
        s.skip('\n');

        const size_t faceCount = parse<uint>(s);
        s.skip('\n');

        scene.faces = buffer<Scene::Face>(align(8,faceCount), 0);
        scene.B = buffer<float>(align(8,faceCount), 0);
        scene.G = buffer<float>(align(8,faceCount), 0);
        scene.R = buffer<float>(align(8,faceCount), 0);
        for(size_t i: range(4)) {
            scene.X[i] = buffer<float>(align(8,faceCount), 0);
            scene.Y[i] = buffer<float>(align(8,faceCount), 0);
            scene.Z[i] = buffer<float>(align(8,faceCount), 0);
        }

        while(s) {
            if(s.match('\n')) continue;
            if(s.match('#')) { s.until('\n'); continue; }
            array<vec3> polygon;
            while(s && !s.match('\n')) { // Empty line
                polygon.append(parse<vec3>(s));
                if(!s) break;
                s.skip('\n');
            }
            assert_(polygon.size == 4);
            const vec3 A = polygon[0], B = polygon[1], C = polygon[2];
            const vec3 N = normalize(cross(B-A, C-A));
            float reflect = N.z == -1;
            const bgr3f color = reflect==0 ? (N+vec3(1))/2.f : 0;
            scene.faces.append({{0,1,1,0},{0,0,1,1},reflect,N,0,0});
            scene.B.append(color.b);
            scene.G.append(color.g);
            scene.R.append(color.r);
            for(size_t i: range(4)) {
                scene.X[i].append(polygon[i].x);
                scene.Y[i].append(polygon[i].y);
                scene.Z[i].append(polygon[i].z);
            }
        }
        assert_(scene.faces.size == faceCount);
#if 0
        // Precomputes barycentric coordinates of V11
        scene.a11 = buffer<float>(faces.size);
        scene.b11 = buffer<float>(faces.size);
        for(size_t i: range(faces.size)) {
            const vec3 v00 (X[0][i], Y[0][i], Z[0][i]);
            const vec3 v10 (X[1][i], Y[1][i], Z[1][i]);
            const vec3 v11 (X[2][i], Y[2][i], Z[2][i]);
            const vec3 v01 (X[3][i], Y[3][i], Z[3][i]);
            const vec3 e01 = v10 - v00;
            const vec3 e03 = v01 - v00;
            const vec3 N = cross(e01, e03);
            const vec3 e02 = v11 - v00;
            float a11, b11;
            /**/ if(abs(N.x) > abs(N.y) && abs(N.x) > abs(N.z)) { // X
                a11 = (e02.y*e03.z-e02.z*e03.y)/N.x;
                b11 = (e01.y*e02.z-e01.z*e02.y)/N.x;
            }
            else if(abs(N.y) > abs(N.x) && abs(N.y) > abs(N.z)) { // Y
                a11 = (e02.z*e03.x-e02.x*e03.z)/N.y;
                b11 = (e01.z*e02.x-e01.x*e02.z)/N.y;
            }
            else /*if(abs(N.z) > abs(N.x) && abs(N.z) > abs(N.y))*/ { // Z
                a11 = (e02.x*e03.y-e02.y*e03.x)/N.z;
                b11 = (e01.x*e02.y-e01.y*e02.x)/N.z;
            }
            scene.a11[i] = a11;
            scene.b11[i] = b11;
        }
#endif
        scene.fit();

        return scene;
    }
}
