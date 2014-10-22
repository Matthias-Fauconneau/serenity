#include "window.h"
#include "time.h"

struct BackgroundTest : Widget {
	Window window{this,int2(512,512)};
	int2 sizeHint(int2) override { return 0; }
	Graphics graphics(int2) override {
		Graphics graphics;
        return graphics;
	}
} test;
