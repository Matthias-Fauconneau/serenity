#include "file.h"
#include "zip.h"
#include "tiff.h"
#include "flic.h"
#include "time.h"

// Converts zipped tif tiles to Exp Golomb RLE mipmaps
struct Convert {
	Convert() {
		static constexpr size_t W=2*3, H=4; // 2x3 tiles per band, 4 bands
		for(size_t X0: range(1,2)) { // Level 0 is 2x1
			Time encode (false);
			buffer<byte> encoded;
			BitWriter bitIO;
			int predictor = 0;
			for(size_t Y: range(H)) {
				Image16 band; // (W, 1)*tileSize
				for(size_t X: range(W/2)) {
					buffer<byte> tif = extractZIPFile(Map("dem15/15-"_+char('A'+Y*W+X0*W/2+X)+".zip"), "15-"_+char('A'+Y*W+X0*W/2+X)+".tif"_);
					Image16 source = parseTIF(tif);
					if(band) assert_(band.size==int2(W/2,1)*(source.size-int2(1))); // Uniform tile size
					else {
						band = Image16(int2(W/2,1)*(source.size-int2(1))); log(Y, band.size);
						if(!encoded) {
							encoded = ::buffer<byte>(H*band.Ref::size*2/4); // Assumes average bitrate < 4bit / sample (large RLE voids)
							bitIO = BitWriter(encoded);// buffer capacity will be large enough
							bitIO.write(32, band.size.x); // Width
							bitIO.write(32, H*band.size.y); // Height
							encode.start(band.size.x, H*band.size.y);
							log();
						}
					}
					int16* target = band.begin() + X*(source.size.x-1);
					for(size_t y: range(band.size.y)) for(size_t x: range(source.size.x-1)) target[y*band.size.x+x] = source[y*source.size.x+x];
					log(X, Y, source.size-int2(1));
				}
				ref<int16> source = band;
				for(size_t index=0; index<source.size;) {
					int value, s;
					uint runLength = 0;
					while(index<source.size) {
						value = source[index++];
						s = value - predictor;
						if(s != 0) break;
						runLength++;
					}
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
			bitIO.end();
			encoded.size = (byte*)bitIO.pointer - encoded.data;
			assert_(encoded.size <= encoded.capacity);
			log(int(round(encoded.size/(1024.*1024))),"MB", encode, int(round((encoded.size/(1024.*1024))/encode.toReal())),"MB/s"); // 200 MB/s
			writeFile("dem15."+str(X0)+".eg2rle6", encoded, currentWorkingDirectory(), true);
		}
	}
} convert;

