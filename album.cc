/// \file album.cc Photo album
#include "image-folder.h"
#include "source-view.h"
#include "layout.h"

struct Album {
	Folder folder {"Pictures", home()};
	ImageFolder source { folder };
};

struct SourceImageView : Widget, Poll {
	ImageRGBSource* source;
	size_t index;
	function<void()> onLoad;

	SourceImageView(ImageRGBSource& source, size_t index, Window& window)
		: source(&source), index(index), onLoad([&]{window.render();}) {}

	int2 size;
	SourceImageRGB image; // Holds memory map reference

	int2 sizeHint(int2 size) { return source->size(index, size); }
	Graphics graphics(int2 size) override {
		Graphics graphics;
		if(image) graphics.blits.append(vec2(max(vec2(0),vec2((size-image.size)/2))), vec2(image.size), share(image));
		else { this->size=size; queue(); } // Progressive load
		return graphics;
	}
	void event() {
		image = source->image(index, size);
		onLoad();
	}
};

struct ImageGridLayout : GridLayout {
	buffer<SourceImageView> images;

	ImageGridLayout(ImageRGBSource& source, Window& window, int width = 0)
		: GridLayout(false, false, width), images(apply(source.count(), [&](size_t index) { return SourceImageView(source, index, window); })) {}

	size_t count() const override { return images.size; }
	Widget& at(size_t index) override { return images[index]; }
};

struct AlbumPreview : Album, Application {
	Scroll<ImageGridLayout> view {source, window, 10};
	Window window {&view, 1050};
};
registerApplication(AlbumPreview);
