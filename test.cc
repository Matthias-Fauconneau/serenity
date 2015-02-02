#include "file.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "time.h"

#if 0
#include "zip.h"
buffer<int16> source() {
	static constexpr size_t W=4, H=4;
	Image16 image(int2(W*10800, 6000+(H-2)*4800+6000)); // 890 Ms
	int y0 = 0;
	for(size_t Y: range(H)) {
		const int h = (Y==0 || Y==H-1) ? 4800 : 6000;
		for(size_t X: range(W)) {
			buffer<int16> raw = cast<int16>(extractZIPFile(Map("globe.zip"), "all10/"_+char('a'+Y*W+X)+"10g"_));
			assert_(raw.size%10800==0);
			Image16 source (raw, int2(10800, raw.size/10800)); // WARNING: unsafe weak reference to file
			assert_(source.size.y == h);
			int16* target = &image(X*10800, y0);
			for(size_t y: range(source.size.y)) for(size_t x: range(source.size.x)) target[y*image.size.x+x] = source[y*source.size.x+x];
			log(X, Y, source.size);
		}
		y0 += h;
	}
	return move(image);
}
struct CacheSource { CacheSource() { if(!existsFile("source")) writeFile("source", cast<byte>(source())); } } cache;
#endif

#if 0
struct GenerateStatistics {
	GenerateStatistics() {
		//buffer<int16> source = ::source();
		Map map ("source"); ref<int16> source = cast<int16>(map);
		uint maxRunLength = 0;
		{
			int predictor = 0;
			uint runLength = 0;
			for(int value: source) {
				int residual = value - predictor; predictor = value;
				if(residual == 0) runLength++;
				else {
					maxRunLength = max(maxRunLength, runLength);
					runLength=0;
				}
			}
			maxRunLength = max(maxRunLength, runLength);
		};
		int predictor = 0;
		uint histogram[8192] = {};
		buffer<uint> runLengthHistogram(maxRunLength+1);
		runLengthHistogram.clear(0);
		uint runLength = 0;
		for(int value: source) {
			int residual = value - predictor; predictor = value;
			assert_(residual >= -2862 && residual <= 3232);
			histogram[4096+residual]++;
			if(residual == 0) runLength++;
			else {
				assert_(runLength<=maxRunLength, runLength);
				if(runLength) { runLengthHistogram[runLength]++; runLength=0; }
				runLengthHistogram[0]++;
			}
		}
		runLengthHistogram[runLength]++;
		writeFile("histogram", cast<byte>(ref<uint>(histogram)), currentWorkingDirectory(), true);
		writeFile("runLengthHistogram", cast<byte>(runLengthHistogram), currentWorkingDirectory(), true);
	}
} generate;
#endif

#if 0
struct AnalyzeStatistics {
	AnalyzeStatistics() {
		buffer<uint> histogram = cast<uint>(readFile("histogram"));
		uint valueCount=0; for(int x: range(histogram.size)) valueCount += histogram[x];
		uint max=0; for(int x: range(histogram.size)) max=::max(max, histogram[x]);
		buffer<uint> runLengthHistogram = cast<uint>(readFile("runLengthHistogram"));
		size_t nonzero = runLengthHistogram[0];
		size_t zero = runLengthHistogram[1];
		size_t running = 0;
		for(int length: range(2, runLengthHistogram.size)) running += length*runLengthHistogram[length];
		assert_(nonzero+zero+running == valueCount, nonzero+zero+running, valueCount);
		log(nonzero/1024/1024, zero/1024/1024, running/1024/1024);
		for(uint rleK: range(0, 8)) {
				static constexpr uint K = 4;
				int64 fixed = 0, GR[K] = {}, EG[K] = {}; //, huffman = 0, arithmetic = 0;
				auto write = [&](uint x, int64 count) {
					assert_(fixed > -13*count, x, count, fixed);
					fixed += 13*count;
					for(uint k: range(1,K)) {
						uint q = x >> k;//, r = x&(k-1);
						{ // GR
							int length = q+1+k;
							GR[k] += count*length;}
						{ // EG
							int N = log2(q+1);
							int length = N+1+N+k;
							EG[k] += count*length;}
					}
				};
				for(int code: range(histogram.size)) {
					size_t count = histogram[code];
					if(count) {
						int sx = code-4096;
						uint x = sx > 0 ? 2*sx-1 : 2 * -sx;
						//if(minimumRunLength) if(x>=rleCode) x++;
						if(rleK) x++;
						assert_(x >= 0);
						write(x, count);
					}
				}
				/*if(minimumRunLength) {
					log("Minimum RLE length", minimumRunLength, "RLE code", rleCode);
					buffer<uint> runLengthHistogram = cast<uint>(readFile("runLengthHistogram"));
					for(uint length_1: range(runLengthHistogram.size)) {
						int count = runLengthHistogram[length_1];
						int length = length_1 + 1;
						if(length > minimumRunLength) {
							write(rleCode, count);
							write(length-minimumRunLength, count); // FIXME: length context => not necessarily same encoding as residual
							write(0, -length); // Deduces length zeroes
						}
					}
				}*/
				log("RLE k", rleK);
				if(rleK) {
					buffer<uint> runLengthHistogram = cast<uint>(readFile("runLengthHistogram"));
					for(int length: range(2, runLengthHistogram.size)) {
						int count = runLengthHistogram[length];
						if(count) {
							int rleCodeRepeats = (length-2) >> rleK; // RLE length = 2 + rleCodeRepeats · 2^k
							int rleLength = rleCodeRepeats << rleK;
							write(0, rleCodeRepeats*count); // RLE code overhead
							write(1+0, -(2+rleLength)*count); // Deduces RLE savings
						}
					}
				}
				log(fixed/(8.*1024*1024));
				for(uint k: range(1,K)) log(k, GR[k]/(8.*1024*1024), EG[k]/(8.*1024*1024));
			//}
		}
		/*{
			buffer<uint> histogram = cast<uint>(readFile("runLengthHistogram"));
			auto& points = runs.dataSets.insert("Runs"__);
			for(int length: range(1, min(65ul, histogram.size))) points.insert(1+length, histogram[length]);
		}*/
	}
} analyze;
#endif

