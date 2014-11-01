/// \file album.cc Photo album
#include "image-folder.h"
#include "operation.h"
#include "source-view.h"
#include "layout.h"

struct PropertyGroup : GroupSource {
	PropertySource& source;
	PropertyGroup(PropertySource& source) : source(source) {}

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
	ImageFolder source {folder};
	PropertyGroup groups {source};
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
		if(image) {
			assert_(size, image.size);
			int2 target = min(image.size*size.x/image.size.x, image.size*size.y/image.size.y);
			//assert_(target <= size, target, size);
			if(target > int2(9)) {
				assert_(target > int2(9), target, size);
				graphics.blits.append(vec2((size-target)/2), vec2(target), share(image));
			}
		} else { this->size=size; queue(); } // Progressive load
		return graphics;
	}
	void event() {
		image = source.image(index, max(size, int2(64)));
		onLoad();
	}
};

/// Allocates more space to widgets near the cursor
generic struct Magnify : T, virtual Layout {
	int2 cursor;

	using T::T;

	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
		int2 previousCursor = this->cursor;
		this->cursor = cursor;
		return T::mouseEvent(cursor, size, event, button, focus) || cursor != previousCursor;
	}
	array<Rect> layout(int2 size) override {
		auto layout = T::layout(size);
		if(size>int2(1)) for(int axis: range(2)) {
			real x0 = (real)clip(1, cursor[axis], size[axis]-1)/size[axis];
			real a = -1/(2*x0*x0);
			real b = 3/(2*x0);
			real D = 2*sq(x0-1);
			real c = -1/D;
			real d = 3*x0/D;
			real e = (3-6*x0)/D;
			real f = x0*(2*x0-1)/D;

			auto F = [=](real x) { // Smooth step with maximum slope under cursor
				x /= size[axis];
				real y = x < x0 ? a*x*x*x + b*x*x : c*x*x*x + d*x*x + e*x + f;
				return y * size[axis];
			};
			for(Rect& r: layout) { r.min[axis] = F(r.min[axis]), r.max[axis] = F(r.max[axis]); }
		}
		return layout;
	}
};

struct AlbumPreview : Album, Application {
	Magnify<VList<Magnify<HList<SourceImageView>>>> lines = apply(groups.count(), [&](size_t groupIndex) {
			return Magnify<HList<SourceImageView>>(apply(groups(groupIndex),[&](size_t imageIndex) {
				return SourceImageView(source, imageIndex, window);
			})); });
	Window window {&lines, int2(-1), []{return String("Album");}};
	AlbumPreview() { for(auto& line: lines) window.onMotion.append(&line); }
};
registerApplication(AlbumPreview);
