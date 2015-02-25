#include "jpeg.h"
#include "interface.h"
#include "window.h"

Image4f whiteBalance(const Image4f& source, const Image4f& reference) {
    Image4f target (source.size);
	v4sf mean = ::mean(reference);
	for(size_t i: range(source.Ref::size)) target[i] = source[i] / mean;
	return target;
}

struct WhiteBalance {
	string name = section(arguments()[0],'.');
    Image4f source = convert(decodeImage(Map(name+".jpg"_)));
    Image4f reference = convert(decodeImage(Map(name+".white-balance-reference.jpg"_)));
	Image target = convert(whiteBalance(source, reference));
};

struct WindowView : ImageView { Window window {this}; WindowView(Image&& image) : ImageView(move(image)) {} };
//struct WhiteBalancePreview : WhiteBalance, WindowView { WhiteBalancePreview() : WindowView(move(target)) {} } app;

struct WhiteBalanceExport : WhiteBalance {
	WhiteBalanceExport() { writeFile(name+".white-balance-corrected.jpg"_, encodeJPEG(target)); }
} app;
