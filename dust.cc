/// \file dust.cc Automatic dust removal
#include "processed-source.h"
#include "inverse-attenuation.h"
#include "interface.h"
#include "window.h"

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
    function<void()> contentChanged;

    bool setIndex(int value) {
        size_t index = clip(0, value, (int)source.count()-1);
        if(index != this->index) { this->index = index; return true; }
        return false;
    }

    ImageSourceView(ImageSource& source, size_t* index, function<void()> contentChanged)
        : source(source), index{index}, contentChanged(contentChanged) {}
    ImageSourceView(ImageSource& source, size_t* index, Window& window)
        : ImageSourceView(source, index, {&window, &Window::render}) {}

    String title() const override {
        return str(source.name(), source.count() ? str(index+1,'/',source.count(), source.name(index), source.properties(index)) : String());
    }

    void update() {
        if(!source.count()) return;
        index = clip(0ul, (size_t)index, source.count()-1);
        if(imageIndex != index) image = SourceImageRGB();
        if(image.size != source.size(index)) {
            imageIndex = index;
            assert_(!image.size || source.size(index).x/image.size.x == source.size(index).y/image.size.y);
            int downscaleFactor = image.size ? (source.size(index).x/image.size.x)/2 : 4; // Doubles resolution at every step
            int2 hint = source.size(index)/downscaleFactor;
            image = source.image(index, hint); // Progressive view
            log(downscaleFactor, hint, image.size);
            ImageView::image = share(image);
            if(image.size != source.size(index)) contentChanged(); // Requests further display until full resolution is shown
        }
    }

    int2 sizeHint(int2) override { return source.size(0); }
    Graphics graphics(int2 size) override { update(); return ImageView::graphics(size); }

    /// Browses source by moving mouse horizontally over image view (like an hidden slider)
    bool mouseEvent(int2 cursor, int2 size, Event, Button button, Widget*&) override {
        return button ? setIndex(source.count()*min(size.x-1,cursor.x)/size.x) : false;
    }

    /// Browses source with keys
    bool keyPress(Key key, Modifiers) override {
        if(key==Home) return setIndex(0);
        if(ref<Key>{Backspace,LeftArrow}.contains(key)) return setIndex(index-1);
        if(ref<Key>{Return,RightArrow}.contains(key)) return setIndex(index+1);
        if(key==End) return setIndex(source.count()-1);
        return false;
    }
};

struct DustRemovalPreview {
    Folder folder {"Pictures", home()};
    ImageFolder calibration {Folder("Paper", folder)};
    InverseAttenuation correction { calibration };
#if 1
    ImageFolder source { folder, [](const String&, const map<String, String>& properties){ return
                    fromDecimal(properties.at("Aperture"_)) <= 5 ||
                    ( fromDecimal(properties.at("Focal"_)) < 4.1 ||
                      (fromDecimal(properties.at("Focal"_)) <= 4.3 && fromDecimal(properties.at("Bias"_)) != 0)); } };
    ProcessedSource corrected {source, correction};

    File last {".last", folder, Flags(ReadWrite|Create)};
    size_t index = source.keys.indexOf(string(last.read(last.size())));
    ~DustRemovalPreview() { last.seek(0); last.resize(last.write(str(source.name(index)))); }

    ImageSourceView sourceView {source, &index, window};
    ImageSourceView correctedView {corrected, &index, window};
    WidgetToggle toggleView {&sourceView, &correctedView};
#else
    ImageView views[2]  = {correction.attenuationImage(), correction.blendFactorImage()};
    WidgetToggle toggleView {&views[0], &views[1]};
#endif
    Window window {&toggleView};
} application;
