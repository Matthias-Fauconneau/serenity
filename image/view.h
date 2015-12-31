#include "interface.h"
#include "window.h"

struct WindowCycleView {
	buffer<ImageView> views;
	WidgetCycle layout;
	Window window {&layout, int2(1024, 768)};
	WindowCycleView(const map<String, Image>& images)
		: views(apply(images.size(), [&](size_t i) { return ImageView(share(images.values[i]), images.keys[i]); })),
		  layout(toWidgets<ImageView>(views)) {}
};
