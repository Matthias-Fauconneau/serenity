#include "file.h"
#include "zip.h"
#include "tiff.h"
#include "flic.h"
#include "time.h"

struct Encoder {
	buffer<byte> buffer;
	BitWriter bitIO {buffer}; // Assumes buffer capacity will be large enough
	int predictor = 0;
	Time encode;
	Encoder() {}
	Encoder(int2 size) : buffer((size_t)size.y*size.x*2) { // Assumes average bitrate < 16bit / sample (large RLE voids)
		bitIO.write(32, size.x); bitIO.write(32, size.y);
	}
	::buffer<byte> end() {
		bitIO.flush();
		buffer.size = (byte*)bitIO.pointer - buffer.data;
		assert_(buffer.size <= buffer.capacity);
		log(int(round(buffer.size/(1024.*1024))),"MB", encode, int(round((buffer.size/(1024.*1024))/encode.toReal())),"MB/s"); // 200 MB/s
		return move(buffer);
	}
	void write(ref<int16> source) {
		for(size_t index=0; index<source.size;) {
			int value, s;
			uint runLength = 0;
			do {
				value = source[index];
				index++;
				s = value - predictor;
				if(s != 0) break;
				runLength++;
			} while(index<source.size);
			if(runLength) {
				uint rleCodeRepeats = runLength >> RLE; // RLE length = rleCodeRepeats · 2^RLE
				for(;rleCodeRepeats > 0; rleCodeRepeats--) bitIO.write(3, 0b100);
				uint remainingZeroes = runLength & ((1<<RLE)-1);
				for(;remainingZeroes > 0; remainingZeroes--) bitIO.write(3, 0b101); // Explicitly encode remaining zeroes
			}
			predictor = value;
			uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
			uint x = u+1; // 0 is RLE escape
			// Exp Golomb _ r
			x += (1<<EG);
			uint b = 63 - __builtin_clzl(x); // BSR
			bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
		}
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
		Map map(name+".eg2rle6"_);
		FLIC source(map);
		int n = 0; while(source.size.y%(1<<n)==0) n++; n--; // Maximum mipmap level count
		int N = 1<<n, tileSize = source.size.y/N;
		log(source.size, n, N, tileSize);
		assert_(N*tileSize == source.size.y && 2*N*tileSize == source.size.x);
		for(size_t tY: range(N)) {
			buffer<Encoder> encoder (2*N); encoder.clear(tileSize);
			buffer<int16> buffer (tileSize);
			for(size_t unused y: range(tileSize)) {
				for(size_t tX: range(encoder.size)) { // Splits each line into tiles
					source.read(buffer);
					encoder[tX].write(buffer);
				}
			}
			for(size_t tX: range(encoder.size)) writeFile(str(tX)+","+str(tY), encoder[tX].end(),
														  Folder(name+"."_+str(N)+".eg2rle6"_, currentWorkingDirectory(), true), true);
		}
	}
} tile;
#endif
