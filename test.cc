#include "file.h"
#include "window.h"
#include "plot.h"

struct Test {
	Plot plot {"Histogram", true};
	Window window {&plot};
	Test() {
		for(string name: {/*"valueHistogram"_,*/"residualHistogram"_}) {
			auto histogram = cast<uint>(readFile(name));
			auto& points = plot.dataSets.insert(copyRef(name));
			uint max=0; for(int x: range(histogram.size)) max=::max(max, histogram[x]);
			size_t total = 0, fixed = 0, variable = 0, unary = 0; //, huffman = 0, arithmetic = 0;
			for(int code: range(histogram.size)) {
				size_t count = histogram[code];
				total += count;
				fixed += 13*count;
				int value = code-4096;
				variable += count*(/*unary stop*/1 + (value ? /*sign*/1+/*length[unary]*/1+log2((uint)abs(value))+/*value[binary]*/log2((uint)abs(value))-1/*implicit msb 1*/ : 0));
				unary += count*(1+abs(value)+(value<0));
				if(count > max/2048) points.insert(value, count);
			}
			log(total*(2./1024/1024), fixed/(8.*1024*1024), variable/(8.*1024*1024), unary/(8.*1024*1024));
		}
	}
} test;
