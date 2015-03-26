#include "layout.h"
#include "image.h"

struct LayoutRender : Layout {
	Image target;

	LayoutRender(Layout&& _this, const float _mmPx, const float _inchPx=0);
};
