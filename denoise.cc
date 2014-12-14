/// \file denoise.cc Noise reduction
#include "operation.h"
#include "image-folder.h"
#include "source-view.h"
#include "jpeg-encoder.h"

/// Denoises an 8bit image using a median filter
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

struct Denoise {
	Folder folder {"Documents/Pictures/Denoise", home()};
	ImageFolder source { folder };
	ImageRGBOperationT<Median> target {source};
};

struct DenoisePreview : Denoise, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageSourceView sourceView {source, &index};
	ImageSourceView targetView {target, &index};
	WidgetCycle toggleView {{&targetView, &sourceView}};
	Window window {&toggleView, -1, [this]{ return toggleView.title(); } };
};
registerApplication(DenoisePreview);

struct DenoiseExport : Denoise, Application {
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
