/// \file album.cc Photo album
#include "image-folder.h"
#include "operation.h"
#include "source-view.h"
#include "layout.h"

struct PropertySplit : GroupSource {
	PropertySource& source;
	PropertySplit(PropertySource& source) : source(source) {}

	array<array<size_t>> groups;

	size_t count(size_t need=-1) override { while(groups.size < need && nextGroup()) {} return groups.size; }
	array<size_t> operator()(size_t groupIndex) override {
		while(groups.size <= groupIndex) assert_( nextGroup() );
		return copy(groups[groupIndex]);
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

struct Album {
	Folder folder {"Pictures", home()};
	ImageFolder source { folder };
	PropertySplit groups { source };
};

struct SourceImageView : Widget, Poll {
	ImageRGBSource& source;
	size_t index;
	function<void()> onLoad;

	SourceImageView(ImageRGBSource& source, size_t index, Window& window) : source(source), index(index), onLoad([&]{window.render();}) {}

	int2 size;
	SourceImageRGB image; // Holds memory map reference

	int2 sizeHint(int2 size) { return source.size(index, size); }
	Graphics graphics(int2 size) override {
		Graphics graphics;
		if(image) graphics.blits.append(vec2(max(vec2(0),vec2((size-image.size)/2))), vec2(image.size), share(image));
		else { this->size=size; queue(); } // Progressive load
		return graphics;
	}
	void event() {
		image = source.image(index, size);
		onLoad();
	}
};

struct AlbumPreview : Album, Application {
	Scroll<VList<HList<SourceImageView>>> view =
		apply(groups.count(), [&](size_t groupIndex) {
			return HList<SourceImageView>(apply(groups(groupIndex),[&](size_t imageIndex) {
				return SourceImageView(source, imageIndex, window);
			}));
		});
	Window window {&view, 1050};
};
registerApplication(AlbumPreview);
