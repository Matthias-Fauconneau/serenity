#include "window.h"
#include "file.h"
#include "image.h"
#include "matrix.h"
#include "png.h"

extern Image framebuffer;

template <typename S, typename D> inline void prefilter(const S* src, D* dst, int w, int h) {
  const float Zp = sqrt(3.)-2; // Only pole for Z-Transform of cubic spline
  const float lambda = (1 - Zp) * (1 - (1 / Zp)); // Gain to normalize the coefficients
  const int horizon = min(8,w); // 8 pixel is enough precision for float precision
  for(int y=0; y<h; y++) {
    v4sf c[w]; // A line of float coefficients
    float Zn = Zp; // Will store Zp^x
    const S* const s = &src[y*w]; // Source line
    c[0] = {(float)s[0][0], (float)s[0][1], (float)s[0][2], (float)s[0][3]};
    for(int x=0; x<horizon; x++) { // Initializes filter with mirror condition
        c[0] += float4(Zn) * (v4sf){(float)s[x][0], (float)s[x][1], (float)s[x][2], (float)s[x][3]};
        Zn *= Zp;
    }
    c[0] *= float4(lambda); //normalize
    for(int x=1; x<w; x++) c[x] = float4(lambda) * (v4sf){(float)s[x][0], (float)s[x][1], (float)s[x][2], (float)s[x][3]} + float4(Zp) * c[x-1]; // Runs the causal filter
    c[w-1] = float4(Zp / (Zp-1)) * c[w-1]; // Initializes anticausal filter (mirror conditions)
    for(int x=w-2; x>=0; x--) c[x] = float4(Zp) * ( c[x+1] - c[x] ); // Runs anticausal filter (last pixel (w-1) is initialized above)
    D* d = &dst[y]; // Destination column
    for(int x=w-1; x>=0; x--) { // Copies coefficient line to output column (i.e transposed store)
        if(sizeof(D)==sizeof(byte4)) // static_if(D==byte4)
            d[x*w] = clamp(0.f, c[x], 255.f);
        else
            d[x*w] = D(c[x]);
    }
  }
}

struct BSplineDemo : Widget {
    unique<Window> window = ::window(this);
    Image image, filtered;
    enum Filter { Nearest, Bilinear, CatmullRom, MitchellNetravali, NoPrefilter, CubicBSpline, FilterCount } filter = CubicBSpline;
    bool play = true;
    BSplineDemo() {
        assert(arguments(),"Usage: bspline image.png"_);
        window->setTitle(arguments()[0]);
        image = decodeImage(readFile(arguments()[0]));
        int w=image.width, h=image.height;
        v4sf buffer[w*h]; // use full precision buffer between filtering pass
        prefilter(image.data,buffer,w,h); //filter X and upsample to float
        filtered = Image(w,h);
        prefilter<v4sf, byte4>(buffer,filtered.begin(),h,w); //filter Y and clip/quantize to byte
        window->actions[Space] = [this]{ play=!play; window->render(); };
        window->actions[Return] = [this]{
            filter = Filter((filter+1)%FilterCount);
            window->setTitle(ref<string>{"Nearest","Bilinear","Catmull-Rom","Mitchell-Netravali", "No Prefilter", "Cubic B-Spline"}[int(filter)]);
            window->render();
        };
    }
    mat2 warp;
    mat2 warpStart; vec2 drag; float angle = 0;
    bool mouseEvent(vec2 position, vec2 size, Event event, Button button, Widget*&) override {
        if(button==LeftButton) {
            vec2 pos = position-size/2.f;
            if(event == Press) drag=pos, warpStart=warp;
            if(event == Motion) {
                float r = dot(pos,pos), scale = dot(pos,drag)/r, shear = cross(pos,drag)/r;
                warp = warpStart*mat2(scale, -shear, shear, scale);
            }
            return true;
        }
        return false;
    }
    vec2 sizeHint(vec2) override { return 512; }
    shared<Graphics> graphics(vec2 size) override {
        Image target = Image( int2(size) );
        int w = target.width, h = target.height;
        for(size_t y: range(h)) for(size_t x: range(w)) {
            const vec2 pos = size/2.f + warp*(vec2(x,y)-size/2.f);
            byte4 pixel;
            if(pos.x < 1 || pos.y < 1 || pos.x+2 >= w || pos.y+2 >= h ) continue;
            if(filter==Nearest) pixel = image(round(pos.x),round(pos.y));
            else if(filter==Bilinear) {
                int x=pos.x, y=pos.y; float u=pos.x-x, v=pos.y-y;
                pixel = byte4((bgra4f(image(x+0,y+0))*(1-u) + bgra4f(image(x+1,y+0))*u) * (1-v)
                        + (bgra4f(image(x+0,y+1))*(1-u) + bgra4f(image(x+1,y+1))*u) * v);
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
                    1.f/6 * ((-B-6*C)*cb(t[0]) + (6*B+30*C)*sq<vec2>(t[0]) + (-12*B-48*C)*(t[0]) + vec2(8*B+24*C,8*B+24*C)),
                    1.f/6 * ((12-9*B-6*C)*cb(t[1]) + (-18+12*B+6*C)*sq<vec2>(t[1]) + vec2(6-2*B,6-2*B)),
                    1.f/6 * ((12-9*B-6*C)*cb(t[2]) + (-18+12*B+6*C)*sq<vec2>(t[2]) + vec2(6-2*B,6-2*B)),
                    1.f/6 * ((-B-6*C)*cb(t[3]) + (6*B+30*C)*sq<vec2>(t[3]) + (-12*B-48*C)*(t[3]) + vec2(8*B+24*C,8*B+24*C)),
                };
                /*vec2 w[4] = { //Cubic B-Spline
                  1./6 * cb(1-f),
                  2./3 - 1./2 * sq(f)*(2-f),
                  2./3 - 1./2 * sq(1-f)*(2-(1-f)),
                  1./6 * cb(f)
                };*/
                pixel = 0;
                for(int y=0; y<4; y++) {
                  bgra4f u = 0;
                  if(filter==CubicBSpline) for(int x=0; x<4; x++) u += w[x].x * bgra4f(filtered(ix+x-1,iy+y-1));
                  else for(int x=0; x<4; x++) u += w[x].x * bgra4f(image(ix+x-1,iy+y-1));
                  pixel += byte4(w[y].y * u);
                }
#endif
            }
            target(x,y) = pixel;
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        if(play) {
            angle += 2*PI/60;
            warp = warpStart*mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
            window->render();
        }
        return graphics;
    }
} bspline;
