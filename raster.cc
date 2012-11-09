#include "raster.h"
#include "matrix.h"

struct sRGBLookup {
    uint8 lookup[256];
    inline float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4)-0.055; else return 12.92*c; }
    sRGBLookup() { for(int i=0;i<256;i++) { int l = round(255*sRGB(i/255.f)); assert(l>=0 && l<256); lookup[i]=l; } }
    inline uint8 operator [](float c) { assert(round(c)>=0 && round(c)<256,c); return lookup[round(c)%256]; }
} sRGBLookup;

static constexpr int MSAA=2; // multisample antialiasing

Rasterizer::Rasterizer(int width, int height) : width(width),height(height),stride(width*MSAA),
    zbuffer(allocate16<float>(width*height*MSAA*MSAA)),
    framebuffer(allocate16<vec4>(width*height*MSAA*MSAA)){}

Rasterizer::~Rasterizer() { unallocate(zbuffer,width*height*MSAA*MSAA); unallocate(framebuffer,width*height*MSAA*MSAA); }

void Rasterizer::clear(vec4 color, float depth) {
    ::clear(zbuffer,width*height*MSAA*MSAA,depth);
    ::clear(framebuffer,width*height*MSAA*MSAA,color);
}

template<int N> void Rasterizer::triangle(const Shader<N>& shader, vec4 A, vec4 B, vec4 C, vec3 attributes[N]) {
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
    vec3 varyings[N];
    for(int i=0;i<N;i++) varyings[i] = attributes[i]*M;

    vec2 min = ::max(vec2(0,0),::min(::min(A.xy(),B.xy()),C.xy()));
    vec2 max = ::min(vec2(width-1,height-1),::max(::max(A.xy(),B.xy()),C.xy()));
    for(float Y=floor(min.y); Y<=max.y; Y++) for(float X=floor(min.x); X<=max.x; X++) {
        uint mask=0, bit=1;
        float centroid[N] = {}; float samples=0;
        for(float y=Y+1./(2*MSAA); y<Y+1; y+=1./MSAA) for(float x=X+1./(2*MSAA); x<X+1; x+=1./MSAA) {
            vec3 XY1(x,y,1);
            float d0 = dot(e0,XY1), d1 = dot(e1,XY1), d2 = dot(e2,XY1);
            if(d0>0 && d1>0 && d2>0) {
                float w = 1/dot(iw,XY1);
                float z = w*dot(zw,XY1);

                float& Z = zbuffer[int(y*MSAA)*stride+int(x*MSAA)];
                if(z>=Z) {
                    Z = z;
                    samples++;
                    mask |= bit;
                    for(int i=0;i<N;i++) centroid[i]+=w*dot(varyings[i],XY1);
                }
            }
            bit <<= 1;
        }
        if(mask) {
            for(int i=0;i<N;i++) centroid[i] /= samples;
            vec4 bgra = shader(centroid);
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
}
template void Rasterizer::triangle<1>(const Shader<1>& shader, vec4 A, vec4 B, vec4 C, vec3 d[1]);

inline byte4 sRGB(vec4 c) { return byte4(sRGBLookup[c.x],sRGBLookup[c.y],sRGBLookup[c.z],c.w); }
void Rasterizer::resolve(int2 position, int2 unused size) {
    assert(size.x>=width && size.y>=height);
    int x0=position.x, y0=position.y;
    for(int y=0; y<height; y++) for(int x=0; x<width; x++) {
        vec4* s = &framebuffer[(y*MSAA)*stride+x*MSAA];
        vec4 sum=0;
        for(int i=0;i<MSAA;i++) for(int j=0;j<MSAA;j++) sum+=s[i*stride+j];
        sum *= 255.f/(MSAA*MSAA); //normalize to [0-255]
        ::framebuffer(x0+x,y0+(height-1-y))=sRGB(sum);
    }
}
