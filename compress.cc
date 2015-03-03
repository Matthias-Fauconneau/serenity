#include "thread.h"
#include "raw.h"
#include "flic.h"
#include "time.h"

struct List {
	List() {
		for(string file: currentWorkingDirectory().list(Files|Recursive)) {
			if(!endsWith(file, ".raw16")) continue;
			Raw raw(Map(file), false);
			log(file,"\t", int(round(raw.exposure*1e3)),"\t", raw.gain, raw.gainDiv, raw.temperature);
		}
	}
};

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

struct Compress {
	Compress() {
		uint64 dataSize = 0, encodedSize = 0;
		Time encodeTime (false), decodeTime (false);
		for(string fileName: currentWorkingDirectory().list(Files)) {
			if(!endsWith(fileName, ".raw16")) continue;
			Map raw(fileName);
			dataSize += raw.size;
			ref<uint16> data = cast<uint16>(raw);
			Image16 source (unsafeRef(data.slice(0, Raw::size.y*Raw::size.x)), Raw::size);
			encodeTime.start();
			buffer<byte> encoded = Encoder(source.Ref::size*2).write(deinterleave(source)).end();
			encodedSize += encoded.size+256;
			encodeTime.stop();
			log(fileName,"\t", raw.size/1024./1024., encoded.size/1024./1024., int(round(100.*encoded.size/raw.size)));
			buffer<uint16> planar (source.Ref::size);
			decodeTime.start();
			FLIC(encoded).read(planar);
			decodeTime.stop();
			assert_(interleave(planar, Raw::size) == source);
			//ref<byte> metadata = cast<byte>(data.slice(Raw::size.y*Raw::size.x));
			//writeFile(section(fileName,'.')+".eg10", encoded + metadata);
		}
		assert_(dataSize == 3*(3072*4096*2+256));
		const size_t zipSize = 58863037, xzSize = 48869840;
		log(encodedSize/1024./1024, "\t/ deflate (GZ)", (real)encodedSize/zipSize, "\t/ 12bit (raw)", encodedSize/(dataSize*12/16.),  "\t/ LZMA (XZ)", (real)encodedSize/xzSize);
		log(encodedSize/1024./1024/encodeTime.toReal(), encodedSize/1024./1024/decodeTime.toReal());
	}
} app;
