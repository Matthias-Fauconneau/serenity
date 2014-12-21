/// \file denoise.cc Noise reduction
#include "operation.h"
#include "image-folder.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/// Noise reduction using median
void median(const Image& target, const Image& source) {
	constexpr int radius = 1;
	assert_(source.size>int2(radius+1+radius));
	target.copy(source); // FIXME: border
	for(size_t c: range(3)) {
		for(size_t y: range(radius, source.size.y-radius)) {
			uint8 histogram8[1<<8] = {};
			const int X = source.width;
			const byte4* const sourceY = source.data + y*X;
			for(int dy=-radius; dy<=radius; dy++) for(int x=0; x<radius+radius; x++) { // Initializes histograms
				uint8 value = sourceY[dy*X+x][c];
				histogram8[value]++;
			}
			for(int x: range(radius, source.size.x-radius)) {
				for(int dy=-radius; dy<=radius; dy++) { // Updates histogram with values entering the window
					uint8 value = sourceY[dy*X+x+radius][c];
					histogram8[value]++;
				}
				uint count=0;
				for(uint i: range(256)) {
					count += histogram8[i];
					if(count > sq(radius+1+radius)/2) {
						target[y*X+x][c] = i;
						break;
					}
				}
				for(int dy=-radius; dy<=radius; dy++) { // Updates histogram with values leaving the window
					uint8 value = sourceY[dy*X+x-radius][c];
					histogram8[value]--;
				}
			}
		}
	}
}
struct Median : ImageRGBOperator, OperatorT<Median> { void apply(const Image& Y, const Image& X) const override { median(Y, X); } };

/// Noise reduction using non-local means
void NLM(ref<ImageF> V, ref<ImageF> U) {
	constexpr int pHL = 1; //-5 Patch half length (d+1+d)
	constexpr int wHL = 1; //-17 Window half length (d+1+d)
	constexpr float h2 = sq(1); // ~ sigma/2 (sigma ~ 2-25)
	int nX=U[0].size.x, nY=U[0].size.y;
	chunk_parallel(nY, [=](uint, int Y) {
		if(Y<wHL+pHL || Y>=nY-wHL-pHL) {
			for(int c: range(3)) for(int X: range(nX))  V[c](X, Y) = U[c](X,Y); // FIXME
			return;
		}
		for(int c: range(3)) for(int X: range(0, wHL+pHL)) V[c](X, Y) = U[c](X,Y); // FIXME
		for(int c: range(3)) for(int X: range(nX-wHL-pHL, nX)) V[c](X, Y) = U[c](X,Y); // FIXME
		for(int X: range(wHL+pHL, nX-wHL-pHL)) {
			float weights[sq(wHL+1+wHL)];
			float weightSum = 0;
			for(int dY=-wHL; dY<=wHL; dY++) {
				for(int dX=-wHL; dX<=wHL; dX++) {
					float d = 0; // Patch distance
					for(int c: range(3)) {
						for(int dy=-pHL; dy<=pHL; dy++) {
							for(int dx=-pHL; dx<=pHL; dx++) {
								d += sq( U[c](X+dx, Y+dy) - U[c](X+dX+dx, Y+dY+dy) );
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
						sum += weight * U[c](X+dX, Y+dY);
					}
				}
				V[c](X, Y) = sum / weightSum;
			}
		}
	});
}
struct NonLocalMeans : ImageOperator, OperatorT<NonLocalMeans> {
	size_t inputs() const override { return 3; }
	size_t outputs() const override { return 3; }
	void apply(ref<ImageF> Y, ref<ImageF> X) const override { NLM(Y, X); }
};

struct Denoise {
	Folder folder {"Documents/Pictures/Denoise", home()};
	ImageFolder source { folder };
	ImageRGBOperationT<Median> median {source};
	ImageOperationT<NonLocalMeans> NLM {source};
};

struct DenoisePreview : Denoise, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageSourceView sourceView {source, &index};
	ImageSourceView medianView {median, &index};
	sRGBOperation target { Denoise::NLM };
	ImageSourceView targetView {target, &index};
	WidgetCycle toggleView {{&targetView, &medianView, &sourceView}};
	Window window {&toggleView, -1, [this]{ return toggleView.title(); } };
};
registerApplication(DenoisePreview);

/*
struct DenoiseExport : Denoise, Application {
	sRGBOperation target { Denoise::NLM };
	DenoiseExport() {
		Folder output ("Export", folder, true);
		for(size_t index: range(target.count())) {
			String name = target.elementName(index);
			Time correctionTime;
			SourceImageRGB image = target.image(index);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/target.count())+'%', '\t',index+1,'/',target.count(),
				'\t',target.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
registerApplication(DenoiseExport, export);
*/
