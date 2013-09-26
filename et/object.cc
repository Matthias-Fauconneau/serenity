#include "object.h"

uint Surface::addVertex(Vertex v) {
    assert(!vertexBuffer);
    if(!vertices) bbMin=bbMax=v.position; else bbMin=min(bbMin,v.position), bbMax=max(bbMax,v.position); // Initializes or updates bounding box
    vertices<<v; return vertices.size-1; // Appends vertex
}

void Surface::addTriangle(const ref<Vertex>& sourceVertices, int i1, int i2, int i3) {
    if(!indexMap.contains(i1)) indexMap[i1]=addVertex(sourceVertices[i1]);
    if(!indexMap.contains(i2)) indexMap[i2]=addVertex(sourceVertices[i2]);
    if(!indexMap.contains(i3)) indexMap[i3]=addVertex(sourceVertices[i3]);
    Vertex& v1 = vertices[indexMap[i1]];
    Vertex& v2 = vertices[indexMap[i2]];
    Vertex& v3 = vertices[indexMap[i3]];

    vec2 p[3]; for(int i: range(3)) p[i] = vec2(v2.position[i] - v1.position[i], v3.position[i] - v1.position[i]);
    vec2 s(v2.texcoord.x - v1.texcoord.x, v3.texcoord.x - v1.texcoord.x);
    vec2 t(v2.texcoord.y - v1.texcoord.y, v3.texcoord.y - v1.texcoord.y);
    float sign = cross(s,t)<0?-1:1;
    vec3 tangent(   cross(t,p[0]) * sign,  cross(t,p[1]) * sign,  cross(t,p[2]) * sign );
    vec3 bitangent(-cross(s,p[0]) * sign, -cross(s,p[1]) * sign, -cross(s,p[2]) * sign );

    v1.tangent += tangent, v2.tangent += tangent, v3.tangent += tangent;
    v1.bitangent += bitangent, v2.bitangent += bitangent, v3.bitangent += bitangent;
    indices << indexMap[i1] << indexMap[i2] << indexMap[i3];
}

void Surface::draw(GLShader& program,bool withTexcoord,bool withNormal, bool /*withAlpha*/,bool withTangent) {
    if(!vertexBuffer) {
        indexMap.clear();
        indexBuffer.upload(indices);

        for(Vertex& v: vertices) { // Projects tangents on normal plane
            v.tangent = normalize(v.tangent - v.normal * dot(v.normal, v.tangent));
            v.bitangent = normalize(v.bitangent - v.normal * dot(v.normal, v.bitangent));
        }
        vertexBuffer.upload(vertices);
        //log(indexBuffer.indexCount/3,"triangles");
    }
    #define offsetof __builtin_offsetof
    vertexBuffer.bindAttribute(program, "position"_, 3, offsetof(Vertex,position));
    if(withTexcoord) vertexBuffer.bindAttribute(program, "texcoord"_, 2, offsetof(Vertex,texcoord));
    if(withNormal) vertexBuffer.bindAttribute(program, "normal"_, 3, offsetof(Vertex,normal));
    /*if(withAlpha)*/ vertexBuffer.bindAttribute(program, "alpha"_, 1, offsetof(Vertex,alpha));
    if(withTangent) {
        vertexBuffer.bindAttribute(program, "tangent"_, 3, offsetof(Vertex,tangent));
        vertexBuffer.bindAttribute(program, "bitangent"_, 3, offsetof(Vertex,bitangent));
    }
    indexBuffer.draw();
}


static bool intersect(vec3 min, vec3 max, vec3 O, vec3 D, float& t) { //from "An Efﬁcient and Robust Ray–Box Intersection Algorithm"
    if(O>min && O<max) return true;
    float tmin = ((D.x >= 0?min:max).x - O.x) / D.x;
    float tmax = ((D.x >= 0?max:min).x - O.x) / D.x;
    float tymin= ((D.y >= 0?min:max).y - O.y) / D.y;
    float tymax= ((D.y >= 0?max:min).y - O.y) / D.y;
    if( (tmin > tymax) || (tymin > tmax) ) return false;
    if(tymin>tmin) tmin=tymin; if(tymax<tmax) tmax=tymax;
    float tzmin= ((D.z >= 0?min:max).z - O.z) / D.z;
    float tzmax= ((D.z >= 0?max:min).z - O.z) / D.z;
    if( (tmin > tzmax) || (tzmin > tmax) ) return false;
    if(tzmin>tmin) tmin=tzmin; if(tzmax<tmax) tmax=tzmax;
    if(tmax <= 0) return false; t=tmax;/*tmax is nearest point*/ return true;
}
static bool intersect( vec3 A, vec3 B, vec3 C, vec3 O, vec3 D, float& t ) { //from "Fast, Minimum Storage Ray/Triangle Intersection"
    if(dot(A-O,D)<=0 && dot(B-O,D)<=0 && dot(C-O,D)<=0) return false;
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross( D, edge2 );
    float det = dot( edge1, pvec );
    if( det < 0.000001 ) return false;
    vec3 tvec = O - A;
    float u = dot(tvec, pvec);
    if(u < 0 || u > det) return false;
    vec3 qvec = cross( tvec, edge1 );
    float v = dot(D, qvec);
    if(v < 0 || u + v > det) return false;
    t = dot(edge2, qvec);
    t /= det;
    return true;
}
bool Surface::raycast(vec3 O,vec3 D, float& minZ) {
    float z=0;
    if(!intersect(bbMin,bbMax,O,D,z) || z>=minZ) return false;
    bool hit=false;
    for(uint i=0;i<indices.size;i+=3) {
        float z;
        if(intersect(vertices[indices[i]].position,vertices[indices[i+1]].position,vertices[indices[i+2]].position,O,D,z)) {
            if(z<minZ) { minZ=z; hit=true; }
        }
    }
    return hit;
}
