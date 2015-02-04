#include "file.h"
#include "zip.h"
#include "tiff.h"
#include "flic.h"
#include "time.h"

struct Encoder {
	buffer<byte> buffer;

	BitWriter bitIO {buffer}; // Assumes buffer capacity will be large enough
	int predictor = 0;
	uint runLength = 0;

	int2 size = 0;
	size_t valueCount = 0;

	Time encode;
	Encoder() {}
	Encoder(int2 size) : buffer((size_t)size.y*size.x*2), size(size) { // Assumes average bitrate < 16bit / sample (large RLE voids)
		bitIO.write(32, size.x); bitIO.write(32, size.y);
	}
	void write(ref<int16> source) {
		valueCount += source.size;
		assert_(valueCount <= (size_t)size.y*size.x, valueCount, size.y, size.x);
		for(size_t index=0;;) {
			int value, s;
			for(;;) {
				if(index >= source.size) return;
				value = source[index];
				index++;
				s = value - predictor;
				if(s != 0) break;
				runLength++;
			};
			flushRLE();
			predictor = value;
			uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
			uint x = u+1; // 0 is RLE escape
			// Exp Golomb _ r
			x += (1<<EG);
			uint b = 63 - __builtin_clzl(x); // BSR
			bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
		}
	}
	void flushRLE() {
		if(!runLength) return;
		uint rleCodeRepeats = runLength >> RLE; // RLE length = rleCodeRepeats · 2^RLE
		for(;rleCodeRepeats > 0; rleCodeRepeats--) bitIO.write(3, 0b100);
		uint remainingZeroes = runLength & ((1<<RLE)-1);
		for(;remainingZeroes > 0; remainingZeroes--) bitIO.write(3, 0b101); // Explicitly encode remaining zeroes
		runLength = 0;
	}

	::buffer<byte> end() {
		assert_(valueCount == (size_t)size.y*size.x, valueCount, size);
		flushRLE();
		bitIO.flush();
		buffer.size = (byte*)bitIO.pointer - buffer.data;
		buffer.size += 16; // Pads to avoid segfault with "optimized" BitReader (whose last refill may stride end) // FIXME: 7 bytes should be enough
		assert_(buffer.size <= buffer.capacity);
		//log(int(round(buffer.size/(1024.*1024))),"MB", encode, int(round((buffer.size/(1024.*1024))/encode.toReal())),"MB/s"); // 200 MB/s
		return move(buffer);
	}
};

#if 0 // Converts zipped tif tiles to an Exp Golomb RLE image
struct Convert {
	Convert() {
		Encoder encoder;
		static constexpr size_t W=6, H=4; // 6 tiles per band, 4 bands
		for(size_t Y: range(H)) {
			Image16 band; // (W, 1)*tileSize
			for(size_t X: range(W)) {
				buffer<byte> tif = extractZIPFile(Map("dem15.tif.zip/15-"_+char('A'+Y*W+X)+".zip"), "15-"_+char('A'+Y*W+X)+".tif"_);
				Image16 source = parseTIF(tif);
				if(band) assert_(band.size==int2(W,1)*(source.size-int2(1))); // Uniform tile size
				else {
					band = Image16(int2(W,1)*(source.size-int2(1))); log(Y, band.size);
					if(!encoder.buffer) encoder = Encoder(int2(band.size.x, H*band.size.y);
				}
				int16* target = band.begin() + X*(source.size.x-1);
				for(size_t y: range(band.size.y)) for(size_t x: range(source.size.x-1)) target[y*band.size.x+x] = source[y*source.size.x+x];
				log(X, Y, source.size-int2(1));
			}
			encoder.write(band);
		}
		writeFile("dem15.eg2rle6", encoder.end(), currentWorkingDirectory(), true);
	}
} convert;
#endif

#if 1
// Tiles an Exp Golomb RLE image into 2·(2ⁿ)×(2ⁿ) tiles
struct Tile {
	Tile() {
		string name = 0 ? "dem15"_ : "globe30";
		//Map map(name+".eg2rle6"_);
		buffer<byte> map = readFile(name+".eg2rle6"_)+ref<byte>{0,0,0,0,0,0,0,0}; // FIXME
		FLIC source(map);
		int n = 0; while(source.size.y%(1<<n)==0) n++; n--; // Maximum mipmap level count
		int N = 1<<n, tileSize = source.size.y/N;
		log("Tile", source.size, n, N, tileSize);
		assert_(N*tileSize == source.size.y && 2*N*tileSize == source.size.x);
		Time time;
		for(size_t tY: range(N)) {
			buffer<Encoder> encoder (2*N); encoder.clear(tileSize);
			buffer<int16> buffer (tileSize);
			for(size_t unused y: range(tileSize)) {
				for(size_t tX: range(encoder.size)) { // Splits each line into tiles
					source.read(buffer);
					encoder[tX].write(buffer);
				}
			}
			for(size_t tX: range(encoder.size)) writeFile(str(tX, 2u)+","+str(tY, 2u), encoder[tX].end(),
														  Folder(name+"."_+str(N)+".eg2rle6"_, currentWorkingDirectory(), true), true);
			log(str(tY,2u,' '), "/", N);
		}
		log(time);
	}
} tile;
#endif

#if 1
// Downsamples and merges 4 tiles recursively up to top mipmap/quadtree level 2x1
struct Mipmap {
	Mipmap() {
		string name = 0 ? "dem15"_ : "globe30";
		Map map(name+".eg2rle6"_);
		FLIC source(map);
		int n = 0; while(source.size.y%(1<<n)==0) n++; n--; // Maximum mipmap level count
		int N = 1<<n, tileSize = source.size.y/N;
		log("Mipmap", source.size, n, N, tileSize);
		assert_(N*tileSize == source.size.y && 2*N*tileSize == source.size.x);
		Time time;
		for(size_t level: reverse_range(n)) {
			for(size_t tY: range(1<<level)) {
				for(size_t tX: range(2<<level)) {
					Encoder encoder(tileSize);
					::buffer<int16> buffer(tileSize*2*tileSize*2);
					for(size_t unused Y: range(2)) {
						for(size_t unused X: range(2)) {
							Map map(str(tX*2+X, 2u)+","+str(tY*2+Y, 2u), Folder(name+"."_+str(1<<(level+1))+".eg2rle6"_));
							FLIC source(map);
							for(size_t y: range(tileSize)) source.read(buffer.slice((Y*tileSize+y)*tileSize*2+X*tileSize, tileSize));
						}
					}
					// Downsamples
					::buffer<int16> target (tileSize*tileSize); // In place (in source) would be less clear and prevent (TODO) parallelization
					for(size_t y: range(tileSize)) for(size_t x: range(tileSize)) {
						target[y*tileSize+x] =
								(  buffer[(y*2+0)*tileSize*2+(x*2+0)] + buffer[(y*2+0)*tileSize*2+(x*2+1)]
								+ buffer[(y*2+1)*tileSize*2+(x*2+0)] + buffer[(y*2+1)*tileSize*2+(x*2+1)] ) / 4;
					}
					encoder.write(target);
					writeFile(str(tX, 2u)+","+str(tY, 2u), encoder.end(),
							  Folder(name+"."_+str(1<<level)+".eg2rle6"_, currentWorkingDirectory(), true), true);
				}
				log(str(tY,2u,' '), "/", 1<<level);
			}
		}
		log(time);
	}
} mipmap;
#endif
