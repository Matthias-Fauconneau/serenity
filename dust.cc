#include "parallel.h"

// -> image.cc
#include "image.h"
//ImageF operator-(const ImageF& A, const ImageF& B) { ImageF Y(A.size); subtract(Y.pixels, A.pixels, B.pixels); return Y; }

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

    InverseAttenuation(const Folder& calibrationFolder) : attenuationBias(calibrateAttenuationBias(move(calibrationFolder))) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const Folder& calibrationFolder) {
        if(!existsFile("attenuationBias"_, Folder(".cache"_,calibrationFolder, true))) {
            ImageFolder calibration (Folder("."_,calibrationFolder)); // Lists images and evaluates target image size
            ImageTarget attenuationBias ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);

            // Sums all images
            attenuationBias.pixels.clear();
            for(string imageName: calibration.keys) {
                ImageSource source = calibration.image(imageName, Mean); // Assumes dust affects all components equally
                parallel_apply(attenuationBias.pixels, [](float sum, float source) { return sum + source; }, attenuationBias.pixels, source.pixels);
            }

            // Low pass to filter texture and noise
            gaussianBlur(attenuationBias, 8); // Useful?, TODO?: weaken near spot, strengthen outside

            // TODO: High pass to filter lighting conditions

            // Normalizes sum by mean (DC) (and clips values over average to 1)
            float sum = parallel_sum(attenuationBias.pixels);
            float mean = sum/attenuationBias.pixels.size;
            float factor = 1/mean;
            parallel_apply(attenuationBias.pixels, [&](float v) {  return min(1.f, factor*v); }, attenuationBias.pixels);
        }
        return ImageSource("attenuationBias"_, Folder(".cache"_,calibrationFolder));
    }

    ImageSource image(ImageFolder& imageFolder, string imageName, Component component) const override {
        //if(fromDecimal(imageFolder.at(imageName).at("Aperture"_)) < 6.3) return imageFolder.image(imageName, component);
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        String id = imageName+"."_+str(component);
        if(!existsFile(id, targetFolder)) {
            ImageSource source = imageFolder.image(imageName, component);
            int2 size = source.size;

            ImageTarget target (id, targetFolder, size);
            parallel_apply(target.pixels, [&](float source, float bias) { return source / bias; }, source.pixels, attenuationBias.pixels);

            if(1) {
                // Restricts correction to a frequency band
                const float lowPass = 8, highPass = 32; //TODO: automatic determination from spectrum of correction (difference) image
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
        return ImageSource(id, targetFolder);
    }
};

#include "interface.h"

struct ImageView : ImageWidget {
    ImageFolder& images;
    string imageName = images.keys[0];
    ImageSourceRGB source; // Holds memory map reference

    ImageView(ImageFolder& images) : images(images) { update(); }

    virtual void update() {
        source = images.scaledRGB(imageName);
        ImageWidget::image = share(source);
    }

    /// Browses images by moving mouse horizontally over image view (like an hidden slider)
    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = images.keys[images.size()*min(size.x-1,cursor.x)/size.x];
        if(imageName != this->imageName) {
            this->imageName = imageName;
            update();
            return true;
        }
        return false;
    }

    virtual String title() { return str(images.keys.indexOf(imageName),'/',images.size(), imageName, images.at(imageName)); }
};

struct FilterView : ImageView {
    const Filter& filter;
    bool enabled = false;

    FilterView(ImageFolder& images, const Filter& filter) : ImageView(images), filter(filter) { update(); }

    void update() override {
        if(enabled) ImageWidget::image = sRGB(
                    filter.image(images, imageName, Blue),
                    filter.image(images, imageName, Green),
                    filter.image(images, imageName, Red) );
        else ImageView::update();
    }

    /// Enables filter while a mouse button is pressed
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
        bool enabled = button != NoButton && event != Release;
        if(enabled != this->enabled) { this->enabled = enabled, imageName=""_;/*Forces update*/ }
        return ImageView::mouseEvent(cursor, size, event, button, focus);
    }

    String title() override { return str(ImageView::title(), enabled); }
};

#include "window.h"

struct FilterWindow : FilterView {
    Window window {this, -1, title()};
    FilterWindow(ImageFolder& images, const Filter& filter) : FilterView(images, filter) {}
    void update() override { FilterView::update(); if(window)/*called in constructor while window is not initialized yet*/ window.setTitle(title()); }
};

struct DustRemovalPreview {
    InverseAttenuation filter { Folder("Pictures/Paper"_, home()) };
    //ImageWidget view {sRGB(filter.attenuationBias)};
    ImageFolder images { Folder("Pictures"_, home()),
                [](const String&, const map<String, String>& tags){ return fromDecimal(tags.at("Aperture"_)) > 4; } };
    FilterWindow view {images, filter};
} application;
