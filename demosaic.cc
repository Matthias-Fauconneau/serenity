#include "IT8.h"
#include "raw.h"
#include "demosaic.h"
#include "interface.h"
#include "window.h"
#include "png.h"

#if 1
struct List {
    List() {
        for(string file: currentWorkingDirectory().list(Files|Recursive)) {
            if(!endsWith(file, ".raw16")) continue;
            Raw raw(file, false);
            log(file,"\t", int(round(raw.exposure*1e3)),"\t", raw.gain, raw.gainDiv, raw.temperature);
        }
    }
} app;
#endif

#if 0
v4sf max(ref<v4sf> X) { v4sf y = float4(0); for(v4sf x: X) y = max(y, x); return y; }
void multiply(mref<v4sf> X, v4sf c) { for(v4sf& x: X) x *= c; }
Image4f normalize(Image4f&& image) { multiply(image, float4(1)/max(image)); return move(image); }

struct CalibrationVisualization {
    CalibrationVisualization() {
        for(string name: {"c0"_,"c1"_}) {
            writeFile(name+".png", encodePNG(convert(normalize(demosaic(ImageF(unsafeRef(cast<float>(Map(name))), CMV12000))))),
                      currentWorkingDirectory(), true);
       }
    }
} app;
#endif

#if 1
struct IT8Application : Application {
    string it8Charge = arguments()[0];
    string it8Image = arguments()[1];
    string name = section(it8Image,'.');

    mat4 rawRGBtoXYZ;
    map<String, Image> images;
    IT8Application() {
        Raw raw (it8Image);

        ImageF FPN (raw.size); // Corrected for fixed pattern noise
        if(0) { // Statically calibrated DSNU correction (fails)
            Map c0Map("c0"); ImageF c0 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
            Map c1Map("c1"); ImageF c1 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
            real exposure = raw.exposure;
            real gain = (real) raw.gain * 3/*darkframe gainDiv*/ / raw.gainDiv;
            for(size_t i: range(FPN.Ref::size)) FPN[i] = raw[i] - gain * (c0[i] + exposure * c1[i]);
        } else { // Dynamic column wise DSNU correction
            if(0) { // High pass
                float window[5] = {0,0,0,0,0};
                for(size_t x: range(2)) { // First estimations
                    real sum = 0;
                    for(size_t y: range(raw.size.y)) sum += raw(x, y);
                    window[2+x] = sum / raw.size.y;
                }
                for(size_t x: range(raw.size.x-2)) {
                    for(size_t x: range(5-1)) window[x] = window[x+1]; // Shifts window
                    real sum = 0;
                    for(size_t y: range(raw.size.y)) sum += raw(x+2, y); // Next value
                    window[4] = sum / raw.size.y;
                    float columnDSNU = window[2] - (window[0]+window[4])/2; // Consider only similar Bayer rows (red/blue)
                    for(size_t y: range(raw.size.y)) FPN(x, y) = raw(x,y) - columnDSNU;
                }
                for(size_t x: range(raw.size.x-2, raw.size.x)) { // Last corrections
                    for(size_t x: range(5-1)) window[x] = window[x+1]; // Shifts window
                    window[4] = 0;
                    float columnDSNU = window[2] - (window[0]+window[4])/2; // Consider only similar Bayer rows (red/blue)
                    for(size_t y: range(raw.size.y)) FPN(x, y) = raw(x,y) - columnDSNU;
                }
            } else if(0) { // Cut DC
                float profile[raw.size.x];
                for(size_t x: range(raw.size.x)) {
                    real sum = 0;
                    for(size_t y: range(raw.size.y)) sum += raw(x, y);
                    profile[x] = sum / raw.size.y;
                }
                real sum[2] = {0, 0};
                for(size_t x: range(raw.size.x/2)) {
                    sum[0] += profile[x*2+0];
                    sum[1] += profile[x*2+1];
                }
                float DC[2] = {float(sum[0] / raw.size.x), float(sum[1] / raw.size.x)}; // Consider only similar Bayer rows (red/blue)
                for(size_t x: range(raw.size.x/2)) {
                    for(size_t dx: range(2)) for(size_t y: range(raw.size.y)) FPN(x*2+dx, y) = raw(x*2+dx,y) - (profile[x*2+dx]-DC[dx]);
                }
            } else { // Gaussian high pass
                // Sums along columns
                real sum[raw.size.x]; mref<real>(sum,raw.size.x).clear();
                for(size_t y: range(raw.size.y)) for(size_t x: range(raw.size.x)) sum[x] += raw(x, y);
                int width = raw.size.x/2;
                float profile[2][width];
                for(size_t x: range(width)) for(size_t dx: range(2)) profile[dx][x] = sum[x*2+dx] / raw.size.y;

                // Dirac-Gaussian high pass kernel (unsharp mask)
                float sigma = 2;
                int radius = ceil(3*sigma);
                size_t N = radius+1+radius;
                float kernel[N];
                for(int dx: range(N)) kernel[dx] = -gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
                float scale = -1/::sum(ref<float>(kernel,N), 0.);
                for(float& w: kernel) w *= scale; // Normalizes
                kernel[radius] += 1; // Dirac

                // Convolves profile with high pass kernel (mirror boundary conditions)
                float correction[width*2];
                for(size_t i: range(2)) for(int x: range(width)) {
                    float sum = 0;
                    for(int dx: range(N)) sum += kernel[dx] * profile[i][width-1-abs(abs(x-radius+dx)-(width-1))];
                    correction[x*2+i] = sum;
                }

                // Substracts DSNU profile from columns
                for(size_t y: range(raw.size.y)) for(size_t x: range(raw.size.x)) FPN(x, y) = raw(x,y) - correction[x];
            }
        }

        mat4 rawRGBtosRGB = mat4(sRGB);
        Image4f FPNRGB = demosaic(FPN);
        if(0) {
            IT8 it8(FPNRGB, readFile(it8Charge));
            rawRGBtoXYZ = it8.rawRGBtoXYZ;
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            images.insert(name+".chart", convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
        }
        images.insert(name+".raw", convert(convert(demosaic(raw), rawRGBtosRGB)));
        images.insert(name+".FPN", convert(convert(FPNRGB, rawRGBtosRGB)));
    }
};

struct WindowCycleView {
    buffer<ImageView> views;
    WidgetCycle layout;
    Window window {&layout, int2(1024, 768)};
    WindowCycleView(const map<String, Image>& images)
        : views(apply(images.size(), [&](size_t i) { return ImageView(share(images.values[i]), images.keys[i]); })),
          layout(toWidgets<ImageView>(views)) {}
};
struct Preview : IT8Application, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);

struct Export : IT8Application {
    Export() {
        writeFile(name+".xyz", str(rawRGBtoXYZ), currentWorkingDirectory(), true);
        for(auto image: images) writeFile(image.key+".png", encodePNG(image.value), currentWorkingDirectory(), true);
    }
};
registerApplication(Export, export);
#endif
