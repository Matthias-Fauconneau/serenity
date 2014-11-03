#include "prism.h"
#include "color.h"

void Prism::apply(ref<ImageF> Y, ref<ImageF> X) const {
	if(X.size <= 3) {
		for(size_t index: range(X.size)) Y[index].copy(X[index]);
		for(size_t index: range(X.size, Y.size)) Y[index].clear();
	} else {
		assert_(Y.size == 3);
		for(size_t index: range(X.size)) {
			vec3 color = LChuvtoBGR(53, 179, 2*PI*index/X.size);
			assert_(isNumber(color));
			for(size_t component: range(3)) {
				assert_(X[index].size == Y[component].size);
				if(index == 0) parallel::apply(Y[component], [=](float x) { return x * color[component]; }, X[index]);
				else parallel::apply(Y[component], [=](float y, float x) { return y + x * color[component]; }, Y[component], X[index]);
			}
		}
		for(size_t component: range(3)) for(auto v: Y[component]) assert_(isNumber(v));
	}
}
