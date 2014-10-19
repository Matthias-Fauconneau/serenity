#pragma once
#include "source.h"
#include "interface.h"
#include "window.h"

/// Displays an image collection
struct ImageSourceView : ImageView, Poll {
    ImageSource& source;
    Index index;
	size_t imageIndex = -1;
    SourceImageRGB image; // Holds memory map reference
    int2 size = 0;
    function<void()> contentChanged;

	ImageSourceView(ImageSource& source, size_t* index, function<void()> contentChanged)
		: source(source), index(index), contentChanged(contentChanged) {}
	ImageSourceView(ImageSource& source, size_t* index, Window& window)
        : ImageSourceView(source, index, {&window, &Window::render}) {}

    // Progressive evaluation
    void event() override {
        if(imageIndex != index) { image=SourceImageRGB();  ImageView::image=Image(); }
#if PROGRESSIVE
        if(image.size*2 > size) return;
        debug(if(image.size) return;)

        int downscaleFactor = image.size ? (source.size(index).x/image.size.x)/2 : 16; // Doubles resolution at every step
        while(source.maximumSize().x/(source.size(index).x/downscaleFactor) > 16) downscaleFactor /= 2; // Limits calibration downscale (FIXME)
        int2 hint = (source.size(index)+int2(downscaleFactor-1))/downscaleFactor;
#else
        int2 hint = size;
        assert_(source.maximumSize().x/hint.x <= 16);
#endif
        imageIndex = index;
		image = source.image(min<size_t>(index, source.count(index+1)-1), hint);
        ImageView::image = share(image);
        contentChanged();
    }

    // Content

    String title() override {
		if(!source.count()) return String();
		return str(/*source.name(), str(index+1,'/',source.count(),*/ source.elementName(index), source.properties(index));
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
		if(!source.count(1)) return {};
        this->size=size;
        if(imageIndex != index) event(); // Evaluates requested image immediately
#if PROGRESSIVE
        else if(image.size*2 <= size) queue(); // Requests further display until full resolution is shown
#endif
        assert_(image.size <= size, image.size, size);
        return ImageView::graphics(size);
    }

    // Control

    bool setIndex(int value) {
		if(!source.count(value)) return {};
        if(index != this->index) { this->index = index; if(index != imageIndex) queue(); return true; }
        return false;
    }

	/// Browses source by dragging mouse horizontally over image view (like an hidden slider)
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*&) override {
		return (event==Motion && button) ? setIndex(source.count()*min(size.x-1,cursor.x)/size.x) : false;
    }

    /// Browses source with keys
    bool keyPress(Key key, Modifiers) override {
		if(source.count() && key==Home) return setIndex(0);
		if(int(index)>0 && ref<Key>{Backspace,LeftArrow}.contains(key)) return setIndex(index-1);
		if(index+1<source.count(index+1) && ref<Key>{Return,RightArrow}.contains(key)) return setIndex(index+1);
		if(index+1<source.count(index+1) && key==End) return setIndex(source.count(index+1)-1);
        return false;
    }
};
