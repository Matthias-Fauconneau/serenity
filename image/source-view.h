#pragma once
#include "source.h"
#include "interface.h"
#include "window.h"

/// Displays an image collection
struct GenericImageSourceView : ImageView {
	GenericImageSource& source;
	size_t ownIndex;
	Index index;
	SourceImageRGB image; // Holds memory map reference

	GenericImageSourceView(GenericImageSource& source, size_t* index=0) : source(source), index(index?:&ownIndex) {}

    // Content

    String title() override {
		if(!source.count()) return String();
		return str(index+1,'/',source.count(), source.elementName(min<size_t>(index, source.count(1)-1)), source.name());
    }

    int2 sizeHint(int2 size) override {
        assert_(size);
		int2 maximum = source.maximumSize();
		/*int downscaleFactor = min(max((maximum.x+size.x-1)/size.x, (maximum.y+size.y-1)/size.y), 16);
        int2 hint = maximum/downscaleFactor;
		assert_(hint<=size, maximum, size, downscaleFactor, maximum/downscaleFactor);*/
		return int2(-maximum.x, -size.y); //hint;
    }

	virtual void update(size_t index, int2 size) abstract;

	shared<Graphics> graphics(int2 size) override {
		if(!source.count(1)) return shared<Graphics>();
		update(index, size);
		ImageView::image = share(image);
		//assert_(image.size.x <= size.x || image.size.y <= size.y, image.size, size);
        return ImageView::graphics(size);
    }

    // Control

	bool setIndex(size_t value) {
		if(value != this->index) { this->index = value; return true; }
        return false;
    }

    /// Browses source with keys
    bool keyPress(Key key, Modifiers) override {
		if(key==Home && source.count()) return setIndex(0);
		if(key==LeftArrow && int(index)>0) return setIndex(index-1);
		if(key==RightArrow && index+1<source.count(index+2)) return setIndex(index+1);
		if(key==End && index+1<source.count(index+2)) return setIndex(source.count(index+2)-1);
        return false;
    }
};

struct ImageSourceView  : GenericImageSourceView {
	ImageRGBSource& source;
	size_t lastImageIndex = -1;

	ImageSourceView(ImageRGBSource& source, size_t* index=0) : GenericImageSourceView(source, index), source(source) {}

	void update(size_t index, int2 size) override {
		if(lastImageIndex != index) {
			lastImageIndex = index;
			int2 sourceSize = source.size(index);
			int2 targetSize = max(sourceSize*size.x/sourceSize.x, sourceSize*size.y/sourceSize.y); // Fits aspect ratio
			image = source.image(min<size_t>(index, source.count(index+1)-1), targetSize);
		}
	}
};

struct ImageGroupSourceView  : GenericImageSourceView {
	ImageRGBGroupSource& source;
	size_t lastGroupIndex = -1;
	size_t ownImageIndex = 0;
	Index imageIndex;
	array<SourceImageRGB> images;

	ImageGroupSourceView(ImageRGBGroupSource& source, size_t* index=0, size_t* imageIndex=0)
		: GenericImageSourceView(source, index), source(source), imageIndex(imageIndex?:&ownImageIndex) {}

	String title() override {
		return str(GenericImageSourceView::title(), imageIndex);
	}

	void update(size_t index, int2 size) override {
		if(lastGroupIndex != index) {
			lastGroupIndex = index;
			int2 sourceSize = source.size(index);
			size = max(sourceSize*size.x/sourceSize.x, sourceSize*size.y/sourceSize.y); // Fits aspect ratio
			images = source.images(min<size_t>(index, source.count(index+1)-1), size);
		}
		assert_(images.size);
		image = share(images[min<size_t>(images.size-1, imageIndex)]);
	}

	/// Cycles between images of a group with the mouse wheel
	bool mouseEvent(int2, int2 size, Event event, Button button, Widget*&) override {
		update(index, size);
		if(event == Press) {
			if(button==WheelDown) { imageIndex=(imageIndex+1)%images.size; return true; }
			if(button==WheelUp) { imageIndex=(imageIndex+images.size-1)%images.size; return true; }
		}
		return false;
	}
};
