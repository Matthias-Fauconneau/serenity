#include "raster.h"
// Z interpolation, MSAA, vertex attribute interpolation, perspective correct interpolation, sRGB, tile-based deferred rendering

void Rasterizer::clear() {
    ::clear(zbuffer,width*height,-1.f);
    ::clear(framebuffer,width*height,vec4(1,1,1,0));
}

template<uint N> inline void Rasterizer::polygon(vec3 polygon[N], const Shader& shader) {
    vec2 min=polygon[0].xy(),max=polygon[0].xy();
    float lines[N][3]; // cross(P-A,B-A) > 0 <=> (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1) > 0 <=> δx*y + δy*x > c (with c=y1*δx-x1*δy)
    for(int i: range(N-1)) {
        min=::min(min,polygon[i+1].xy()), max=::max(max,polygon[i+1].xy());
        lines[i][0] = polygon[i+1].x-polygon[i].x;
        lines[i][1] = polygon[i+1].y-polygon[i].y;
        lines[i][2] = polygon[i].y*lines[i][0] - polygon[i].x*lines[i][1];
        if(lines[i][1]>0 || (lines[i][1]==0 && lines[i][0]<0)) lines[i][2]++; //top-left fill rule
        float l = sqrt(lines[i][0]*lines[i][0]+lines[i][1]*lines[i][1]);
        lines[i][0]/=l, lines[i][1]/=l, lines[i][2]/=l; // normalize distance equation (for line smoothing)
    }
    lines[N-1][0] = polygon[0].x-polygon[N-1].x;
    lines[N-1][1] = polygon[0].y-polygon[N-1].y;
    lines[N-1][2] = polygon[N-1].y*lines[N-1][0] - polygon[N-1].x*lines[N-1][1];
    min = ::max(min,vec2(0,0)), max=::min(max,vec2(width,height));
    float z=0; for(int i: range(N)) z+=polygon[i].z; z/=N; //TODO: Z interpolation
    for(float y=floor(min.y); y<max.y; y++) for(float x=floor(min.x); x<max.x; x++) {
        for(uint i=0; i<N; i++) {
            float d = lines[i][0]*y-lines[i][1]*x-lines[i][2];
            if(d>1./2) goto done; //outside
        }
        for(uint i=0; i<N; i++) { //smooth edges (TODO: correct HSR using Z-Buffer multisampling)
            float d = lines[i][0]*y-lines[i][1]*x-lines[i][2];
            if(d>-1./2) {
                d +=1./2; assert(d>=0 && d<=1);
                if(1-d>0.5 && ztest(x,y,z)) output(x,y,z,vec4(shader(vec3(x,y,0)).xyz(),1-d));
                goto done;
            }
        }
        if(ztest(x,y,z)) output(x,y,z,shader(vec3(x,y,0))); //completely inside
        done:;
    }
}

template void Rasterizer::polygon<3>(vec3 polygon[3], const Shader& shader);
template void Rasterizer::polygon<4>(vec3 polygon[4], const Shader& shader);

void Rasterizer::circle(vec3 A, float r, const Shader& shader) {
    A+=vec3(1./2,1./2,0);
    float z=A.z;
    for(float y=A.y-r-1; y<A.y+r+1; y++) for(float x=A.x-r-1; x<A.x+r+1; x++) {
        float d = length(vec2(x,y)-A.xy())-r;
        if(d<-1./2) {
            if(ztest(x,y,z)) output(x,y,z,shader(vec3(x,y,0)));
        } else if(d<1./2) {
            d +=1./2;
            if(1-d>0.5 && ztest(x,y,z)) output(x,y,z,vec4(shader(vec3(x,y,0)).xyz(),1-d));
        }
    }
}

void Rasterizer::line(vec3 A, vec3 B, float wa, float wb, const Shader& shader) {
    vec2 T = B.xy()-A.xy();
    float l = length(T);
    if(l<0.01) return;
    vec2 N = normal(T)/l;
    quad(vec3(A.xy()+N*(wa/2),A.z),vec3(B.xy()+N*(wb/2),B.z),vec3(B.xy()-N*(wb/2),B.z),vec3(A.xy()-N*(wa/2),A.z),shader);
}

//TODO: MSAA, sRGB
void Rasterizer::resolve(int2 position, int2 unused size) {
    assert(size.x<=width && size.y<=height);
    int x0=position.x, y0=position.y;
    for(int y=0; y<size.y; y++) for(int x=0; x<size.x; x++) {
        ::framebuffer(x0+x,y0+size.y-1-y)=byte4(255.f*framebuffer[y*width+x]);
    }
}
