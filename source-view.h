#pragma once
#include "source.h"
#include "interface.h"
#include "window.h"

/// Displays an image collection
struct ImageSourceView : ImageView, Poll {
	ImageRGBSource& source;
    Index index;
	size_t imageIndex = -1;
    SourceImageRGB image; // Holds memory map reference
    int2 size = 0;
    function<void()> contentChanged;
	int2 hint = 0;
	bool noCacheWrite = true;

	ImageSourceView(ImageRGBSource& source, size_t* index, function<void()> contentChanged, int2 hint=0)
		: source(source), index(index), contentChanged(contentChanged), hint(hint) {}
	ImageSourceView(ImageRGBSource& source, size_t* index, Window& window, int2 hint=0)
		: ImageSourceView(source, index, {&window, &Window::render}, hint) {}

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
		assert_(source.maximumSize().x/hint.x <= 256);
#endif
        imageIndex = index;
		image = source.image(min<size_t>(index, source.count(index+1)-1), hint, noCacheWrite);
        ImageView::image = share(image);
        contentChanged();
    }

    // Content

    String title() override {
		if(!source.count()) return String();
		return str(index+1,'/',source.count(), source.elementName(min<size_t>(index, source.count(1)-1)));
    }

    int2 sizeHint(int2 size) override {
        assert_(size);
        int2 maximum = source.maximumSize();
        int downscaleFactor = min(max((maximum.x+size.x-1)/size.x, (maximum.y+size.y-1)/size.y), 16);
        int2 hint = maximum/downscaleFactor;
        assert_(hint<=size, maximum, size, downscaleFactor, maximum/downscaleFactor);
		return this->hint ? this->hint : hint;
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

	bool setIndex(size_t value) {
		if(value != this->index) { this->index = value; if(value != imageIndex) queue(); return true; }
        return false;
    }

	/// Browses source by dragging mouse horizontally over image view (like an hidden slider)
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*&) override {
		return (event==Motion && button) ? setIndex(source.count()*min(size.x-1,cursor.x)/size.x) : false;
    }

    /// Browses source with keys
    bool keyPress(Key key, Modifiers) override {
		if(key==Home && source.count()) return setIndex(0);
		if(ref<Key>{Backspace,LeftArrow}.contains(key) && int(index)>0) return setIndex(index-1);
		if(ref<Key>{Return,RightArrow}.contains(key) && index+1<source.count(index+2)) return setIndex(index+1);
		if(key==End && index+1<source.count(index+2)) return setIndex(source.count(index+2)-1);
        return false;
    }
};
