#include "scene.h"
#include "file.h"
#include "json.h"
#include "algorithm.h"
#include "sort.h"
#include "parse.h"
#include "image.h"
#include "jpeg.h"
#include "quad.h"

#ifdef JSON

generic inline void rotateLeft(T& a, T& b, T& c) { T t = a; a = b; b = c; c = t; }

inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }

inline const vec2 Vec2(ref<Variant> v) { return vec2((float)v[0],(float)v[1]); }
inline const vec3 Vec3(const Variant& v) {
    if(v.type == Variant::List) return vec3((float)v[0],(float)v[1],(float)v[2]);
    return vec3(v.real());
}

static const mat4 transform(const Variant& transform) {
    mat4 M;
    if(transform.type == Variant::Dict) {
        vec3 position = 0, scale = 1, look_at = 0, up = 0;
        for(const auto i: transform.dict) {
            /**/ if(i.key == "position") position = Vec3(i.value);
            else if(i.key == "look_at") look_at = Vec3(i.value);
            else if(i.key == "up") up = Vec3(i.value);
            else if(i.key == "scale") scale = Vec3(i.value);
            else error(i.key);
        }

        M[0] = vec4(scale.x,0,0,0);
        M[1] = vec4(0,scale.y,0,0);
        M[2] = vec4(0,0,scale.z,0);

        if(look_at) {
            const vec3 z = normalize(look_at - position);
            assert_(up);
            vec3 y = up;
            y = normalize(y - dot(y,z)*z); // Projects up on plane orthogonal to z
            const vec3 x = cross(y, z);
            M[0] = vec4(x, 0);
            M[1] = vec4(y, 0);
            M[2] = vec4(z, 0);
            //M.scale(1./length(position-look_at));
        }
        M[3].xyz() = position;
    } else {
        assert_(transform.type == Variant::List);
        for(uint i: range(16)) M(i/4, i%4) = transform.list[i];
    }
    return M;
}

