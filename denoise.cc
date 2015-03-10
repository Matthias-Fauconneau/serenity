/// \file denoise.cc Noise reduction
#include "image.h"
#include "parallel.h"
#include "thread.h"
#include "raw.h"
#include "demosaic.h"
#include "IT8.h"

/// Noise reduction using non-local means
// FIXME: CFA
Image4f NLM(Image4f&& target, const Image4f& source) {
	constexpr int pHL = 2; //2-5 Patch half length (d+1+d)
	constexpr int wHL = 2; //10-17 Window half length (d+1+d)
	constexpr float h2 = sq(1./256); // ~ sigma/2 (sigma ~ 2-25)
	int nX=source.size.x, nY=source.size.y;
	chunk_parallel(nY, [&](uint, int Y) {
		if(Y<wHL+pHL || Y>=nY-wHL-pHL) {
			for(int c: range(3)) for(int X: range(nX))  target(X, Y)[c] = source(X,Y)[c]; // FIXME
			return;
		}
		for(int c: range(3)) for(int X: range(0, wHL+pHL)) target(X, Y)[c] = source(X,Y)[c]; // FIXME
		for(int c: range(3)) for(int X: range(nX-wHL-pHL, nX)) target(X, Y)[c] = source(X,Y)[c]; // FIXME
		for(int X: range(wHL+pHL, nX-wHL-pHL)) {
			float weights[sq(wHL+1+wHL)];
			float weightSum = 0;
			for(int dY=-wHL; dY<=wHL; dY++) {
				for(int dX=-wHL; dX<=wHL; dX++) {
					float d = 0; // Patch distance
					for(int c: range(3)) {
						for(int dy=-pHL; dy<=pHL; dy++) {
							for(int dx=-pHL; dx<=pHL; dx++) {
								d += sq( source(X+dx, Y+dy)[c] - source(X+dX+dx, Y+dY+dy)[c] );
							}
						}
					}
					float weight = exp(-sq(d)/h2);
					weights[(wHL+dY)*(wHL+1+wHL)+(wHL+dX)] = weight;
					weightSum += weight;
				}
			}
			for(int c: range(3)) {
				float sum = 0;
				for(int dY=-wHL; dY<=wHL; dY++) {
					for(int dX=-wHL; dX<=wHL; dX++) {
						float weight = weights[(wHL+dY)*(wHL+1+wHL)+(wHL+dX)];
						sum += weight * source(X+dX, Y+dY)[c];
					}
				}
				target(X, Y)[c] = sum / weightSum;
			}
		}
	});
	return move(target);
}
Image4f NLM(const Image4f& source) { return NLM(source.size, source); }

struct Denoise : Application {
	string fileName = arguments()[0];
	map<String, Image> images;
	Denoise() {
		string name = section(fileName,'.');
		Raw raw {Map(fileName)};
		Image4f source = demosaic(raw);
		IT8 it8(source, readFile("R100604.txt"));
		mat4 rawRGBtosRGB =mat4(sRGB) * it8.rawRGBtoXYZ;
		if(0) {
			images.insert(name+".source", convert(convert(it8.chart, rawRGBtosRGB)));
			images.insert(name+".target", convert(convert(NLM(it8.chart), rawRGBtosRGB)));
		} else {
			images.insert(name+".source", convert(convert(source, rawRGBtosRGB)));
			images.insert(name+".target", convert(convert(NLM(source), rawRGBtosRGB)));
		}
	}
};

#if 1
#include "view.h"
struct Preview : Denoise, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);
#endif
