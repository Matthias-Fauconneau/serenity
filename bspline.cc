#include "process.h"
#include "window.h"
#include "file.h"
#include "image.h"
#include "matrix.h"

extern Image framebuffer;

template <typename S, typename D> inline void prefilter(const S* src, D* dst, int w, int h) {
  const float Zp = sqrt(3)-2; //only pole for Z-Transform of cubic spline
  const float lambda = (1 - Zp) * (1 - (1 / Zp)); //gain to normalize the coefficients
  const int horizon = min(8,w); //8 pixel is enough precision for float precision
  for(int y=0; y<h; y++) {
    float4 c[w]; //a line of float coefficients
    float Zn = Zp; //will store Zp^x
    const S* const s = &src[y*w]; //source line
    c[0] = float4(s[0]);
    for(int x=0; x<horizon; x++) { //initialize filter with mirror condition
        c[0] += Zn * float4(s[x]);
        Zn *= Zp;
    }
    c[0] *= lambda; //normalize
    for(int x=1; x<w; x++) c[x] = lambda * float4(s[x]) + Zp * c[x-1]; //run the causal filter
    c[w-1] = Zp / (Zp-1) * c[w-1]; //initialize anticausal filter (mirror conditions)
    for(int x=w-2; x>=0; x--) c[x] = Zp * ( c[x+1] - float4(c[x]) ); //run anticausal filter (last pixel (w-1) is initialized above)
    D* const d = &dst[y]; //destination column
    for(int x=w-1; x>=0; x--) { //copy coefficient line to output column (i.e transposed store)
        if(sizeof(D)==sizeof(byte4)) //static_if(D==byte4)
            d[x*w] = D(clip(0.f, c[x], 255.f));
        else
            d[x*w] = D(c[x]);
    }
  }
}

struct BSplineDemo : Application, Widget {
    Window window{*this,int2(512,512),"BSpline"_};
    Image image, filtered;
    enum Filter { Nearest, Bilinear, CatmullRom, MitchellNetravali, NoPrefilter, CubicBSpline } filter = CubicBSpline;
    void start(array<string>&& args) override {
        assert(args,"Usage: bspline image.png"_);
        window.rename(args[0]);
        image = Image(readFile(args[0]));
        int w=image.width, h=image.height;
        float4 buffer[w*h]; // use full precision buffer between filtering pass
        prefilter(image.data,buffer,w,h); //filter X and upsample to float
        filtered = Image(w,h);
        prefilter(buffer,filtered.data,h,w); //filter Y and clip/quantize to byte
        //Combo({"Nearest"_,"Bilinear"_,"Catmull-Rom","Mitchell-Netravali","Cubic B-Spline without prefilter)","Cubic B-Spline"});
        window.show();
    }
    mat2 warp;
    mat2 warpStart; vec2 drag;
    bool mouseEvent(int2 position, Event event, Button button) {
        if(button==LeftButton) {
            vec2 pos = vec2(position-size/2);
            if(event == Press) drag=pos, warpStart=warp;
            if(event == Motion) {
                float r = dot(pos,pos), scale = dot(pos,drag)/r, shear = cross(pos,drag)/r;
                warp = warpStart*mat2(scale, -shear, shear, scale);
            }
            return true;
        }
        return false;
    }
    void render(int2) {
        for_Image(framebuffer) {
            const vec2 pos = warp*vec2(int2(x,y)-size/2) + vec2(size/2);
            byte4 pixel;
            if(pos.x < 1 || pos.y < 1 || pos.x+2 >= w || pos.y+2 >= h ) continue;
            if(filter==Nearest) pixel = image(round(pos.x),round(pos.y));
            else if(filter==Bilinear) {
                int x=pos.x, y=pos.y; float u=pos.x-x, v=pos.y-y;
                pixel = (image(x+0,y+0)*(1-u) + image(x+1,y+0)*u) * (1-v)
                        + (image(x+0,y+1)*(1-u) + image(x+1,y+1)*u) * v;
            } else {
                int ix = pos.x, iy = pos.y;
                vec2 f(pos.x-ix, pos.y-iy);
                float B,C;
                if(filter==CatmullRom) B=0,C=1./2;
                else if(filter==MitchellNetravali) B=1./3, C=1./3;
                else B=1,C=0;
#if PLOT
                float x = abs((4*pos.x/image.width)-2);
                float y = 1-(2*pos.y/image.width);
                float k;
                if(x<1)
                    k = 1./6 * ((12-9*B-6*C)*cb(x) + (-18+12*B+6*C)*sq(x) + (6-2*B));
                else
                    k = 1./6 * ((-B-6*C)*cb(x) + (6*B+30*C)*sq(x) + (-12*B-48*C)*    x  + (8*B+24*C));
                pixel = (y>0 && y>k) || (y<0 && y<k) ? byte4(255,255,255,255) : byte4(0,0,0,0);
#else
                vec2 t[4] = { vec2(1,1)+f, f, vec2(1,1)-f, vec2(2,2)-f };
                vec2 w[4] = {
                    1./6 * ((-B-6*C)*cb(t[0]) + (6*B+30*C)*sq(t[0]) + (-12*B-48*C)*(t[0]) + vec2(8*B+24*C,8*B+24*C)),
                    1./6 * ((12-9*B-6*C)*cb(t[1]) + (-18+12*B+6*C)*sq(t[1]) + vec2(6-2*B,6-2*B)),
                    1./6 * ((12-9*B-6*C)*cb(t[2]) + (-18+12*B+6*C)*sq(t[2]) + vec2(6-2*B,6-2*B)),
                    1./6 * ((-B-6*C)*cb(t[3]) + (6*B+30*C)*sq(t[3]) + (-12*B-48*C)*(t[3]) + vec2(8*B+24*C,8*B+24*C)),
                };
                /*vec2 w[4] = { //Cubic B-Spline
                  1./6 * cb(1-f),
                  2./3 - 1./2 * sq(f)*(2-f),
                  2./3 - 1./2 * sq(1-f)*(2-(1-f)),
                  1./6 * cb(f)
                };*/
                pixel=zero;
                for(int y=0; y<4; y++) {
                  float4 u=zero;
                  if(filter==CubicBSpline) for(int x=0; x<4; x++) u += w[x].x * float4(filtered(ix+x-1,iy+y-1));
                  else for(int x=0; x<4; x++) u += w[x].x * float4(image(ix+x-1,iy+y-1));
                  pixel += byte4(w[y].y * u);
                }
#endif
            }
            framebuffer(x,y) = pixel;
        }
    }
    bool keyPress(Key key) { if(key == Escape) quit(); return false; }
} bspline;