Scene loadScene(string name, map<string,float>) {
    TextData s (readFile("scene.json", name));
    Variant root = parseJSON(s);

    array<String> materialNames;
    struct Material { rgb3f diffuseReflectance; };
    array<Material> materials;

    for(const Variant& material: root("bsdfs")) {
        materialNames.append(copyRef((string)material("name")));
        const Variant& albedo = material("albedo");
        if(albedo.type == Variant::Data) {
            if(existsFile(albedo, name)) {
                Image image = decodeImage(Map(albedo, name));
                rgb3f sum = 0;
                for(byte4 v: image) {
                    sum += rgb3f(sRGB_reverse[v.r], sRGB_reverse[v.g], sRGB_reverse[v.b]);
                }
                materials.append(Material{sum/float(image.ref::size)});
            } else {
                materials.append(Material{rgb3f(1)});
            }
        } else {
            materials.append(Material{rgb3f(Vec3(albedo)).bgr()});
        }
    }

    vec3 min = inff, max = -inff;

    array<array<Vertex>> allVertices (root("primitives").list.size);
    array<array<Quad>> allQuads (root("primitives").list.size);

    uint totalVertexCount = 0;
    uint totalQuadCount = 0;

    for(const Variant& primitive: root("primitives")) {
        if(!primitive.contains("file")) continue;
        string file = primitive("file");

        const mat4 transform = ::transform(primitive("transform"));
        const mat3 normalMatrix = transform.normalMatrix();

        const uint materialIndex = materialNames.add(copyRef(str(primitive("bsdf"))));
        if(materialIndex == materials.size) {
            rgb3f albedo = parseDecimal(parseDict(materialNames.last()).at("albedo"));
            materials.append(Material{albedo});
        }

        Map map (file, name);
        BinaryData wo3 (map);
        const uint64 vertexCount = wo3.read();
        struct VertexW03 { vec3 position, normal; vec2 uv; };
        ref<VertexW03> verticesWO3 = wo3.read<VertexW03>(vertexCount);
        const uint64 storedTriangleCount = wo3.read();
        const uint maxTriangleCount = 1204;
        if(storedTriangleCount>maxTriangleCount) continue;
        if(storedTriangleCount!=maxTriangleCount) continue; // DEBUG
        log(storedTriangleCount);
        struct Triangle { uint3 indices; uint material; };
        buffer<Triangle> triangles = copyRef(wo3.read<Triangle>(storedTriangleCount));

        buffer<uint32> usedVertices (vertexCount, 0);
        uint64 triangleCount = 0;
        for(const uint i: range(triangles.size)) {
            const uint32 P0 = triangles[i].indices[0];
            const uint32 P1 = triangles[i].indices[1];
            const uint32 P2 = triangles[i].indices[2];
            triangleCount++;
            if(!usedVertices.contains(P0)) usedVertices.append(P0);
            if(!usedVertices.contains(P1)) usedVertices.append(P1);
            if(!usedVertices.contains(P2)) usedVertices.append(P2);
        }

        buffer<uint32> indexMap (vertexCount); indexMap.clear(-1);
        buffer<Vertex> objectVertices (usedVertices.size);
        for(const uint i: range(usedVertices.size)) {
            indexMap[usedVertices[i]] = i;
            VertexW03 v = verticesWO3[usedVertices[i]];
            const vec3 p = transform*v.position;
            min = ::min(min, p);
            max = ::max(max, p);
            objectVertices[i] = {p, normalMatrix*v.normal};
        }

        array<array<uint32>> polygons (triangles.size);
#if 0
        for(Triangle triangle: triangles) {
            uint32 A0 = indexMap[triangle.indices[0]];
            uint32 A1 = indexMap[triangle.indices[1]];
            uint32 A2 = indexMap[triangle.indices[2]];

            if(0) for(array<uint32>& polygon: polygons) {
                vec3 O;
                for(int i: polygon) O += objectVertices[i].position;
                O /= polygon.size;

                // Finds polygon edge where to add point
                for(int i: range(polygon.size)) {
                    const uint32 e0 = polygon[i];
                    const uint32 e1 = polygon[(i+1)%polygon.size];
                    int p;
                    /**/ if(A0 == e1 && A1 == e0) p=A2;
                    else if(A1 == e1 && A2 == e0) p=A0;
                    else if(A2 == e1 && A0 == e0) p=A1;
                    else continue;
                    const vec3 E0 = objectVertices[e0].position;
                    const vec3 E1 = objectVertices[e1].position;
                    const vec3 P = objectVertices[p].position;
                    const float coplanar = dot(normalize(cross(E0-O, E1-O)), normalize(P-O));
                    if(abs(coplanar) < 0.0001) {
                        polygon.insertAt((i+1)%polygon.size, A2);
                        goto break2;
                    }
                }
            }
            {
                array<uint32> polygon (3);
                polygon.append(A0);
                polygon.append(A1);
                polygon.append(A2);
                polygons.append(::move(polygon));
            }
            break2:;
        }
#else
        array<Triangle> remaining = copyRef(triangles);
        while(remaining) {
            for(uint i: range(remaining.size)) {
                Triangle triangle = remaining[i];
                uint32 A0 = indexMap[triangle.indices[0]];
                uint32 A1 = indexMap[triangle.indices[1]];
                uint32 A2 = indexMap[triangle.indices[2]];

                for(array<uint32>& polygon: polygons) {
                    // Finds any common edge
                    for(int j: range(polygon.size)) {
                        const uint32 e0 = polygon[j];
                        const uint32 e1 = polygon[(j+1)%polygon.size];
                        if((A0 == e1 && A1 == e0) || (A1 == e1 && A2 == e0) || (A2 == e1 && A0 == e0)) { // Coherent winding
                            array<uint32> polygon (3);
                            polygon.append(A0);
                            polygon.append(A1);
                            polygon.append(A2);
                            polygons.append(::move(polygon));
                            remaining.removeAt(i);
                            goto break2;
                        }
                        else if((A1 == e1 && A0 == e0) || (A2 == e1 && A1 == e0) || (A0 == e1 && A2 == e0)) { // Reverse winding
                            array<uint32> polygon (3);
                            polygon.append(A2);
                            polygon.append(A1);
                            polygon.append(A0);
                            polygons.append(::move(polygon));
                            remaining.removeAt(i);
                            goto break2;
                        }
                    }
                }
            } /*else*/ { // Start new mesh
                const uint i = remaining.size-1;
                Triangle triangle = remaining[i];
                uint32 A0 = indexMap[triangle.indices[0]];
                uint32 A1 = indexMap[triangle.indices[1]];
                uint32 A2 = indexMap[triangle.indices[2]];
                array<uint32> polygon (3);
                polygon.append(A0);
                polygon.append(A1);
                polygon.append(A2);
                polygons.append(::move(polygon));
                remaining.removeAt(i);
            }
            break2:;
        }
#endif
#if 0
        while(polygon.size > 4) { // Removes point contributing the least to area
            int best = -1; float bestArea = inff;
            float totalArea = 0;
            {
                const vec3 O = vertices[polygon[0]].position;
                for(int i: range(1,polygon.size-1)) {
                    const vec3 A = vertices[polygon[i]].position;
                    const vec3 B = vertices[polygon[i+1]].position;
                    totalArea += length(cross(A-O,B-O));
                }
            }
            for(int i: range(polygon.size)) {
                const vec3 A = vertices[polygon[(i+polygon.size-1)%polygon.size]].position;
                const vec3 B = vertices[polygon[i]].position;
                const vec3 C = vertices[polygon[(i+1)%polygon.size]].position;
                const float area = length(cross(B-A,C-A)); // actually area^2
                if(area < bestArea) { bestArea=area; best=i; }
            }
            if(bestArea < 0.01*totalArea || 0) polygon.removeAt(best);
            else break;
        }
#endif

        array<Quad> quads (triangles.size);
        for(array<uint32>& polygon: polygons) {
            while(polygon.size>=3) {
                const vec3 p0 = objectVertices[polygon[0]].position;
                const vec3 p1 = objectVertices[polygon[1]].position;
                const vec3 p2 = objectVertices[polygon[2]].position;
                const vec3 p3 = objectVertices[polygon[polygon.size>=4?3:2]].position; // FIXME: triangle degenerate quad
                const float area = (sq(cross(p1-p0,p2-p0))+sq(cross(p2-p0,p3-p0)))/2;
                if(area) {
                    //assert_(polygon.size >= 3, polygon.size);
                    //log(polygon[0], polygon[1], polygon[2], polygon[3], objectVertices.size);
                    quads.append(Quad{uint4(polygon[0],polygon[1],polygon[2],polygon[polygon.size>=4?3:2]), materialIndex});
                }
                polygon.removeAt(1);
                polygon.removeAt(1);
            }
            assert_(polygon.size == 2 || polygon.size == 1, polygon.size);
        }

        //quads = decimate(quads, objectVertices);

        totalVertexCount += objectVertices.size;
        allVertices.append(::move(objectVertices));

        totalQuadCount += quads.size;
        allQuads.append(::move(quads));
    }
    assert_(allQuads.size == allVertices.size);
    //error("");

    const float near = 1/tan(19.5f/2*π/180); //2.8;
    const float far = -min.z + 0x1p-19f;
    const float originZ = ::max(ref<float>{max.x-min.x, max.y-min.y})*near/2 + max.z;
    const float scale = 2./::max(ref<float>{max.x-min.x, max.y-min.y});
    const mat4 view = mat4().scale(scale).translate(vec3(-(min.x+max.x)/2,-(min.y+max.y)/2,-originZ));

    buffer<Vertex> vertices (totalVertexCount, 0);
    log(allVertices.size);
    for(ref<Vertex> objectVertices: allVertices) {
        log(objectVertices.size);
        for(Vertex v: objectVertices) vertices.append(Vertex{view*v.position, v.normal});
    }

    buffer<Quad> quads (totalQuadCount, 0);
    {
        uint base = 0;
        for(uint objectIndex: range(allQuads.size)) {
            log(base);
            for(Quad q: allQuads[objectIndex])
                quads.append(Quad{{base+q.indices[0], base+q.indices[1], base+q.indices[2], base+q.indices[3]}, q.material});
            base += allVertices[objectIndex].size;
        }
        assert_(base == vertices.size);
    }

    Scene _ (quads.size, near, far);

    const mat4 projection = ::perspective(near, far);
    const float resolutionFactor = 32; //512;
    const uint outgoingAngleResolution = 40;
    uint samplePositionCount = 0;
    uint sampleRadianceCount = 0;

    for(size_t quadIndex: range(quads.size)) {
        const Quad quad = quads[quadIndex];
        for(uint i: range(6)) {
            //static constexpr uint quadTo2Triangles[] = {0,1,2,2,1,3};
            static constexpr uint quadTo2Triangles[] = {0,1,2,0,2,3};
            assert_(quad.indices[quadTo2Triangles[i]] < vertices.size, quad.indices[quadTo2Triangles[i]], vertices.size);
            const vec3 p = vertices[quad.indices[quadTo2Triangles[i]]].position;
            _.x[quadIndex*6+i] = p.x;
            _.y[quadIndex*6+i] = p.y;
            _.z[quadIndex*6+i] = p.z;
            _.X[i%3][quadIndex*2+i/3] = p.x;
            _.Y[i%3][quadIndex*2+i/3] = p.y;
            _.Z[i%3][quadIndex*2+i/3] = p.z;
        }
        _.diffuseReflectance[quadIndex] = materials[quad.material].diffuseReflectance;
        //_.emissiveFlux[quadIndex] = _.diffuseReflectance[quadIndex]; //FIXME 0;
        _.specularReflectance[quadIndex] = rgba4f(0,0,0,1); // Diffuse (α=1)

        const vec3 p00 (_.x[quadIndex*6+0], _.y[quadIndex*6+0], _.z[quadIndex*6+0]);
        const vec3 p01 (_.x[quadIndex*6+1], _.y[quadIndex*6+1], _.z[quadIndex*6+1]);
        const vec3 p11 (_.x[quadIndex*6+2], _.y[quadIndex*6+2], _.z[quadIndex*6+2]);
        const vec3 p10 (_.x[quadIndex*6+5], _.y[quadIndex*6+5], _.z[quadIndex*6+5]);

        /*{
            const vec3 X = p01-p00;
            const vec3 Y = p10-p00;
            const vec2 size (length(X), length(Y));
            const float area = size.x*size.y;
            _.emissiveRadiance[quadIndex].rgb() = _.emissiveFlux[quadIndex] / area;
        }*/

        uint X, Y;
#if 0 // Reduce resolution of backward facing quads (TODO: check always backward facing for all viewpoints)
        const vec3 T = p01-p00;
        const vec3 B = p10-p00;
        if(::cross(T, B).z < 0) {
            X = 1, Y = 1;
        } else
#endif
        {
            const vec2 uv00 = (projection*p00).xy();
            const vec2 uv01 = (projection*p01).xy();
            const vec2 uv11 = (projection*p11).xy();
            const vec2 uv10 = (projection*p10).xy();
            const float maxU = ::max(length(uv01-uv00), length(uv11-uv10)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uv10-uv00), length(uv11-uv01)); // Maximum projected edge length along quad's v axis
            X = uint(ceil(maxU*resolutionFactor));
            Y = uint(ceil(maxV*resolutionFactor));
            //X = align(8, X);
            //Y = align(8, Y);
            X = ::max(1u, X); Y = ::max(1u, Y);
            //assert_(X && Y, X, Y, uv00, uv01, uv11, uv10, maxU, maxV, quad.indices);
        }
        _.nX[quadIndex] = X;
        _.nY[quadIndex] = Y;

        uint Xω, Yω;
        if(_.specularReflectance[quadIndex] != rgba4f(0,0,0,1)) {
            assert_(_.specularReflectance[quadIndex].rgb() != rgb3f(0)); // Any specular reflectance
            assert_(_.specularReflectance[quadIndex].a < 1); // Not perfectly diffuse
            assert_(_.specularReflectance[quadIndex].a > 0); // Not perfectly specular
            Xω = outgoingAngleResolution;
            Yω = outgoingAngleResolution;
            assert_(Xω*Yω == 1 || Xω*Yω%8==0);
        } else {
            Xω = 1;
            Yω = 1;
        }
        _.nXω[quadIndex] = Xω;
        _.nYω[quadIndex] = Yω;
        _.sampleBase[quadIndex] = samplePositionCount;
        _.sampleRadianceBase[quadIndex] = sampleRadianceCount;
        //if(_.diffuseReflectance[quadIndex] != rgb3f(0)) // FIXME: Trim non-diffuse surface (pure emissive, specularReflectance)
        samplePositionCount += Y*X;
        sampleRadianceCount += Y*X*Yω*Xω;
        //assert_(sampleRadianceCount%8==0, X, Y, Xω, Yω);
    }
    assert_(_.x.size==_.x.capacity); //for(size_t i: range(_.x.size, _.x.capacity)) { _.x[i] = 0; _.y[i] = 0; _.z[i] = 0; }
    for(uint c: range(3)) for(size_t i: range(_.X[c].size, _.X[c].capacity)) { _.X[c][i] = 0; _.Y[c][i] = 0; _.Z[c][i] = 0; }
    assert_(sampleRadianceCount <= 563871936, sampleRadianceCount, samplePositionCount);
    _.sampleRadianceCount = sampleRadianceCount;
    _.samplePositions = buffer<vec3>(samplePositionCount);

    uint samplePositionIndex = 0;
    for(const uint i: range(quads.size)) {
        const uint4 quad = quads[i].indices;
        const vec3 p00 = vertices[quad[0]].position;
        const vec3 p01 = vertices[quad[1]].position;
        const vec3 p10 = vertices[quad[3]].position;
        const vec3 T = p01-p00;
        const vec3 B = p10-p00;
        const uint X = _.nX[i], Y = _.nY[i];
        const vec3 halfSizeT = _.halfSizeT[i] = T/float(2*X);
        const vec3 halfSizeB = _.halfSizeB[i] = B/float(2*Y);
        _.sampleArea[i] = 2*length(halfSizeT)*2*length(halfSizeB);

        _.T[i] = normalize(T);
        _.B[i] = normalize(B);
        _.N[i] = normalize(::cross(T, B));

        //if(_.specularReflectance[i].a < 1) _.specularReflectance[i].a = 0; // Forces glossy (non-diffuse) surface to pure specular
        // Roughness is approximated by filtering (FIXME: resolution/noise/bias tradeoff)
        // Renderer option to render reference and samples from same Scene

        mref<vec3> samples = _.samplePositions.slice(samplePositionIndex, Y*X);
        for(uint y: range(Y)) for(uint x: range(X)) {
            const float u = float(x+1.f/2)/float(X);
            const float v = float(y+1.f/2)/float(Y);
            vec3 O = p00 + u*T + v*B;
            samples[y*X+x] = O;
        }
        samplePositionIndex += Y*X;
    }
    return _;
}

