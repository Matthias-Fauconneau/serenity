#include "scene.h"
#include "data.h"
#include "variant.h"
#include <sys/mman.h>
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
    mat4 transform;
    if(t.contains("position")) {
        ref<Variant> position = t.at("position");
        transform.translate(vec3((float)position[0],(float)position[1],(float)position[2]));
    }
    if(t.contains("scale")) {
        ref<Variant> scale = t.at("scale");
        transform.scale(vec3((float)scale[0], (float)scale[1], (float)scale[2]));
    }
    if(t.contains("rotation")) {
        ref<Variant> rotation = t.at("rotation");
        transform.rotateX(rotation[0]*PI/180);
        transform.rotateY(rotation[1]*PI/180);
        transform.rotateZ(rotation[2]*PI/180);
    }
    return transform;
}

Scene parseScene(ref<byte> file) {
    TextData s (file);
    Variant root = parseJSON(s);

    mat4 camera;// = transform( root.dict.at("camera") ).inverse();

    Scene scene;
    array<float> X[4], Y[4], Z[4];
    array<float> B, G, R;
    array<Scene::Face> faces;

    for(const Dict& primitive: root.dict.at("primitives"_).list) {
        mat4 transform = camera * ::transform(primitive);
        auto quad = [&](vec3 v00, vec3 v10, vec3 v11, vec3 v01) {
            vec3 quad[4] {transform*v00,transform*v10,transform*v11,transform*v01};
            const vec3 N = normalize(transform.normalMatrix() * vec3(0,0,1));
            float reflect = N.z == -1;
            const bgr3f color = reflect==0 ? (N+vec3(1))/2.f : 0;
            const float gloss = 1./8;
            faces.append({{0,1,1,0},{0,0,1,1},{N,N,N,N},reflect,0,gloss,0,0});
            B.append(color.b);
            G.append(color.g);
            R.append(color.r);
            for(size_t i: range(4)) {
                X[i].append(quad[i].x);
                Y[i].append(quad[i].y);
                Z[i].append(quad[i].z);
            }
        };

        if((string)primitive.at("type")=="quad"_) {
            quad(vec3(0,0,0),vec3(1,0,0),vec3(1,1,0),vec3(0,1,0));
        }
        else if((string)primitive.at("type")=="cube"_) {
            quad(vec3(0,0,0),vec3(1,0,0),vec3(1,1,0),vec3(0,1,0));
            quad(vec3(0,0,1),vec3(1,0,1),vec3(1,1,1),vec3(0,1,1));
            quad(vec3(0,0,0),vec3(0,1,0),vec3(0,1,1),vec3(0,0,1));
            quad(vec3(1,0,0),vec3(1,1,0),vec3(1,1,1),vec3(1,0,1));
            quad(vec3(0,0,0),vec3(0,1,0),vec3(0,1,1),vec3(0,0,1));
            quad(vec3(0,1,0),vec3(1,1,0),vec3(1,1,1),vec3(1,0,1));
        }
        else error("Unknown primitive", primitive);
    }

    scene.B = buffer<float>(align(8,faces.size+1), faces.size);
    scene.G = buffer<float>(align(8,faces.size+1), faces.size);
    scene.R = buffer<float>(align(8,faces.size+1), faces.size);
    scene.B.slice(0, B.size).copy(B);
    scene.G.slice(0, G.size).copy(G);
    scene.R.slice(0, R.size).copy(R);
    scene.B[faces.size] = 0;
    scene.G[faces.size] = 0;
    scene.R[faces.size] = 0;
    for(size_t i: range(4)) {
        scene.X[i] = buffer<float>(align(8,faces.size), X[i].size);
        scene.X[i].slice(0, X[i].size).copy(X[i]);
        scene.Y[i] = buffer<float>(align(8,faces.size), Y[i].size);
        scene.Y[i].slice(0, Y[i].size).copy(Y[i]);
        scene.Z[i] = buffer<float>(align(8,faces.size), Z[i].size);
        scene.Z[i].slice(0, Z[i].size).copy(Z[i]);
        //log(X[i], Y[i], Z[i]);
    }
    scene.faces = buffer<Scene::Face>(align(8,faces.size), faces.size);
    scene.faces.slice(0, faces.size).copy(faces);
    scene.fit();
    log(scene.min, scene.max);

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
#if 0
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
    } else
