#include "window.h"
#include "time.h"

struct PlotTest : Widget {
	Window window{this,int2(512,512)};

	int2 cursor;
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
		int2 previousCursor = this->cursor;
		this->cursor = cursor;
		return cursor != previousCursor;
	}

	int2 sizeHint(int2) override { return 0; }
	Graphics graphics(int2) override {
		Graphics graphics;
		real x0 = (real)clip(1, cursor[axis], size[axis]-1)/size[axis];
		real a = -1/(2*x0*x0);
		real b = 3/(2*x0);
		real D = 2*sq(x0-1);
		real c = -1/D;
		real d = 3*x0/D;
		real e = (3-6*x0)/D;
		real f = x0*(2*x0-1)/D;

		auto F = [=](real x) { // Smooth step with maximum under cursor
			x /= size[axis];
			real y = x < x0 ? a*x*x*x + b*x*x : c*x*x*x + d*x*x + e*x + f;
			const real a = 0x1p-6;
			return ((1-a)*x+a*y) * size[axis];
		};
		for(Rect& r: layout) {
			log_(str(r.size()));
			r.min[axis] = F(r.min[axis]);
			r.max[axis] = F(r.max[axis]);
			log(" ", r.size());
		}
	}

        return graphics;
	}
} test;
