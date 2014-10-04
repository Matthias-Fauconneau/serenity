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

void gaussianBlur(ImageF& target, const ImageF& source) {

}

void normalize(mref<float> values) {
    float max = maximum(values);
    chunk_parallel(values.size, [&](uint, uint start, uint size) {
        float scaleFactor = 1./max;
        for(uint index: range(start, start+size)) values[index] *= scaleFactor;
    });
}

/// Inverts attenuation bias
struct InverseAttenuation : Filter {
    ImageSource attenuationBias;

    InverseAttenuation(const ImageFolder& calibrationImages) : attenuationBias(calibrateAttenuationBias(calibrationImages)) {}

    /// Calibrates attenuation bias image by summing images of a white subject
    ImageSource calibrateAttenuationBias(const ImageFolder& calibration) {
        if(1 || !existsFile("attenuationBias"_, calibration.cacheFolder)) { //FIXME: automatic invalidation
            ImageF sum (calibration.imageSize);
            assert_(max(sum.pixels)==0);

            for(string imageName: calibration.imageNames) {
                ImageSource source = calibration.image(imageName);
                chunk_parallel(source.pixels.size, [&](uint, uint start, uint size) {
                    for(uint index: range(start, start+size)) {
                        sum.pixels[index] += source.pixels[index];
                    }
                });
            }
            ImageTarget attenuationBias ("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
            // Low pass to filter texture and noise
            gaussianBlur(attenuationBias, move(sum));
            // TODO: High pass to filter lighting conditions
            normalize(attenuationBias.pixels); // Normalizes sum by maximum (TODO: normalize by low frequency energy)
        }
        return ImageSource("attenuationBias"_, calibration.cacheFolder, calibration.imageSize);
    }

    ImageSource image(const ImageFolder& imageFolder, string imageName) const override {
        Folder targetFolder = Folder(".target"_, imageFolder.cacheFolder, true);
        ImageSource source = imageFolder.image(imageName);
        if(/*1 ||*/ !existsFile(imageName, targetFolder)) { //FIXME: automatic invalidation
            ImageTarget target (imageName, targetFolder, source.ImageF::size);
            // Removes dust from image
            chunk_parallel(target.pixels.size, [&](uint, uint start, uint size) {
                for(uint index: range(start, start+size)) target.pixels[index] = source.pixels[index] / attenuationBias.pixels[index];
            });
        }
        return  ImageSource(imageName, targetFolder, source.ImageF::size);
    }
};

#include "interface.h"

struct FilterView : ImageWidget {
    const Filter& filter;
    const ImageFolder& images;

    string imageName = images.imageNames[0];
    bool enabled = true;
    Image image() const { return sRGB(enabled ? (ImageF)filter.image(images, imageName) : (ImageF)images.image(imageName)); }

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
    InverseAttenuation filter {Folder("Pictures/Paper"_, home())};
    ImageFolder images {Folder("Pictures/Paper"_, home())};
    //FilterView view {filter, images};
    ImageWidget attenuationBias {sRGB(filter.attenuationBias)};
    Window window {&attenuationBias, images.imageSize/4};
} application;
