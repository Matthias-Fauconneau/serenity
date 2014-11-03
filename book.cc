/// \file album.cc Photo album
#include "image-folder.h"
#include "operation.h"
#include "source-view.h"
#include "layout.h"
#include "pdf.h"

struct PropertyGroup : GroupSource {
	PropertySource& source;
	PropertyGroup(PropertySource& source) : source(source) {}

	array<array<size_t>> groups;

	size_t count(size_t need=-1) override { while(groups.size < need && nextGroup()) {} return groups.size; }
	ref<size_t> operator()(size_t groupIndex) override {
		while(groups.size <= groupIndex) assert_( nextGroup() );
		assert_(groupIndex < groups.size);
		return groups[groupIndex];
	}
	int64 time(size_t groupIndex) override { return max(apply(operator()(groupIndex), [this](size_t index) { return source.time(index); })); }

	bool nextGroup() {
		size_t start = groups ? groups.last().last()+1 : 0;
		if(start == source.count()) return false; // Drained source
		size_t last = start+1;
		for(; last < source.count(); last++) if(source.at(last) != source.at(start)) break;
		assert_(last <= source.count());
		array<size_t> group;
		for(size_t index: range(start, last)) group.append(index);
		assert_(group.size >= 1);
		groups.append(move(group));
		return true;
	}
};

struct SourceImageView : Widget, Poll {
	ImageRGBSource& source;
	size_t index = 0;
	function<void()> onLoad;

	SourceImageView(ImageRGBSource& source, size_t index) : source(source), index(index) { assert_(index < source.count()); }
	SourceImageView(ImageRGBSource& source, size_t index, Window& window) : SourceImageView(source, index) {
		onLoad = [&]{window.render();};
	}

	int2 size;
	SourceImageRGB image; // Holds memory map reference

	int2 sizeHint(int2 size) { assert_(index < source.count()); return source.size(index, size); }
	Graphics graphics(int2 size) override {
		Graphics graphics;
		if(!onLoad) {
			assert_(index < source.count());
			image = source.image(index, size);
		}
		if(image) {
			vec2 target = vec2(image.size)*min(float(size.x)/float(image.size.x), float(size.y)/float(image.size.y));
			graphics.blits.append((vec2(size)-target)/2.f, target, share(image));
		} else { this->size=size; queue(); assert_(onLoad); } // Progressive load
		return graphics;
	}
	void event() {
		assert_(index < source.count());
		image = source.image(index, size);
		assert_(onLoad);
		if(onLoad) onLoad();
	}
};

/// Browses a collection displaying a single element
generic struct Book : array<T>, Widget {
	using array<T>::array;
	size_t index = 0;
	Widget& active() { assert_(index < array<T>::size); return array<T>::at(index); }

	int2 sizeHint(int2 size) { return active().sizeHint(size); }
	Graphics graphics(int2 size) { return active().graphics(size); }
	Graphics graphics(int2 size, Rect clip) { return active().graphics(size, clip); }
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		focus = this;
		return active().mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) {
		size_t previousIndex = index;
		if(key==LeftArrow) index=max(0, int(index)-1);
		if(key==RightArrow) index=min<int>(array<T>::size-1, index+1);
		return active().keyPress(key, modifiers) || previousIndex != index;
	}
	bool keyRelease(Key key, Modifiers modifiers) { return active().keyRelease(key, modifiers); }
};

typedef UniformGrid<SourceImageView> Page;

struct PhotoBook {
	Folder folder {"Pictures", home()};
	ImageFolder source {folder};
	PropertyGroup groups {source};
};

struct PhotoBookPreview : PhotoBook, Application {
	Book<Page> book = apply(groups.count(), [&](size_t groupIndex) {
			return Page(apply(groups(groupIndex),[&](size_t imageIndex) {
							return SourceImageView(source, imageIndex, window);
						}), true, true); });

	Window window {&book, int2(1050), []{return "Album"__;}};
	PhotoBookPreview() { window.background = Window::White; }
};
registerApplication(PhotoBookPreview);

struct PhotoBookExport : PhotoBook, Application {
	Book<Page> book = apply(groups.count(), [&](size_t groupIndex) {
			return Page(apply(groups(groupIndex),[&](size_t imageIndex) {
							return SourceImageView(source, imageIndex);
						}), true, true); });

	PhotoBookExport() {
		int2 size = 1050;
		buffer<Graphics> pages = apply(book.slice(0,1), [=](Widget& page) { return page.graphics(size, Rect(size)); });
		writeFile("book.pdf", toPDF(size, pages), home(), true);
	}
};
registerApplication(PhotoBookExport, export);
