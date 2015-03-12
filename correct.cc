#include "IT8.h"
#include "raw.h"
#include "demosaic.h"
#include "view.h"
#include "png.h"

generic ImageT<T> subtract(ImageT<T>&& y, const ImageT<T>& a, T b) {
    for(size_t i: range(y.Ref::size)) y[i] = a[i] - b;
    return move(y);
}
generic ImageT<T> subtract(const ImageT<T>& a, T b) { return subtract<T>(a.size, a, b); }

Map c0Map("c0"); const ImageF c0 = ImageF(unsafeRef(cast<float>(c0Map)), Raw::size);
Map c1Map("c1"); const ImageF c1 = ImageF(unsafeRef(cast<float>(c1Map)), Raw::size);

/// Statically calibrated dark signal non uniformity correction
ImageF staticDSNU(ImageF&& target, const Raw& raw, v4sf* DC=0) {
    real exposure = 0; // raw.exposure; // No effect
	real gain = 1; //(real) raw.gain / raw.gainDiv;
    for(size_t i: range(target.Ref::size)) target[i] = raw[i] - gain * (c0[i] + exposure * c1[i]);
    if(DC) *DC = float4(gain) * (mean(demosaic(c0)) + float4(exposure) * mean(demosaic(c1)));
    return move(target);
}
ImageF staticDSNU(const Raw& raw, v4sf* DC=0) { return staticDSNU(raw.size, raw, DC); }

/// Dynamic column wise dark signal non uniformity correction
ImageF dynamicDSNU(ImageF&& target, const ImageF& raw) {
    // Sums along columns
    real sum[2][raw.size.x];
    for(size_t dy: range(2)) mref<real>(sum[dy], raw.size.x).clear();
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) sum[dy][x] += raw(x, y*2+dy);
    int width = raw.size.x/2;
    float correction[2][raw.size.x];
    for(size_t dy: range(2)) { // Splits similar Bayer cells (FIXME: merge both greens)
        float profile[2][width];
        for(size_t x: range(width)) for(size_t dx: range(2)) profile[dx][x] = sum[dy][x*2+dx] / raw.size.y;
        for(size_t dx: range(2)) for(int x: range(width))
            correction[dy][x*2+dx] = max(0.f, profile[dx][x] - (profile[dx][abs(x-1)]+profile[dx][width-1-abs((width-1)-(x+1))])/2);
    }
    // Substracts DSNU profile from all cells
    for(size_t y: range(raw.size.y/2)) for(size_t dy: range(2)) for(size_t x: range(raw.size.x)) target(x, y*2+dy) = raw(x, y*2+dy) - correction[dy][x];
    return move(target);
}
ImageF dynamicDSNU(const ImageF& raw) { return dynamicDSNU(raw.size, raw); }

ImageF transpose(ImageF&& target, const ImageF& source) {
    for(size_t y: range(source.size.y)) for(size_t x: range(source.size.x)) target(y, x) = source(x, y);
    return move(target);
}
ImageF transpose(const ImageF& source) { return transpose(int2(source.size.y, source.size.x), source); }

ImageF subtract(ImageF&& y, const ImageF& a, const ImageF& b) {
    for(size_t i: range(y.Ref::size)) y[i] = a[i] - b[i];
    return move(y);
}
ImageF subtract(const ImageF& a, const ImageF& b) { return subtract(a.size, a, b); }

ImageF subtract(ImageF&& target, const ImageF& source, v4sf b) {
    for(size_t y: range(target.height/2)) for(size_t x: range(target.width/2)) {
        target(x*2+0, y*2+0) = source(x*2+0, y*2+0) - b[1];
        target(x*2+1, y*2+0) = source(x*2+1, y*2+0) - b[0];
        target(x*2+0, y*2+1) = source(x*2+0, y*2+1) - b[2];
        target(x*2+1, y*2+1) = source(x*2+1, y*2+1) - b[1];
    }
    return move(target);
}
ImageF subtract(const ImageF& a, const v4sf b) { return subtract(a.size, a, b); }

ImageF correlation(ImageF&& y, const ImageF& a, const ImageF& b) {
    assert_(a.Ref::size == y.Ref::size && b.Ref::size == y.Ref::size);
    assert_(a.size == y.size && b.size == y.size);
    float meanA = mean(a), meanB = mean(b);
    real varA = 0; for(size_t i: range(a.Ref::size)) varA += sq(a[i]-meanA); varA /= a.Ref::size;
    real varB = 0; for(size_t i: range(b.Ref::size)) varB += sq(b[i]-meanB); varB /= b.Ref::size;
    float stdAB = sqrt(varA)*sqrt(varB);
    for(size_t i: range(y.Ref::size)) y[i] = (a[i]-meanA) * (b[i]-meanB) / stdAB;
    return move(y);
}
ImageF correlation(const ImageF& a, const ImageF& b) { return correlation(a.size, a, b); }

ImageF sqrtMultiply(ImageF&& y, const ImageF& a, const ImageF& b) {
    for(size_t i: range(y.Ref::size)) y[i] = sqrt(a[i] * b[i]);
    return move(y);
}
ImageF sqrtMultiply(const ImageF& a, const ImageF& b) { return sqrtMultiply(a.size, a, b); }

struct FlatFieldCorrection : Application {
	string fileName = arguments()[0];
	string name = section(fileName,'.');

    mat4 rawRGBtoXYZ;
    map<String, Image> images;
	FlatFieldCorrection() {
		Raw raw {Map(fileName)};

        // Fixed pattern noise correction
		v4sf DC = float4(0);
		ImageF unused staticDSNU = ::staticDSNU(raw, &DC);
		//DC = float4(0);

        mat4 rawRGBtosRGB = mat4(sRGB);
        Image4f RGB = subtract(demosaic(raw), DC);
        if(1) {
			IT8 it8(RGB, readFile("R100604.txt"));
			rawRGBtoXYZ = it8.rawRGBtoXYZ;
            //log(rawRGBtoXYZ);
            rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
            //images.insert(name+".chart", convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
        }
		images.insert(name+".raw", convert(convert(subtract(demosaic(raw), DC), rawRGBtosRGB)));
        //images.insert(name+".DSNU", convert(convert(demosaic(transpose(dynamicDSNU(transpose(dynamicDSNU(raw, 0)), 0))), rawRGBtosRGB)));
        //ImageF a = subtract(c0, DC);
        //images.insert(name+".c0", convert(convert(demosaic(a), rawRGBtosRGB)));
        const ImageF& a = staticDSNU;
        images.insert(name+".static", convert(convert(demosaic(a), rawRGBtosRGB)));
        //ImageF b = dynamicDSNU(subtract(raw, DC));
		//ImageF b = transpose(dynamicDSNU(transpose(dynamicDSNU(subtract(raw, DC)))));
		//images.insert(name+".dynamic", convert(convert(demosaic(b), rawRGBtosRGB)));
        //images.insert(name+".correlation", convert(convert(demosaic(correlation(subtract(raw, a), subtract(raw, b))), rawRGBtosRGB)));
        //images.insert(name+".hybrid", convert(convert(demosaic(subtract(raw, sqrtMultiply(subtract(raw, a), subtract(raw, b)))), rawRGBtosRGB)));
    }
};

struct Preview : FlatFieldCorrection, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);

struct Export : FlatFieldCorrection {
    Export() {
        writeFile(name+".xyz", str(rawRGBtoXYZ), currentWorkingDirectory(), true);
        for(auto image: images) {
            log(image.key);
            writeFile(image.key+".png", encodePNG(image.value), currentWorkingDirectory(), true);
        }
    }
};
registerApplication(Export, export);
