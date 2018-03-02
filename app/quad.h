#pragma once
#include "vector.h"

struct Vertex { vec3 position, normal; };
struct Quad { uint4 indices; uint material; };

template<> inline String str(const Quad& q) { return str(q.indices); }

buffer<Quad> decimate(ref<Quad> quads_, ref<Vertex> vertices) {
    buffer<Quad> quads = unsafeRef(quads_);
    for(;;) { // FIXME: before object merge
        buffer<Quad> low (quads.size, 0);
        buffer<int> removedVertices (vertices.size); removedVertices.clear(0);
        buffer<int> removedQuads (quads.size); removedQuads.clear(0);
        for(uint32 v: range(vertices.size)) {
            if(removedVertices[v]) continue;
            entry<uint, uint4> faces[4 +1]; uint faceCount = 0;
            uint material;
            for(uint q: range(quads.size)) {
                if(removedQuads[q]) continue;
                uint4 Q = quads[q].indices;
                /**/ if(Q[0] == v) faces[faceCount++] = {q, {Q[0], Q[1], Q[2], Q[3]}};
                else if(Q[1] == v) faces[faceCount++] = {q, {Q[1], Q[2], Q[3], Q[0]}};
                else if(Q[2] == v) faces[faceCount++] = {q, {Q[2], Q[3], Q[0], Q[1]}};
                else if(Q[3] == v) faces[faceCount++] = {q, {Q[3], Q[0], Q[1], Q[2]}};
                else continue;
                if(faceCount == 5) break;
                if(faceCount==1) material = quads[q].material; // Material of first match
                else assert_(quads[q].material == material);
            }
            if(faceCount == 4) {
                //for(uint i: range(4)) log(faces[i].value);
                uint4 sorted[4]; // Follow winding order
                sorted[0] = faces[0].value;
                for(uint i: range(1, 4)) {
                    for(auto q: ref<entry<uint, uint4>>(faces+1, 3)) {
                        if(sorted[i-1][3] == q.value[1]) {
                            sorted[i] = q.value;
                            goto break_;
                        }
                    } /*else*/ goto break__; // Not connected //error(sorted[i-1][3]);
                    break_:;
                }
                //log(sorted);
                /*for(uint i: range(2)) {
                const vec3 A = vertices[sorted[i][2]].position;
                const vec3 B = vertices[sorted[0][0]].position;
                const vec3 C = vertices[sorted[(i+2)%4][2]].position;
                const float cos = sq(dot(B-A, C-B)) / (sq(B-A)*sq(C-B));
                //error(cos);
                if(cos <= 0) goto break_;
            }*/
                /*else*/ {
                    for(uint i: range(4)) {
                        const vec3 A = vertices[sorted[(i+4-1)%4][2]].position;
                        const vec3 B = vertices[sorted[i][1]].position;
                        const vec3 C = vertices[sorted[i][2]].position;
                        const float cos = sq(dot(B-A, C-B)) / (sq(B-A)*sq(C-B));
                        //error(cos);
                        if(cos <= 0) goto break__;
                    }
                    /*else*/ if(1) {
                        removedVertices[faces[0].value[0]] = 1; // 4x
                        removedVertices[faces[0].value[1]] = 1; // 2x
                        removedVertices[faces[1].value[1]] = 1; // 2x
                        removedVertices[faces[2].value[1]] = 1; // 2x
                        removedVertices[faces[3].value[1]] = 1; // 2x
                        removedQuads[faces[0].key] = 1;
                        removedQuads[faces[1].key] = 1;
                        removedQuads[faces[2].key] = 1;
                        removedQuads[faces[3].key] = 1;

                        low.append(Quad{{sorted[0][2],sorted[1][2],sorted[2][2],sorted[3][2]}, material});
                    }
                }
                break__:;
            }
        }
        uint removed = 0;
        for(uint q: range(quads.size)) {
            if(removedQuads[q]) { removed++; continue; }
            low.append(quads[q]);
        }
        log(removed, quads.size);
        // FIXME: append non decimated faces
        quads = ::move(low);
        if(!removed) break;
    }
    return quads;
}
