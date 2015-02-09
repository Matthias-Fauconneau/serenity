#include "jpeg.h"
#include "interface.h"
#include "window.h"

ImageF whiteBalance(const ImageF& source, const ImageF& reference) {
	ImageF target (source.size);
	v4sf mean = ::mean(reference);
	for(size_t i: range(source.Ref::size)) target[i] = source[i] / mean;
	return target;
}

struct WhiteBalance {
	string name = section(arguments()[0],'.');
	ImageF source = convert(decodeImage(Map(name+".jpg"_)));
	ImageF reference = convert(decodeImage(Map(name+".white-balance-reference.jpg"_)));
	Image target = convert(whiteBalance(source, reference));
};

struct WindowView : ImageView { Window window {this}; WindowView(Image&& image) : ImageView(move(image)) {} };
//struct WhiteBalancePreview : WhiteBalance, WindowView { WhiteBalancePreview() : WindowView(move(target)) {} } app;

struct WhiteBalanceExport : WhiteBalance {
	WhiteBalanceExport() { writeFile(name+".white-balance-corrected.jpg"_, encodeJPEG(target)); }
} app;
