#include "object.h"

uint Surface::addVertex(Vertex v) {
    assert(!vertexBuffer);
    if(!vertices) bbMin=bbMax=v.position; else bbMin=min(bbMin,v.position), bbMax=max(bbMax,v.position); // Initializes or updates bounding box
    vertices<<v; return vertices.size-1; // Appends vertex
}

void Surface::addTriangle(const ref<Vertex>& sourceVertices, int i1, int i2, int i3) {
    if(!indexMap.contains(i1)) indexMap[i1]=addVertex(sourceVertices[i1]);
    if(!indexMap.contains(i2)) indexMap[i2]=addVertex(sourceVertices[i2]);
    if(!indexMap.contains(i3)) indexMap[i3]=addVertex(sourceVertices[i2]);
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
    //if(withTexcoord) vertexBuffer.bindAttribute(program, "texcoord"_, 2, offsetof(Vertex,texcoord));
    //if(withNormal) vertexBuffer.bindAttribute(program, "normal"_, 3, offsetof(Vertex,normal));
    /*if(withAlpha)*/ vertexBuffer.bindAttribute(program, "alpha"_, 1, offsetof(Vertex,alpha));
    /*if(withTangent) {
        vertexBuffer.bindAttribute(program, "tangent"_, 3, offsetof(Vertex,tangent));
        vertexBuffer.bindAttribute(program, "bitangent"_, 3, offsetof(Vertex,bitangent));
    }*/
    indexBuffer.draw();
}
