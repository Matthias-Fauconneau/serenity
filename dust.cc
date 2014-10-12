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
struct ImageSourceView : ImageView, Poll {
    ImageSource& source;
    Index index;
    size_t imageIndex = -1;
    SourceImageRGB image; // Holds memory map reference
    int2 size = 0;
    function<void()> contentChanged;

    ImageSourceView(ImageSource& source, size_t* index, function<void()> contentChanged)
        : source(source), index{index}, contentChanged(contentChanged) {}
    ImageSourceView(ImageSource& source, size_t* index, Window& window)
        : ImageSourceView(source, index, {&window, &Window::render}) {}

    // Evaluation

    void event() override {
        if(imageIndex != index) { image=SourceImageRGB();  ImageView::image=Image(); }
        if(image.size*2 > size) return;

        int downscaleFactor = image.size ? (source.size(index).x/image.size.x)/2 : 16; // Doubles resolution at every step
        while(source.maximumSize().x/(source.size(index).x/downscaleFactor) > 16) downscaleFactor /= 2; // Limits calibration downscale (FIXME)
        int2 hint = (source.size(index)+int2(downscaleFactor-1))/downscaleFactor;
        assert_(source.maximumSize().x/hint.x <= 16);

        imageIndex = index;
        image = source.image(index, hint);
        ImageView::image = share(image);
        contentChanged();
    }

    // Content

    String title() const override {
        return str(source.name(), source.count() ? str(index+1,'/',source.count(), source.name(index), source.properties(index)) : String());
    }

    int2 sizeHint(int2 size) override {
        assert_(size);
        int2 maximum = source.maximumSize();
        int downscaleFactor = min(max((maximum.x+size.x-1)/size.x, (maximum.y+size.y-1)/size.y), 16);
        int2 hint = maximum/downscaleFactor;
        assert_(hint<=size, maximum, size, downscaleFactor, maximum/downscaleFactor);
        return hint;
    }

    Graphics graphics(int2 size) override {
        if(!source.count()) return {};
        index = clip(0ul, (size_t)index, source.count()-1);
        if(imageIndex != index) event(); // Evaluates requested image immediately
        else if(image.size*2 <= size) { this->size=size; queue(); } // Requests further display until full resolution is shown
        assert_(image.size <= size, image.size, size);
        return ImageView::graphics(size);
    }

    // Control

    bool setIndex(int value) {
        size_t index = clip(0, value, (int)source.count()-1);
        if(index != this->index) { this->index = index; if(index != imageIndex) queue(); return true; }
        return false;
    }

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
    Window window {&toggleView};
} application;
