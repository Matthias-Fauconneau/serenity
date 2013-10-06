#include "object.h"

Surface::Surface(Shader& shader, VertexBuffer& vertices, array<uint>&& indices) : shader(shader), vertices(vertices), indices(move(indices)) {
    if(this->indices) bbMin=bbMax=vertices[this->indices.first()].position; // Initializes bounding box
    for(uint i: this->indices) { vec3 p = vertices[i].position; bbMin=min(bbMin,p), bbMax=max(bbMax,p); } // Computes bounding box
}

void Surface::addTriangle(int a, int b, int c) {
    if(!indices)  bbMin=bbMax=vertices[a].position;
    indices.reserve(indices.size+3);
    {vec3 p = vertices[a].position; bbMin=min(bbMin,p), bbMax=max(bbMax,p); indices<<a;}
    {vec3 p = vertices[b].position; bbMin=min(bbMin,p), bbMax=max(bbMax,p); indices<<b;}
    {vec3 p = vertices[c].position; bbMin=min(bbMin,p), bbMax=max(bbMax,p); indices<<c;}
}

void Surface::draw(GLShader& program) {
    if(!vertices.id) vertices.upload(vertices);
    if(!indexBuffer.id) indexBuffer.upload(indices);
    vertices.bindAttribute(program, "position"_, 3, offsetof(Vertex,position));
    vertices.bindAttribute(program, "alpha"_, 1, offsetof(Vertex,alpha));
    vertices.bindAttribute(program, "texcoord"_, 2, offsetof(Vertex,texcoord));
    vertices.bindAttribute(program, "normal"_, 3, offsetof(Vertex,normal));
    vertices.bindAttribute(program, "lightmap"_, 2, offsetof(Vertex,lightmap));
    vertices.bindAttribute(program, "color"_, 3, offsetof(Vertex,color));
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
bool Surface::intersect(vec3 O,vec3 D, float& minZ) {
    float z=0;
    if(!::intersect(bbMin,bbMax,O,D,z) || z>=minZ) return false;
    bool hit=false;
    for(uint i=0;i<indices.size;i+=3) {
        float z;
        if(::intersect(vertices[indices[i]].position,vertices[indices[i+1]].position,vertices[indices[i+2]].position,O,D,z)) {
            if(z<minZ) { minZ=z; hit=true; }
        }
    }
    return hit;
}
