#include "file.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

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

#if 1
struct AnalyzeStatistics {
	//Plot residuals {"Residuals"};
	//Plot runs {"Runs"};
	//HBox layout {{&residuals/*, &runs*/}};
	//Window window {&layout};
	AnalyzeStatistics() {
		buffer<uint> histogram = cast<uint>(readFile("histogram"));
		uint valueCount=0; for(int x: range(histogram.size)) valueCount += histogram[x];
		uint max=0; for(int x: range(histogram.size)) max=::max(max, histogram[x]);
		//uint maxX = 0; for(int code: range(histogram.size)) if(count > max/2048) maxX = ::max(maxX, (uint)abs(sx));
		//auto& points = residuals.dataSets.insert("Residuals"__); //if(abs(sx)<=64) points.insert(sx, count);
		buffer<uint> runLengthHistogram = cast<uint>(readFile("runLengthHistogram"));
		size_t nonzero = runLengthHistogram[0];
		size_t zero = runLengthHistogram[1];
		size_t running = 0;
		for(int length: range(2, runLengthHistogram.size)) running += length*runLengthHistogram[length];
		assert_(nonzero+zero+running == valueCount, nonzero+zero+running, valueCount);
		log(nonzero/1024/1024, zero/1024/1024, running/1024/1024);
		/*for(uint minimumRunLength: range(1)) {
			for(uint rleCode: range(minimumRunLength ? 1 : 1)) {*/
		for(uint rleK: range(5, 8)) {
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

#if 0

struct BitWriter {
	uint64* pointer;
	uint64 word = 0;
	int bitsLeftCount = 64;
	~BitWriter() { if(bitsLeftCount<64) *pointer = word; }
	void operator()(int size, uint value) {
#if 1 // lsb
		word |= value << (64 - bitsLeftCount);
		if(bitsLeftCount <= size) { // Puts the bits falling off on the left to the right of the next word
			*pointer = word;
			pointer++;
			word = value >> bitsLeftCount; // LSB
			bitsLeftCount += 64;
		}
#else // msb
		if(size < bitsLeftCount) {
			word <<= size;
			word |= value;
		} else {
			word <<= bitsLeftCount;
			word |= value >> (size - bitsLeftCount); // Puts leftmost bits in remaining space
			*pointer = word; // 8byte LSB
			pointer++;
			bitsLeftCount += 64;
			word = value; // Already stored leftmost bits will be pushed out eventually
		}
#endif
		bitsLeftCount -= size;
	}
};

static uint8 log2i[256];
static void __attribute((constructor)) initialize_log2i() { int i=1; for(int l=0;l<=7;l++) for(int r=0;r<1<<l;r++) log2i[i++] = 7-l; assert(i==256); }
struct Compress {
	Compress() {
		//buffer<int16> source = ::source();
		Map map ("source"); ref<int16> source = cast<int16>(map);

		int predictor = 0;
		buffer<byte> compressed (source.size*2/8*8, 0);
		size_t size = 0;
		{BitWriter s {(uint64*)compressed.data}; // Assumes buffer is big enough
			constexpr uint k = 2;//, runLength = 4;
			for(int value: source) {
				int sx = value - predictor; predictor = value;
				int x  = -2 * sx - 1; x ^= x >> 31;
				x++; // 0 is RLE escape
				// Exp Golomb q + k residual
				uint q = x >> k;
				q++;
				uint b = 0;
				if(q & 0xFF00) { q >>= 8; b += 8; }
				b += log2i[q];
				size += b+b+1+k;
				//s(b+b+1+k, x); // unary b, binary q[b], binary r = 0^b.1.q[b]r[k] = x[b+b+1+k]
			}
			//compressed.size = (byte*)s.pointer + ((64-s.bitsLeftCount)+7)/8 - compressed.data;
			//assert_(compressed.size < compressed.capacity);
		}
		log(compressed.size, size/(8.*1024*1024), compressed.capacity*8/(8.*1024*1024));
	}
} compress;
#endif