#if 1

struct BitWriter {
	uint8* pointer;
	uint64 word = 0;
	//uint64 bitsLeftCount = 64;
	uint64 bitIndex = 0;
	BitWriter(mref<byte> buffer) : pointer((uint8*)buffer.data) {}
	void write(uint size, uint64 value) {
#if 0
		if(size < bitsLeftCount) {
			word <<= size;
			word |= value;
		} else {
			word <<= bitsLeftCount;
			word |= value >> (size - bitsLeftCount); // Puts leftmost bits in remaining space
			*pointer = big64(word); // MSB
			pointer+=8;
			bitsLeftCount += 64;
			word = value; // Already stored leftmost bits will be pushed out eventually
		}
		bitsLeftCount -= size;
#else
		bitIndex += size;
		word |= value << (64-bitIndex);
		/*if(bitIndex > 64)*/ flush();
	}
	void flush() {
		*(uint64*)pointer = big64(word);
		word <<= (bitIndex&~7);
		pointer += (bitIndex>>3);
		bitIndex &= 7;
#endif
	}
	void end() {
#if 0
		if(bitsLeftCount<64) *pointer = big64(word);
#else
		flush();
		if(bitIndex>0) pointer++;
#endif
	}
};

struct BitReader {
	uint8* pointer;
	uint64 word;
	int64 bitsLeftCount = 64;
	BitReader(ref<byte> data) : pointer((uint8*)data.data) { word=big64(*(uint64*)pointer); pointer+=8; }
	uint read(uint size) {
		uint x = word >> (64-size);
		word <<= size;
		bitsLeftCount -= size;
		if(bitsLeftCount <= 56) {
			uint64 next8 = big64(*(uint64*)pointer);
			int64 bytesToGet = (64-bitsLeftCount)>>3;
			pointer += bytesToGet;
			word |= next8 >> bitsLeftCount;
			bitsLeftCount += bytesToGet<<3;
		}
		return x;
	}
};

struct Encode {
	Encode() {
		//buffer<int16> source = ::source();
		Map map ("source"); ref<int16> source = cast<int16>(map);

		buffer<byte> encoded (source.size*2/8*8, 0);
		constexpr uint r = 2;//, runLength = 4;
		Time encode;
		{BitWriter bitIO (encoded); // Assumes buffer is big enough
			int predictor = 0;
			for(int value: source) {
				int s = value - predictor; predictor = value;
				uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
				uint x = u+1; // 0 is RLE escape
				// Exp Golomb q + r residual
				x += (1<<r);
				uint b = 63 - __builtin_clzl(x); // BSR
				bitIO.write(b+b+1-r, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
			}
			encoded.size = (byte*)bitIO.pointer - encoded.data;
			assert_(encoded.size <= encoded.capacity);
			//~BitWriter
		}
		log(int(round(encoded.size/(1024.*1024))),"MB", encode, int(round((encoded.size/(1024.*1024))/encode.toReal())),"MB/s"); // 115 MB/s

		Time decode;
		{BitReader bitIO (encoded);
			int predictor = 0;
			for(int expectedValue: source) {
				uint b = __builtin_clzl(bitIO.word) * 2 + 1 + r;
				uint x = bitIO.read(b) - (1<<r);
				if(x == 0) error("RLE", bitIO.pointer-(uint8*)encoded.data, str(bitIO.word, 64u, '0', 2u), bitIO.bitsLeftCount, b, x, predictor, expectedValue);
				uint u = x-1;
				int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
				int value = predictor + s; predictor = value;
				assert_(value == expectedValue, value, expectedValue, bitIO.pointer-(uint8*)encoded.data, str(bitIO.word, 64u, '0', 2u), bitIO.bitsLeftCount, b, x, u, s, predictor, value, expectedValue);
			}
		}
		log(encoded.size/(1024.*1024),"MB", decode, (encoded.size/(1024.*1024))/decode.toReal(),"MB/s"); // 73 MB/s
	}
} encode;
#endif
