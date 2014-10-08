/// \file dust.cc Automatic dust removal
#include "inverse-attenuation.h"
#include "interface.h"
#include "window.h"

struct ProcessedSource : ImageSource {
    ImageSource& source;
    ImageOperation& operation;
    ProcessedSource(ImageSource& source, ImageOperation& operation) :
        ImageSource(Folder(".",source.folder)), source(source), operation(operation) {}

    size_t size() const override { return source.size(); }
    String name(size_t index) const override { return source.name(index); }
    int64 time(size_t index) const override { return max(source.time(index), operation.version()); }
    const map<String, String>& properties(size_t index) const override { return source.properties(index); }
    int2 size(size_t index) const override { return source.size(index); }

    /// Returns processed linear image
    virtual SourceImage image(size_t index, uint component) const override {
        return cache<ImageF>(source.name(index), operation.name()+'.'+str(component), source.folder, [&](TargetImage&& target) {
            SourceImage sourceImage = source.image(index, component);
            operation.apply(target.resize(sourceImage.size), sourceImage, component);
        }, time(index));
    }

    /// Returns processed sRGB image
    virtual SourceImageRGB image(size_t index) const override {
        return cache<Image>(source.name(index), operation.name()+".sRGB", source.folder, [&](TargetImageRGB&& target) {
            sRGB(target.resize(size(index)), image(index, 0), image(index, 1), image(index, 2));
        }, time(index));
    }
};

/// Displays an image collection
struct ImageSourceView : ImageView {
    ImageSource& source;
    size_t index = -1;
    SourceImageRGB image; // Holds memory map reference

    bool setIndex(size_t index) {
        if(index != this->index) {
            this->index = index;
            image = source.image(index);
            ImageView::image = share(image);
            return true;
        }
        return false;
    }

    ImageSourceView(ImageSource& source) : source(source) { setIndex(0); }

    String title() const override { return str(index,'/',source.size(), source.name(index), source.properties(index)); }

    /// Browses source by moving mouse horizontally over image view (like an hidden slider)
    bool mouseEvent(int2 cursor, int2 size, Event, Button, Widget*&) override { return setIndex(source.size()*min(size.x-1,cursor.x)/size.x); }
};

struct DustRemovalPreview {
    InverseAttenuation correction { Folder("Pictures/Paper", home()) };
    ImageFolder source { Folder("Pictures", home()),
                [](const String&, const map<String, String>& properties){ return fromDecimal(properties.at("Aperture")) > 4; } };
    ImageSourceView sourceView {source};
    ProcessedSource corrected {source, correction};
    ImageSourceView correctedView {corrected};
    WidgetToggle toggleView {&sourceView, &correctedView};
    Window window {&toggleView};
} application;