#endif
    {
        vec3 viewpoint = parse<vec3>(s);
        s.skip('\n');

        const size_t quadCount = parse<uint>(s);
        s.skip('\n');

        scene.faces = buffer<Scene::Face>(align(8,2*quadCount), 0);
        scene.emittanceB = buffer<float>(align(8,2*quadCount+1), 0);
        scene.emittanceG = buffer<float>(align(8,2*quadCount+1), 0);
        scene.emittanceR = buffer<float>(align(8,2*quadCount+1), 0);
        scene.reflectanceB = buffer<float>(align(8,2*quadCount+1), 0);
        scene.reflectanceG = buffer<float>(align(8,2*quadCount+1), 0);
        scene.reflectanceR = buffer<float>(align(8,2*quadCount+1), 0);
        // index=faceCount flags miss (raycast hits no face) (i.e background "face" color)
        scene.emittanceB[2*quadCount] = 0;
        scene.emittanceG[2*quadCount] = 0;
        scene.emittanceR[2*quadCount] = 0;
        scene.reflectanceB[2*quadCount] = 0;
        scene.reflectanceG[2*quadCount] = 0;
        scene.reflectanceR[2*quadCount] = 0;
        for(size_t i: range(3)) {
            scene.X[i] = buffer<float>(align(8,2*quadCount), 0);
            scene.Y[i] = buffer<float>(align(8,2*quadCount), 0);
            scene.Z[i] = buffer<float>(align(8,2*quadCount), 0);
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
            const float gloss = 1./8;
            { // Triangle ABC
                const vec3 T = normalize(polygon[1]-polygon[0]);
                const vec3 B = normalize(polygon[2]-polygon[1]);
                const vec3 cross = ::cross(T, B);
                const float lengthCross = length(cross);
                const vec3 N = cross/lengthCross;
                const float reflect = N.z == -1;
                bgr3f color = reflect==0 ? (bgr3f(N)+bgr3f(1))/2.f : bgr3f(1, 1./2, 1./2);
                scene.faces.append({{0,1,1},{0,0,1},{T,T,T},{B,B,B},{N,N,N},reflect,0,gloss,0,0});
                for(size_t i: range(3)) {
                    scene.X[i].append(polygon[i].x-viewpoint.x);
                    scene.Y[i].append(polygon[i].y-viewpoint.y);
                    scene.Z[i].append(polygon[i].z-viewpoint.z);
                }
                if(N.y == 1 && polygon[0].y==0) {
                    scene.emittanceB.append(1);
                    scene.emittanceG.append(1);
                    scene.emittanceR.append(1);
                    scene.reflectanceB.append(1);
                    scene.reflectanceG.append(1);
                    scene.reflectanceR.append(1);
                    scene.lights.append(scene.faces.size-1);
                    scene.area.append(lengthCross/2);
                    scene.CAF.append((scene.CAF ? scene.CAF.last() : 0)+lengthCross/2);
                } else {
                    scene.emittanceB.append(0);
                    scene.emittanceG.append(0);
                    scene.emittanceR.append(0);
                    color = color / ::max(::max(color.b, color.g), color.r);
                    scene.reflectanceB.append(color.b);
                    scene.reflectanceG.append(color.g);
                    scene.reflectanceR.append(color.r);
                }
            }
            { // Triangle ACD
                const vec3 T = normalize(polygon[2]-polygon[3]);
                const vec3 B = normalize(polygon[3]-polygon[0]);
                const vec3 cross = ::cross(T, B);
                const float lengthCross = length(cross);
                const vec3 N = cross/lengthCross;
                const float reflect = N.z == -1;
                bgr3f color = reflect==0 ? (bgr3f(N)+bgr3f(1))/2.f : bgr3f(1, 1./2, 1./2);
                scene.faces.append({{0,1,0},{0,1,1},{T,T,T},{B,B,B},{N,N,N},reflect,0,gloss,0,0});
                for(size_t i: range(3)) {
                    scene.X[i].append(polygon[i?1+i:0].x-viewpoint.x);
                    scene.Y[i].append(polygon[i?1+i:0].y-viewpoint.y);
                    scene.Z[i].append(polygon[i?1+i:0].z-viewpoint.z);
                }
                if(N.y == 1 && polygon[0].y==0) {
                    scene.emittanceB.append(1);
                    scene.emittanceG.append(1);
                    scene.emittanceR.append(1);
                    scene.reflectanceB.append(1);
                    scene.reflectanceG.append(1);
                    scene.reflectanceR.append(1);
                    scene.lights.append(scene.faces.size-1);
                    scene.area.append(lengthCross/2);
                    scene.CAF.append((scene.CAF ? scene.CAF.last() : 0)+lengthCross/2);
                } else {
                    scene.emittanceB.append(0);
                    scene.emittanceG.append(0);
                    scene.emittanceR.append(0);
                    color = color / ::max(::max(color.b, color.g), color.r);
                    scene.reflectanceB.append(color.b);
                    scene.reflectanceG.append(color.g);
                    scene.reflectanceR.append(color.r);
                }
            }
        }
        assert_(scene.faces.size == 2*quadCount);
    }
    for(float& v: scene.area) v /= scene.CAF.last();
    for(float& v: scene.CAF) v /= scene.CAF.last();
    assert_(scene.CAF.last()==1);
    scene.fit();
    return scene;
}
#endif
