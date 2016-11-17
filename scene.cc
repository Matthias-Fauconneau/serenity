#include "scene.h"
#include "data.h"
#include "variant.h"

#if 0
inline Variant parseJSON(TextData& s) {
    s.whileAny(" \t\n\r"_);
    if(s.match("true")) return true;
    else if(s.match("false")) return false;
    else if(s.match('"')) {
        return copyRef(s.until('"'));
    }
    else if(s.match('{')) {
        Dict dict;
        s.whileAny(" \t\n\r"_);
        if(!s.match('}')) for(;;) {
            s.skip('"');
            string key = s.until('"');
            assert_(key && !dict.contains(key));
            s.whileAny(" \t\n\r"_);
            s.skip(':');
            Variant value = parseJSON(s);
            dict.insertSorted(copyRef(key), ::move(value));
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) { s.whileAny(" \t\n\r"_); continue; }
            if(s.match('}')) break;
            error("Expected , or }"_);
        }
        return dict;
    }
    else if(s.match('[')) {
        array<Variant> list;
        s.whileAny(" \t\n\r"_);
        if(!s.match(']')) for(;;) {
            Variant value = parseJSON(s);
            list.append( ::move(value) );
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) continue;
            if(s.match(']')) break;
            error("Expected , or ]"_);
        }
        return list;
    }
    else {
        string d = s.whileDecimal();
        if(d) return parseDecimal(d);
        else error("Unexpected"_, s.peek(16));
    }
}

const mat4 transform(const Dict& object) {
    const Dict& t = object.at("transform");
    ref<Variant> position = t.at("position");
    transform.translate(position);
    ref<Variant> rotation = t.at("rotation");
    ref<Variant> scale = t.at("scale");
    mat4 transform;
    transform.scale(vec3((float)scale[0], (float)scale[1], (float)scale[2]));
    transform.rotateX(rotation[0]*PI/180);
    transform.rotateY(rotation[0]*PI/180);
    transform.rotateZ(rotation[0]*PI/180);
    return transform
}

Scene parseScene(ref<byte> file) {
    TextData s (file);
    Variant root = parseJSON(s);

    Scene scene;
    array<float> X[4], Y[4], Z[4];
    array<float> B, G, R;
    array<Scene::Face> faces;

    for(const Dict& primitive: root.dict.at("primitives"_).list) {

        if((string)primitive.at("type")=="quad"_) {
            ref<vec3> polygon {transform*vec3(0,0,0),transform*vec3(1,0,0),transform*vec3(1,1,0),transform*vec3(0,1,0)};
            assert_(polygon.size == 4);
            const vec3 A = polygon[0], B = polygon[1], C = polygon[2];
            const vec3 N = normalize(cross(B-A, C-A));
            float reflect = N.z == -1;
            const bgr3f color = reflect==0 ? (N+vec3(1))/2.f : 0;
            const float gloss = 1./8;
            scene.faces.append({{0,1,1,0},{0,0,1,1},{N,N,N,N},reflect,0,gloss,0,0});
            scene.B.append(color.b);
            scene.G.append(color.g);
            scene.R.append(color.r);
            for(size_t i: range(4)) {
                scene.X[i].append(polygon[i].x);
                scene.Y[i].append(polygon[i].y);
                scene.Z[i].append(polygon[i].z);
            }
        }
        else error(primitive);
    }

    scene.B = buffer<float>(align(8,faces.size+1), 0);
    scene.G = buffer<float>(align(8,faces.size+1), 0);
    scene.R = buffer<float>(align(8,faces.size+1), 0);
    scene.B.slice(0, B.size).copy(B);
    scene.G.slice(0, G.size).copy(G);
    scene.R.slice(0, R.size).copy(R);
    scene.B[faces.size] = 0;
    scene.G[faces.size] = 0;
    scene.R[faces.size] = 0;
    for(size_t i: range(4)) {
        scene.X[i] = buffer<float>(align(8,faces.size), 0);
        scene.X[i].slice(0, X[i].size).copy(X[i]);
        scene.Y[i] = buffer<float>(align(8,faces.size), 0);
        scene.Y[i].slice(0, Y[i].size).copy(Y[i]);
        scene.Z[i] = buffer<float>(align(8,faces.size), 0);
        scene.Z[i].slice(0, Z[i].size).copy(Z[i]);
    }
    scene.faces = buffer<Scene::Face>(align(8,faces.size), 0);
    scene.faces.slice(0, faces.size).copy(faces);
    scene.fit();

    scene.viewpoint = transform( root.dict.at("camera") ).inverse() * vec3(0,0,0);

    return scene;
}

#else

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
                                                   normals[normalIndices[faceIndex][2]],normals[normalIndices[faceIndex][3]]},0,0,1,0,0};
            if(faceObjectName[faceIndex] == "Cylinder") face.refract = 1;
            scene.faces.append(face);
            bgr3f color = 1;
            if(faceObjectName[faceIndex] == "Cube") color = bgr3f(faceIndex&0b100,faceIndex&0b010,faceIndex&0b001);
            scene.B.append(color.b);
            scene.G.append(color.g);
            scene.R.append(color.r);
        }
    } else {
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
            const float gloss = 1./8;
            scene.faces.append({{0,1,1,0},{0,0,1,1},{N,N,N,N},reflect,0,gloss,0,0});
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
    }
    scene.fit();
    return scene;
}
#endif
