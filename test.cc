#include "window.h"
#include "time.h"

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
		for(int x: range(1,size.x-1)) graphics.lines.append({vec2(x,size.y-1-step(x, cursor.x, size.x)),
															 vec2(x+1,size.y-1-step(x+1, cursor.x, size.x)), green});
        return graphics;
	}
} test;
