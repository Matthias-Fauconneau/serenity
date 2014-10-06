#include "parallel.h"

// -> image.cc
#include "image.h"

//void operator-=(ImageF& target, const ImageF& source) { target.pixels -= source.pixels; }
ImageF operator-(const ImageF& A, const ImageF& B) { ImageF Y(A.size); subtract(Y.pixels, A.pixels, B.pixels); return Y; }

/// Convolves and transposes (with repeat border conditions)
void convolve(float* target, const float* kernel, int radius, const float* source, int width, int height) {
    int N = radius+1+radius;
    chunk_parallel(height, [&](uint, uint y) {
        const float* line = source + y * width;
        float* targetColumn = target + y;
        for(int x: range(-radius,0)) {
            float sum = 0;
            for(int dx: range(N)) sum += kernel[dx] * line[max(0, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(0,width-2*radius)) {
            float sum = 0;
            const float* span = line + x;
            for(int dx: range(N)) sum += kernel[dx] * span[dx];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(width-2*radius,width-radius)){
            float sum = 0;
            for(int dx: range(N)) sum += kernel[dx] * line[min(width-1, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
    });
}

float gaussian(float sigma, float x) { return exp(-sq(x)/(2*sq(sigma))); }
ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma) {
    assert_(sigma > 0);
    int radius = ceil(3*sigma), N = radius+1+radius;
    float kernel[N];
    for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
    float sum = ::sum(ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
    buffer<float> transpose (target.height*target.width);
    convolve(transpose, kernel, radius, source.pixels, source.width, source.height);
    convolve(target.pixels, kernel, radius, transpose, target.height, target.width);
    return move(target);
}
ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }

ImageF bandPass(const ImageF& source, float lowPass, float highPass) {
    ImageF low_band = lowPass ? gaussianBlur(source, lowPass) : share(source);
    if(!highPass) return low_band;
    ImageF low = gaussianBlur(low_band, highPass);
    subtract(low.pixels, low_band.pixels, low.pixels);
    return low;
}

/// \file dust.cc Automatic dust removal
#include "filter.h"

/// Inverts attenuation bias
struct InverseAttenuation : Filter {
    ImageSource attenuationBias;

    InverseAttenuation(const ImageFolder& calibrationImages) : attenuationBias(calibrateAttenuationBias(calibrationImages)) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const ImageFolder& calibration) {
        if(1 || skipCache || !existsFile("attenuationBias"_, calibration.cacheFolder)) {
            ImageTarget attenuationBias ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
            if(existsFile("attenuationBias.lock"_)) remove("attenuationBias.lock"_);
            rename("attenuationBias"_, "attenuationBias.lock"_, calibration.cacheFolder);
            log("Calibration");

            attenuationBias.pixels.clear();
            for(string imageName: calibration.keys) {
                ImageSource source = calibration.image(imageName, Mean); // Assumes dust affects all components equally
                parallel_apply(attenuationBias.pixels, [](float sum, float source) { return sum + source; }, attenuationBias.pixels, source.pixels);
            }
            // Low pass to filter texture and noise
            gaussianBlur(attenuationBias, 8); // TODO: weaken near spot, strengthen outside
            // TODO: High pass to filter lighting conditions
            // Normalizes sum by mean (DC)
            float sum = parallel_sum(attenuationBias.pixels);
            float mean = sum/attenuationBias.pixels.size;
            float factor = 1/mean;
            parallel_apply(attenuationBias.pixels, [&](float v) {  return min(1.f, factor*v); }, attenuationBias.pixels);
            rename("attenuationBias.lock"_, "attenuationBias"_, calibration.cacheFolder);
        }
        return ImageSource("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
    }

    ImageSource image(const ImageFolder& imageFolder, string imageName, Component component) const override {
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        String id = imageName+"."_+str(component);
        if(1 || skipCache || !existsFile(id, targetFolder)) {
            ImageSource source = imageFolder.image(imageName, component);
            int2 size = source.ImageF::size;

            ImageTarget target (id, targetFolder, size);
            parallel_apply(target.pixels, [&](float source, float bias) { return source / bias; }, source.pixels, attenuationBias.pixels);

            if(1) {
                // Cuts band to correct
                const float lowPass = 8, highPass = 32;
                ImageF reference = bandPass(source, lowPass, highPass);
                ImageF corrected = bandPass(target, lowPass, highPass);

                // Saturates correction below max(0, source) (prevents introduction of a light feature at spot frequency)
                parallel_apply(target.pixels, [&](float source, float reference, float corrected) {
                    float saturated_corrected = min(corrected, max(reference, 0.f));
                    float correction = saturated_corrected - reference;
                    return source + correction;
                }, source.pixels, reference.pixels, corrected.pixels);
            }
        }
        return ImageSource(id, targetFolder, imageFolder.imageSize);
    }
};

#include "interface.h"

struct ImageView : ImageWidget {
    const ImageFolder& images;
    string imageName = images.keys[0];
    ImageSourceRGB source; // Holds memory map reference

    ImageView(const ImageFolder& images) : images(images) { update(); }

    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = images.keys[images.size()*min(size.x-1,cursor.x)/size.x];
        if(imageName != this->imageName) {
            this->imageName = imageName; update();
            return true;
        }
        return false;
    }

    virtual void update() {
        source = images.scaledRGB(imageName);
        ImageWidget::image = share(source);
    }
};

struct FilterView : ImageView {
    const Filter& filter;
    bool enabled = false;

    FilterView(const ImageFolder& images, const Filter& filter) : ImageView(images), filter(filter) { update(); }

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
        bool enabled = button != NoButton && event != Release;
        if(enabled != this->enabled) { this->enabled = enabled, imageName=""_;/*Forces update*/ }
        return ImageView::mouseEvent(cursor, size, event, button, focus);
    }

    void update() override {
        if(enabled) ImageWidget::image = sRGB(
                    filter.image(images, imageName, Blue),
                    filter.image(images, imageName, Green),
                    filter.image(images, imageName, Red) );
        else ImageView::update();
    }
};

#include "window.h"

struct FilterWindow : FilterView {
    Window window {this};
    FilterWindow(const ImageFolder& images, const Filter& filter) : FilterView(images, filter) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
        if(FilterView::mouseEvent(cursor, size, event, button, focus)) {
            window.setTitle(str(imageName, images.at(imageName), enabled));
            return true;
        }
        return false;
    }
};

struct DustRemovalPreview {
    ImageFolder calibrationImages = Folder("Pictures/Paper"_, home());
    //ImageView view {calibrationImages};
    InverseAttenuation filter {calibrationImages};
    //ImageWidget view {sRGB(filter.attenuationBias)};
    ImageFolder images {Folder("Pictures"_, home())};
    FilterWindow view {images, filter};
} application;
