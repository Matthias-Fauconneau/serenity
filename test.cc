#include "window.h"
#include "time.h"

/*/// Explicit evaluation of y(x) for a quadratic bezier curve
// solve([x = (1-t)^2*p0x + t*(1-t)*p1x+ t*t*p2x, y=(1-t)^2*p0y + t*(1-t)*p1y+ t*t*p2y], [t, y])
static real bezier(real p0x, real p0y, real p1x, real p1y, real p2x, real p2y, real x) {
	real s2 = 4*p2x*x-4*p1x*x+4*p0x*x-4*p0x*p2x+sq(p1x);
	assert_(s2 >= 0, s2, p0x, p0y, p1x, p1y, p2x, p2y, x);
	real s = ::sqrt(s2);
	real d = (2*sq(p2x)+(4*p0x-4*p1x)*p2x+2*sq(p1x)-4*p0x*p1x+2*sq(p0x));
	assert_(d != 0);
	return ( p2x*(p1y*(-p1x+4*p0x)+p0y*(-2*s-2*p0x)-2*p0y*p1x)
			 + p2y*(2*p0x*s+p1x*(-s-2*p0x)-2*p0x*p2x+sq(p1x)+2*sq(p0x))
			 + p1y*(-p0x*s-p0x*p1x)
			 + p0y*p1x*s
			 + ((2*p2x-2*p1x+2*p0x)*p2y+(2*p0y-2*p1y)*p2x+(2*p1x-2*p0x)*p1y-2*p0y*p1x+2*p0x*p0y)*x
			 + 2*p0y*sq(p2x)+p0y*sq(p1x)) / d;
}*/

static vec2 bezier(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) { return cb(1-t)*p0 + 3*sq(1-t)*t*p1+ 3*(1-t)*sq(t)*p2 + cb(t)*p3; }
float bezierY(vec2 P0, vec2 P1, vec2 P2, vec2 P3, float x) {
	const int precision = 16;
	for(float i: range(precision)) {
		vec2 p0 = bezier(P0, P1, P2, P3, i/precision);
		vec2 p1 = bezier(P0, P1, P2, P3, (i+1)/precision);
		if(p0.x <= x && x <= p1.x) return p0.y + (p1.y-p0.y)*(x-p0.x)/(p1.x-p0.x);
	}
	return nan;
}
float step(float x, float x0, float x1) {
	{float y = bezierY(vec2(0), vec2(x0,0), vec2(x0,0), vec2(x0), x);
		if(!isNaN(y)) return y;}
	{float y = bezierY(vec2(x0), vec2(x0,x1), vec2(x0,x1), vec2(x1), x);
		if(!isNaN(y)) return y;
		error(y);
	}
}


struct PlotTest : Widget {
	Window window{this,int2(1024)};

	int2 cursor;
	bool mouseEvent(int2 cursor, int2, Event, Button, Widget*&) override {
		int2 previousCursor = this->cursor;
		this->cursor = cursor;
		return cursor != previousCursor;
	}

	int2 sizeHint(int2) override { return 0; }
	Graphics graphics(int2 size) override {
		Graphics graphics;
		real x0 = (real)clip(1, cursor[0], size[0]-1)/size[0];
		for(int curve: range(2)) {
			if(curve == 0) {
				real x = x0;
				real a = -1/(2*x*x);
				real b = 3/(2*x);
				real D = 2*sq(x-1);
				real c = -1/D;
				real d = 3*x/D;
				real e = (3-6*x)/D;
				real f = x*(2*x-1)/D;

				auto F = [=](real x) { // Smooth step with maximum under cursor
					x /= size[0];
					real y = x < x0 ? a*x*x*x + b*x*x : c*x*x*x + d*x*x + e*x + f;
					return round(y * size[0]);
				};
				for(int x: range(size.x)) graphics.lines.append({vec2(x,size.y-1-F(x)),vec2(x+1,size.y-1-F(x+1)), blue});
			} else {
				for(int x: range(1,size.x-1)) graphics.lines.append({vec2(x,size.y-1-step(x, cursor.x, size.x)),
																	 vec2(x+1,size.y-1-step(x+1, cursor.x, size.x)), green});
			}
		}
        return graphics;
	}
} test;
