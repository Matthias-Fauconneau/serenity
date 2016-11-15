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
    Scene scene;
    if(s.match("mtllib")) {
        mat4 transform = mat4().rotateX(PI/2);
        s.until('\n');
        array<vec3> vertices;
        array<vec3> normals;
        array<uint4> indices;
        array<uint4> normalIndices;
        array<string> faceObjectName;
        string objectName;
        while(s) {
            /**/ if(s.match("v ")) {
                vertices.append( transform * parse<vec3>(s) );
                s.skip('\n');
            }
            else if(s.match("vn ")) {
                normals.append( transform.normalMatrix() * parse<vec3>(s) );
                s.skip('\n');
            }
            else if(s.match("f ")) {
                array<int> v, vn;
                for(;;) {
                 v.append(s.integer()-1);
                 s.skip("//");
                 vn.append(s.integer()-1);
                 if(s.match('\n')) break;
                 s.skip(' ');
                }
                //assert_(a.size == 4, a.size);
                if(v.size == 4) {
                    indices.append(uint4(v[0], v[1], v[2], v[3]));
                    normalIndices.append(uint4(vn[0], vn[1], vn[2], vn[3]));
                    faceObjectName.append(objectName); // FIXME
                }
            }
            else if(s.match("o ")) objectName = s.until("\n");
            else if(s.match("vn ")) s.until("\n");
            else if(s.match("usemtl ")) s.until("\n");
            else if(s.match("s ")) s.until("\n");
            else error(s);
        }

        scene.viewpoint = vec3(0,0,-4);
        const size_t faceCount = indices.size;
        scene.faces = buffer<Scene::Face>(align(8,faceCount), 0);
        scene.B = buffer<float>(align(8,faceCount+1), 0);
        scene.G = buffer<float>(align(8,faceCount+1), 0);
        scene.R = buffer<float>(align(8,faceCount+1), 0);
        // index=faceCount flags miss (raycast hits no face) (i.e background "face" color)
        scene.B[faceCount] = 0;
        scene.G[faceCount] = 0;
        scene.R[faceCount] = 0;
        for(size_t i: range(4)) {
            scene.X[i] = buffer<float>(align(8,faceCount), 0);
            scene.Y[i] = buffer<float>(align(8,faceCount), 0);
            scene.Z[i] = buffer<float>(align(8,faceCount), 0);
        }
        //for(uint4 face: indices) {
        for(size_t faceIndex: range(indices.size)) {
            const uint4& faceIndices = indices[faceIndex];
            for(size_t vertexIndex: range(4)) {
                scene.X[vertexIndex].append(vertices[faceIndices[vertexIndex]].x);
                scene.Y[vertexIndex].append(vertices[faceIndices[vertexIndex]].y);
                scene.Z[vertexIndex].append(vertices[faceIndices[vertexIndex]].z);
            }
            //const vec3 A = vertices[faceIndices[0]], B = vertices[faceIndices[1]], C = vertices[faceIndices[2]];
            //const vec3 N = normalize(cross(B-A, C-A)); Scene::Face face {{0,1,1,0},{0,0,1,1},0,0,N,0,0};
            Scene::Face face {{0,1,1,0},{0,0,1,1},{normals[normalIndices[faceIndex][0]],normals[normalIndices[faceIndex][1]],
                                                   normals[normalIndices[faceIndex][2]],normals[normalIndices[faceIndex][3]]},0,0,0,0};
            if(faceObjectName[faceIndex] == "Cylinder") face.refract = 1;
            scene.faces.append(face);
            bgr3f color = 1;
            if(faceObjectName[faceIndex] == "Cube") color = bgr3f(faceIndex&0b100,faceIndex&0b010,faceIndex&0b001);
            scene.B.append(color.b);
            scene.G.append(color.g);
            scene.R.append(color.r);
        }
    } else {
#if 0
        scene.viewpoint = parse<vec3>(s);
        s.skip('\n');

        const size_t faceCount = parse<uint>(s);
        s.skip('\n');

        scene.faces = buffer<Scene::Face>(align(8,faceCount), 0);
        scene.B = buffer<float>(align(8,faceCount+1), 0);
        scene.G = buffer<float>(align(8,faceCount+1), 0);
        scene.R = buffer<float>(align(8,faceCount+1), 0);
        // index=faceCount flags miss (raycast hits no face) (i.e background "face" color)
        scene.B[faceCount] = 0;
        scene.G[faceCount] = 0;
        scene.R[faceCount] = 0;
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
            scene.faces.append({{0,1,1,0},{0,0,1,1},reflect,0,N,0,0});
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
#else
        error("Unsupported");
#endif
    }
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
