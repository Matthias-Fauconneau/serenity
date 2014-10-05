/// \file parallel.cc Parallel operations
#include "thread.h"

float maximum(ref<float> values) {
    float maximums[threadCount];
    chunk_parallel(values.size, [&](uint id, uint start, uint size) {
        float max=0;
        for(uint index: range(start, start+size)) { float v=values[index]; if(v>max) max = v; }
        maximums[id] = max;
    });
    return max(maximums);
}

/// \file dust.cc Automatic dust removal
#include "filter.h"

/// Convolves and transposes (with repeat border conditions)
void convolve(float* target, const float* kernel, int radius, const float* source, int width, int height) {
    int kernelSize = radius+1+radius;
    /*chunk_*/parallel(height, [&](uint, uint y) {
        const float* line = source + y * width;
        float* targetColumn = target + y;
        for(int x: range(-radius,0)) {
            float sum = 0;
            for(int dx: range(kernelSize)) sum += kernel[dx] * line[max(0, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(0,width-2*radius)) {
            float sum = 0;
            const float* span = line + x;
            for(int dx: range(kernelSize)) sum += kernel[dx] * span[dx];
            targetColumn[(x+radius)*height] = sum;
        }
        for(int x: range(width-2*radius,width-radius)){
            float sum = 0;
            for(int dx: range(kernelSize)) sum += kernel[dx] * line[min(width, x+dx)];
            targetColumn[(x+radius)*height] = sum;
        }
    });
}

float gaussian(float sigma, float x) { return exp(-sq(x)/(2*sq(sigma))); }
void gaussianBlur(ImageF& image, float sigma) {
    int radius = ceil(3*sigma);
    float kernel[radius+1+radius];
    for(int dx: range(radius+1+radius)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
    buffer<float> transpose (image.height*image.width);
    convolve(transpose, kernel, radius, image.pixels, image.width, image.height);
    convolve(image.pixels, kernel, radius, transpose, image.height, image.width);
}

void normalize(mref<float> values) {
    float max = maximum(values);
    parallel_apply(values, values, [](float v, float scaleFactor) {  return scaleFactor*v; }, 1./max);
}

/// Inverts attenuation bias
struct InverseAttenuation : Filter {
    ImageSource attenuationBias;

    InverseAttenuation(const ImageFolder& calibrationImages) : attenuationBias(calibrateAttenuationBias(calibrationImages)) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const ImageFolder& calibration) {
        if(skipCache || !existsFile("attenuationBias"_, calibration.cacheFolder)) {
            ImageTarget attenuationBias ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);

            attenuationBias.pixels.clear();
            for(string imageName: calibration.imageNames) {
                ImageSource source = calibration.image(imageName, Mean); // Assumes dust affects all components equally
                parallel_apply2(attenuationBias.pixels, attenuationBias.pixels, source.pixels, [](float sum, float source) { return sum + source; });
            }
            // Low pass to filter texture and noise
            gaussianBlur(attenuationBias, 16); // TODO: weaken near spot, strengthen outside
            // TODO: High pass to filter lighting conditions
            normalize(attenuationBias.pixels); // Normalizes sum by maximum (TODO: normalize by low frequency energy)
            assert_(min(attenuationBias.pixels)>0);
        }
        return ImageSource("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
    }

    ImageSource image(const ImageFolder& imageFolder, string imageName, Component component) const override {
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        String id = imageName+"."_+str(component);
        if(skipCache || !existsFile(id, targetFolder)) {
            ImageSource source = imageFolder.image(imageName, component);
            ImageTarget target (id, targetFolder, source.ImageF::size);
            parallel_apply2(target.pixels, source.pixels, attenuationBias.pixels, [&](float source, float bias) { return source / bias; });
        }
        return ImageSource(id, targetFolder, imageFolder.imageSize);
    }
};

#include "interface.h"

struct FilterView : ImageWidget {
    const Filter& filter;
    const ImageFolder& images;

    string imageName = images.imageNames[0];
    bool enabled = true;
    Image image() const {
        if(enabled) return sRGB(
                    filter.image(images, imageName, Blue),
                    filter.image(images, imageName, Green),
                    filter.image(images, imageName, Red) );
        else return sRGB(
                    images.image(imageName, Blue),
                    images.image(imageName, Green),
                    images.image(imageName, Red) );
    }

    FilterView(const Filter& filter, const ImageFolder& images) : filter(filter), images(images) { ImageWidget::image = image(); }

    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override {
        string imageName = images.imageNames[(images.imageNames.size-1)*cursor.x/size.x];
        bool enabled = cursor.y < size.y/2;
        if(enabled != this->enabled || imageName != this->imageName) {
            this->enabled = enabled;
            this->imageName = imageName;
            ImageWidget::image = image();
            return true;
        }
        return false;
    }
};

#include "window.h"

struct DustRemovalPreview {
    //InverseAttenuation filter {Folder("Pictures/Paper"_, home())};
    ImageFolder images {Folder("Pictures"_, home())};
    //FilterView view {filter, images};
    //ImageWidget view {sRGB(filter.attenuationBias)};
    ImageWidget view { sRGB(
                linear(images.scaledRGB(images.imageNames[0]), Blue),
                linear(images.scaledRGB(images.imageNames[0]), Green),
                linear(images.scaledRGB(images.imageNames[0]), Red) ) };
    Window window {&view};
} application;
