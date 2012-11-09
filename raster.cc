#include "raster.h"
#include "matrix.h"

static constexpr int MSAA=2; // multisample antialiasing

Rasterizer::Rasterizer(int width, int height) : width(width),height(height),stride(width*MSAA),
    zbuffer(allocate16<float>(width*height*MSAA*MSAA)),
    framebuffer(allocate16<vec4>(width*height*MSAA*MSAA)){}

Rasterizer::~Rasterizer() { unallocate(zbuffer,width*height*MSAA*MSAA); unallocate(framebuffer,width*height*MSAA*MSAA); }

void Rasterizer::clear() {
    ::clear(zbuffer,width*height*MSAA*MSAA,-0x1p16f);
    ::clear(framebuffer,width*height*MSAA*MSAA,vec4(1,1,1,0));
}

inline void Rasterizer::shade(float X, float Y, uint mask, const Shader& shader) {
    if(mask) {
        vec4 bgra = shader(X+0.5,Y+0.5); //TODO: centroid
        float a = bgra.w;
        bgra *= a;
        vec4* d = &framebuffer[int(Y*MSAA)*stride+int(X*MSAA)];
        if(mask==(1<<(MSAA*MSAA))-1) { //TODO: color compression
            for(int i=0;i<MSAA;i++) for(int j=0;j<MSAA;j++) {
                vec4& s = d[i*stride+j];
                s=s*(1-a)+bgra;
            }
        } else {
            uint bit=1;
            for(int i=0;i<MSAA;i++) for(int j=0;j<MSAA;j++) {
                if(mask&bit) {
                    vec4& s = d[i*stride+j];
                    s=s*(1-a)+bgra;
                }
                bit <<= 1;
            }
        }
    }
}

void Rasterizer::triangle(vec4 A, vec4 B, vec4 C, const Shader& shader) {
    assert(A.w==1); assert(B.w==1); assert(C.w==1);
    mat3 M = mat3(A.xyw(), B.xyw(), C.xyw());
    float det = M.det();
    if(det<=0.01) return; //small or back-facing triangle
    M = M.adjugate();

    // Interpolation functions (-dy, dx, d)
    vec3 e0 = vec3(1,0,0)*M; if(e0.x>0/*dy<0*/ || (e0.x==0/*dy=0*/ && e0.y<0/*dx<0*/)) e0.z++;
    vec3 e1 = vec3(0,1,0)*M; if(e1.x>0/*dy<0*/ || (e1.x==0/*dy=0*/ && e1.y<0/*dx<0*/)) e1.z++;
    vec3 e2 = vec3(0,0,1)*M; if(e2.x>0/*dy<0*/ || (e2.x==0/*dy=0*/ && e2.y<0/*dx<0*/)) e2.z++;
    vec3 iw = vec3(1,1,1)*M;
    vec3 zw = vec3(A.z,B.z,C.z)*M;

    vec2 min = ::max(vec2(0,0),::min(::min(A.xy(),B.xy()),C.xy()));
    vec2 max = ::min(vec2(width-1,height-1),::max(::max(A.xy(),B.xy()),C.xy()));
    for(float Y=floor(min.y); Y<=max.y; Y++) for(float X=floor(min.x); X<=max.x; X++) {
        uint mask=0, bit=1;
        for(float y=Y+1./(2*MSAA); y<Y+1; y+=1./MSAA) for(float x=X+1./(2*MSAA); x<X+1; x+=1./MSAA) {
            vec3 XY1(x,y,1);
            float d0 = dot(e0,XY1), d1 = dot(e1,XY1), d2 = dot(e2,XY1);
            if(d0>0 && d1>0 && d2>0) {
                float w = 1/dot(iw,XY1);
                float z = w*dot(zw,XY1);

                float& Z = zbuffer[int(y*MSAA)*stride+int(x*MSAA)];
                if(z>=Z) { Z = z; mask|=bit; }
            }
            bit <<= 1;
        }
        shade(X, Y, mask, shader);
    }
}

void Rasterizer::circle(vec4 A, float r, const Shader& shader) {
    A/=A.w; r/=A.w;
    float z=A.z-r;
    for(float Y=floor(A.y-r); Y<=A.y+r; Y++) for(float X=floor(A.x-r); X<=A.x+r; X++) {
        uint mask=0, bit=1;
        for(float y=Y+1./(2*MSAA); y<Y+1; y+=1./MSAA) for(float x=X+1./(2*MSAA); x<X+1; x+=1./MSAA) {
            float d = length(vec2(x,y)-A.xy())-r;
            if(d<0) {
                float& Z = zbuffer[int(y*MSAA)*stride+int(x*MSAA)];
                if(z>=Z) { Z = z; mask |= bit; }
            }
            bit <<= 1;
        }
        shade(X, Y, mask, shader);
    }
}

void Rasterizer::line(vec4 A, vec4 B, float wa, float wb, const Shader& shader) {
    vec2 T = B.xy()-A.xy();
    float l = length(T);
    if(l<0.01) return;
    vec2 N = normal(T)/l;
    quad(
                vec4(A.xy() - N*(wa/2), A.z, 1.f),
                vec4(B.xy() - N*(wb/2), B.z, 1.f),
                vec4(B.xy() + N*(wb/2), B.z, 1.f),
                vec4(A.xy() + N*(wa/2), A.z, 1.f),
                shader);
}

inline float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4)-0.055; else return 12.92*c; }
inline vec4 sRGB(vec4 c) { return vec4(sRGB(c.x),sRGB(c.y),sRGB(c.z),c.w); }
void Rasterizer::resolve(int2 position, int2 unused size) {
    assert(size.x>=width && size.y>=height);
    int x0=position.x, y0=position.y;
    for(int y=0; y<height; y++) for(int x=0; x<width; x++) {
        vec4* s = &framebuffer[(y*MSAA)*stride+x*MSAA];
#if 0
        for(int i=0;i<MSAA;i++) for(int j=0;j<MSAA;j++) {
            const int upsample=16;
            for(int k=0;k<upsample-1;k++) for(int l=0;l<upsample-1;l++)
                ::framebuffer(x0+(x*MSAA+j)*upsample+k,y0+(height*MSAA-1-(y*MSAA+i))*upsample+l)=byte4(255.f*s[i*stride+j]);
        }
#else
        vec4 sum=0;
        for(int i=0;i<MSAA;i++) for(int j=0;j<MSAA;j++) sum+=s[i*stride+j];
        ::framebuffer(x0+x,y0+(height-1-y))=byte4(255.f*sRGB(sum/float(MSAA*MSAA)));
#endif
    }
}
