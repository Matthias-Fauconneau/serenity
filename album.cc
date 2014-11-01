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
			vec2 target = vec2(image.size)*min(float(size.x)/float(image.size.x), float(size.y)/float(image.size.y));
			if(target > vec2(0)) graphics.blits.append((vec2(size)-target)/2.f, target, share(image));
		} else { this->size=size; queue(); } // Progressive load
		return graphics;
	}
	void event() {
		image = source.image(index, max(size, int2(256)));
		onLoad();
	}
};

static vec2 bezier(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) { return cb(1-t)*p0 + 3*sq(1-t)*t*p1+ 3*(1-t)*sq(t)*p2 + cb(t)*p3; }
float bezierY(vec2 P0, vec2 P1, vec2 P2, vec2 P3, float x) {
	const int precision = 2;
	for(float i: range(precision)) { //FIXME: binary search
		vec2 p0 = bezier(P0, P1, P2, P3, i/precision);
		vec2 p1 = bezier(P0, P1, P2, P3, (i+1)/precision);
		if(p0.x <= x && x <= p1.x) {
			if(p0.x == p1.x) { assert_(p0.y == p1.y); return p0.y; }
			return p0.y + (p1.y-p0.y)*(x-p0.x)/(p1.x-p0.x);
		}
	}
	return nan;
}
float step(float x, float min, float mid, float max) {
	mid = clip(min, mid, max);
	{float y = bezierY(vec2(min), vec2(mid,min), vec2(mid,min), vec2(mid), x);
		if(!isNaN(y)) return y;}
	{float y = bezierY(vec2(mid), vec2(mid,max), vec2(mid,max), vec2(max), x);
		if(!isNaN(y)) return y;}
	error(x, min, mid, max);
}

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
		for(int axis: range(2)) if(size[axis] > 1) {
			int min=0, max=size[axis]-1;
			for(Rect r: layout) { min=::min(min, r.min[axis]), max=::max(max, r.max[axis]-1); }
			for(Rect& r: layout) {
				r.min[axis] = step(r.min[axis], min, cursor[axis], max);
				r.max[axis] = step(r.max[axis]-1, min, cursor[axis], max)+1;
			}
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
