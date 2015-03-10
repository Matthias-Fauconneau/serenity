#include "thread.h"
#include "raw.h"
#include "flic.h"
#include "time.h"

/// Deinterleaves Bayer cells (for better prediction) and shifts 4 zero least significant bits (for better coding)
buffer<uint16> deinterleave(const Image16& source, const int shift=4) {
	buffer<uint16> target(source.Ref::size);
	size_t Y = source.size.y , X = source.size.x;
	mref<uint16> G = target.slice(0, Y*X/2);
	mref<uint16> R = target.slice(Y*X/2, Y/2*X/2);
	mref<uint16> B = target.slice(Y*X/2+Y/2*X/2, Y/2*X/2);
	for(size_t y: range(Y/2)) for(size_t x: range(X/2)) {
		G[(y*2+0)*X/2+x] = source[(y*2+0)*X+(x*2+0)] >> shift;
		R[(y*1+0)*X/2+x] = source[(y*2+0)*X+(x*2+1)] >> shift;
		B[(y*1+0)*X/2+x] = source[(y*2+1)*X+(x*2+0)] >> shift;
		G[(y*2+1)*X/2+x] = source[(y*2+1)*X+(x*2+1)] >> shift;
	}
	return target;
}

/// Reinterleaves Bayer cells and shifts (for compatibility with original raw16 files)
Image16 interleave(ref<uint16> source, int2 size, const int shift=4) {
	Image16 target(size);
	size_t Y = size.y , X = size.x;
	ref<uint16> G = source.slice(0, Y*X/2);
	ref<uint16> R = source.slice(Y*X/2, Y/2*X/2);
	ref<uint16> B = source.slice(Y*X/2+Y/2*X/2, Y/2*X/2);
	for(size_t y: range(Y/2)) for(size_t x: range(X/2)) {
		target[(y*2+0)*X+(x*2+0)] = G[(y*2+0)*X/2+x] << shift;
		target[(y*2+0)*X+(x*2+1)] = R[(y*1+0)*X/2+x] << shift;
		target[(y*2+1)*X+(x*2+0)] = B[(y*1+0)*X/2+x] << shift;
		target[(y*2+1)*X+(x*2+1)] = G[(y*2+1)*X/2+x] << shift;
	}
	return target;
}

struct Codec {
	Codec() {
		string fileName = arguments()[0];
		Map file (fileName);
		/**/  if(endsWith(fileName, ".raw16")) {
			ref<uint16> data = cast<uint16>(file);
			Image16 source (unsafeRef(data.slice(0, Raw::size.y*Raw::size.x)), Raw::size);
			buffer<byte> encoded = Encoder(source.Ref::size*2).write(deinterleave(source)).end();
			assert_(interleave(Decoder(encoded).read(buffer<uint16>(Raw::size.y*Raw::size.x)), Raw::size) == source);
			ref<byte> trailer = cast<byte>(data.slice(Raw::size.y*Raw::size.x));
			writeFile(section(fileName,'.')+".eg6", encoded + trailer);
		}
		else if(endsWith(fileName, ".eg6")) {
			buffer<uint16> data = interleave(Decoder(file.slice(0, file.size-256)).read(buffer<uint16>(Raw::size.y*Raw::size.x)), Raw::size);
			ref<byte> trailer = file.slice(file.size-256, 256);
			writeFile(section(arguments().size > 1 ? arguments()[1] : fileName,'.')+".raw16", cast<byte>(data) + trailer);
		}
		else error("Unknown format", fileName, "Expected *.raw16 or *.eg6");
	}
} app;