#else

Scene loadScene(string name, map<string,float> arguments) {
    String source = readFile(name+".scene");
    for(const auto argument: arguments) source=replace(source,"$"+argument.key,str(argument.value));
    TextData s (source);
    const size_t vertexCount = parse<uint>(s);
    s.skip('\n');
    buffer<vec3> vertices (vertexCount);
    for(vec3& v: vertices) { s.whileAny(' '); v=parse<vec3>(s); s.skip('\n'); }
    const size_t quadCount = parse<uint>(s);
    s.skip('\n');

    const vec3 min = ::min(vertices);
    //const vec3 max = ::max(vertices);
    const float scale = 1; //2./::max(ref<float>{max.x-min.x, max.y-min.y/*, max.z-min.z*/});
    /*const float near = 2/(19.5*π/180); //2.8;
    const float originZ = ::max(ref<float>{max.x-min.x, max.y-min.y})*near/2 + max.z;*/
    const float near = 1/tan(19.5f/2*π/180); //2.8;
    const float originZ = 6.8f; //::max(ref<float>{max.x-min.x, max.y-min.y})*6.8/2; //::max(ref<float>{max.x-min.x, max.y-min.y})*near0/2 + max.z;
    const float far = -scale*(min.z-originZ) + 0x1p-19f /*Prevents back and far plane from Z-fighting*/;
    for(vec3& v: vertices) v = scale*(v-vec3(0,0,originZ));

    Scene _ (quadCount, near, far);

    buffer<uint4> indices (quadCount);
    for(uint i: range(quadCount)) {
        s.whileAny(' ');
        indices[i] = parse<uint4>(s);
        s.whileAny(' ');
        _.diffuseReflectance[i] = parse<rgb3f>(s)/π; // uniform diffuse reflectance = albedo/π (∫[2π](fᵣ cos)=albedo)
        s.whileAny(' ');
        _.emissiveFlux[i] = parse<rgb3f>(s);
        s.whileAny(' ');
        _.specularReflectance[i] = parse<rgba4f>(s);
        s.skip('\n');
    }

    _.quadLights = buffer<Scene::QuadLight>(1);
    {
        const vec3 p00 = vertices[indices[0][0]];
        const vec3 p01 = vertices[indices[0][1]];
        const vec3 p10 = vertices[indices[0][3]];
        const vec3 X = p01-p00;
        const vec3 Y = p10-p00;
        const vec2 size (length(X), length(Y));
        const vec3 T = X/size.x;
        const vec3 B = Y/size.y;
        //const vec3 area = ::cross(T, B);
        const vec3 N = /*normalize*/(cross(T, B)); // normalize Just to be Sure™ ?
        _.quadLights[0] = Scene::QuadLight{p00, T, B, N, size, _.emissiveFlux[0]};
    }

    const mat4 M = shearedPerspective(0, 0, near, far);
    const float resolutionFactor = 512;
    const uint outgoingAngleResolution = 1; //40;
    uint samplePositionCount = 0;
    uint sampleRadianceCount = 0;
    for(const uint quadIndex: range(indices.size)) {
        const uint4 quad = indices[quadIndex];
        for(uint i: range(6)) {
            static constexpr uint indices[] = {0,1,2,0,2,3};
            const vec3 v = vertices[quad[indices[i]]];
            _.x[quadIndex*6+i] = v.x;
            _.y[quadIndex*6+i] = v.y;
            _.z[quadIndex*6+i] = v.z;
            _.X[i%3][quadIndex*2+i/3] = v.x;
            _.Y[i%3][quadIndex*2+i/3] = v.y;
            _.Z[i%3][quadIndex*2+i/3] = v.z;
        }
        const vec3 p00 = vertices[quad[0]];
        const vec3 p01 = vertices[quad[1]];
        const vec3 p11 = vertices[quad[2]];
        const vec3 p10 = vertices[quad[3]];

        {
            const vec3 X = p01-p00;
            const vec3 Y = p10-p00;
            const vec2 size (length(X), length(Y));
            const float area = size.x*size.y;
            _.emissiveRadiance[quadIndex].rgb() = _.emissiveFlux[quadIndex] / area;
        }

        uint X, Y;
#if 0 // Reduce resolution of backward facing quads (TODO: check always backward facing for all viewpoints)
        const vec3 T = p01-p00;
        const vec3 B = p10-p00;
        if(::cross(T, B).z < 0) {
            X = 1, Y = 1;
        } else
#endif
        {
            const vec2 uv00 = (M*p00).xy();
            const vec2 uv01 = (M*p01).xy();
            const vec2 uv11 = (M*p11).xy();
            const vec2 uv10 = (M*p10).xy();
            const float maxU = ::max(length(uv01-uv00), length(uv11-uv10)); // Maximum projected edge length along quad's u axis
            const float maxV = ::max(length(uv10-uv00), length(uv11-uv01)); // Maximum projected edge length along quad's v axis
            X = uint(ceil(maxU*resolutionFactor));
            Y = uint(ceil(maxV*resolutionFactor));
            X = align(8, X);
            Y = align(8, Y);
            //X = ::max(1u, X); Y = ::max(1u, Y);
            assert_(X && Y, X, Y);
        }
        _.nX[quadIndex] = X;
        _.nY[quadIndex] = Y;

        uint Xω, Yω;
        if(_.specularReflectance[quadIndex] != rgba4f(0,0,0,1)) {
            assert_(_.specularReflectance[quadIndex].rgb() != rgb3f(0)); // Any specular reflectance
            assert_(_.specularReflectance[quadIndex].a < 1); // Not perfectly diffuse
            assert_(_.specularReflectance[quadIndex].a > 0); // Not perfectly specular
            Xω = outgoingAngleResolution;
            Yω = outgoingAngleResolution;
            assert_(Xω*Yω == 1 || Xω*Yω%8==0);
        } else {
            Xω = 1;
            Yω = 1;
        }
        assert_(Xω*Yω < 1600);
        _.nXω[quadIndex] = Xω;
        _.nYω[quadIndex] = Yω;
        _.sampleBase[quadIndex] = samplePositionCount;
        _.sampleRadianceBase[quadIndex] = sampleRadianceCount;
        //if(_.diffuseReflectance[quadIndex] != rgb3f(0)) // FIXME: Trim non-diffuse surface (pure emissive, specularReflectance)
        samplePositionCount += Y*X;
        sampleRadianceCount += Y*X*Yω*Xω;
        assert_(sampleRadianceCount%8==0, X, Y, Xω, Yω);
    }
    assert_(_.x.size==_.x.capacity); //for(size_t i: range(_.x.size, _.x.capacity)) { _.x[i] = 0; _.y[i] = 0; _.z[i] = 0; }
    for(uint c: range(3)) for(size_t i: range(_.X[c].size, _.X[c].capacity)) { _.X[c][i] = 0; _.Y[c][i] = 0; _.Z[c][i] = 0; }
    assert_(sampleRadianceCount <= 563871936, sampleRadianceCount, samplePositionCount);
    _.sampleRadianceCount = sampleRadianceCount;
    _.samplePositions = buffer<vec3>(samplePositionCount);

    uint samplePositionIndex = 0;
    for(const uint i: range(indices.size)) {
        const uint4 quad = indices[i];
        const vec3 p00 = vertices[quad[0]];
        const vec3 p01 = vertices[quad[1]];
        const vec3 p10 = vertices[quad[3]];
        const vec3 T = p01-p00;
        const vec3 B = p10-p00;
        const uint X = _.nX[i], Y = _.nY[i];
        const vec3 halfSizeT = _.halfSizeT[i] = T/float(2*X);
        const vec3 halfSizeB = _.halfSizeB[i] = B/float(2*Y);
        _.sampleArea[i] = 2*length(halfSizeT)*2*length(halfSizeB);

        _.T[i] = normalize(T);
        _.B[i] = normalize(B);
        _.N[i] = normalize(::cross(T, B));

        //if(_.specularReflectance[i].a < 1) _.specularReflectance[i].a = 0; // Forces glossy (non-diffuse) surface to pure specular
        // Roughness is approximated by filtering (FIXME: resolution/noise/bias tradeoff)
        // Renderer option to render reference and samples from same Scene

        mref<vec3> samples = _.samplePositions.slice(samplePositionIndex, Y*X);
        for(uint y: range(Y)) for(uint x: range(X)) {
            const float u = float(x+1.f/2)/float(X);
            const float v = float(y+1.f/2)/float(Y);
            vec3 O = p00 + u*T + v*B;
            samples[y*X+x] = O;
        }
        samplePositionIndex += Y*X;
    }

    return _;
}
#endif
