/// \file dust.cc Automatic dust removal
#include "inverse-attenuation.h"
#include "interface.h"
#include "window.h"

struct ProcessedSource : ImageSource {
    ImageSource& source;
    ImageOperation& operation;
    ProcessedSource(ImageSource& source, ImageOperation& operation) :
        ImageSource(Folder(".",source.folder)), source(source), operation(operation) {}

    String name() const override { return str(operation.name(), source.name()); }
    size_t size() const override { return source.size(); }
    String name(size_t index) const override { return source.name(index); }
    int64 time(size_t index) const override { return max(operation.time(), source.time(index)); }
    const map<String, String>& properties(size_t index) const override { return source.properties(index); }
    int2 size(size_t index) const override { return source.size(index); }

    /// Returns processed linear image
    virtual SourceImage image(size_t index, uint component) const override {
        return cache<ImageF>(source.name(index), operation.name()+'.'+str(component), source.folder, [&](TargetImage& target) {
            SourceImage sourceImage = source.image(index, component);
            operation.apply(target.resize(sourceImage.size), sourceImage);
        }, time(index));
    }

    /// Returns processed sRGB image
    virtual SourceImageRGB image(size_t index) const override {
        return cache<Image>(source.name(index), operation.name()+".sRGB", source.folder, [&](TargetImageRGB& target) {
            log(source.name(index));
            sRGB(target.resize(size(index)), image(index, 0), image(index, 1), image(index, 2));
        }, time(index));
    }
};

struct Index {
    size_t* pointer;
    void operator=(size_t value) { *pointer = value; }
    operator size_t() const { return *pointer; }
    operator size_t&() { return *pointer; }
};

/// Displays an image collection
struct ImageSourceView : ImageView {
    ImageSource& source;
    Index index;
    size_t imageIndex = -1;
    SourceImageRGB image; // Holds memory map reference

    bool setIndex(int value) {
        size_t index = clip(0, value, (int)source.size()-1);
        if(index != this->index) { this->index = index; return true; }
        return false;
    }

    ImageSourceView(ImageSource& source, size_t* index) : source(source), index{index} {}

    String title() const override {
        return str(source.name(), source.size() ? str(index+1,'/',source.size(), source.name(index), source.properties(index)) : String());
    }

    void update() {
        if(imageIndex != index && source.size()) {
            image = source.image(index);
            ImageView::image = share(image);
        }
    }

    int2 sizeHint(int2 size) override { update(); return ImageView::sizeHint(size); }
    Graphics graphics(int2 size) override { update(); return ImageView::graphics(size); }

    /// Browses source by moving mouse horizontally over image view (like an hidden slider)
    bool mouseEvent(int2 cursor, int2 size, Event, Button button, Widget*&) override {
        return button ? setIndex(source.size()*min(size.x-1,cursor.x)/size.x) : false;
    }

    /// Browses source with keys
    bool keyPress(Key key, Modifiers) override {
        if(key==Home) return setIndex(0);
        if(ref<Key>{Backspace,LeftArrow}.contains(key)) return setIndex(index-1);
        if(ref<Key>{Return,RightArrow}.contains(key)) return setIndex(index+1);
        if(key==End) return setIndex(source.size());
        return false;
    }
};

struct DustRemovalPreview {
    Folder folder {"Pictures", home()};
    InverseAttenuation correction { Folder("Paper", folder) };
#if 1
    ImageFolder source { folder, [](const String&, const map<String, String>& properties){ return
                    !(fromDecimal(properties.at("Aperture"_)) >= 5 || fromDecimal(properties.at("Focal"_)) <= 7.3); } };
    ProcessedSource corrected {source, correction};

    File last {".last", folder, Flags(ReadWrite|Create)};
    size_t index = last.size() ? fromInteger(last.read(last.size())) : 0;
    ~DustRemovalPreview() { last.seek(0); last.resize(last.write(str(index))); }

    ImageSourceView sourceView {source, &index};
    ImageSourceView correctedView {corrected, &index};
    WidgetToggle toggleView {&sourceView, &correctedView};
#else
    ImageView views[2]  = {correction.attenuationImage(), correction.blendFactorImage()};
    WidgetToggle toggleView {&views[0], &views[1]};
#endif
    Window window {&toggleView};
} application;
