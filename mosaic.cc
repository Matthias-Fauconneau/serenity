#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"
#include "time.h"
#include "algebra.h"

typedef float float4 __attribute((__vector_size__ (16)));
inline float4 constexpr float4_1(float f) { return (float4){f,f,f,f}; }
inline float4 mean(const ref<float4> x) { assert_(x.size); return sum(x, float4_1(0)) / float4_1(x.size); }
template<> inline String str(const float4& v) { return "("+str(v[0], v[1], v[2], v[3])+")"; }

/// 2D array of floating-point 4 component vector pixels
struct ImageF : buffer<float4> {
	ImageF(){}
	ImageF(buffer<float4>&& data, int2 size, size_t stride) : buffer(::move(data)), size(size), stride(stride) {
		assert_(buffer::size==size_t(size.y*stride), buffer::size, size, stride);
	}
	ImageF(int width, int height) : buffer(height*width), width(width), height(height), stride(width) {
		assert_(size>int2(0), size, width, height);
	}
	ImageF(int2 size) : ImageF(size.x, size.y) {}

	inline float4& operator()(size_t x, size_t y) const {assert(x<width && y<height, x, y); return at(y*stride+x); }

	union {
		int2 size = 0;
		struct { uint width, height; };
	};
	size_t stride = 0;
};
/*inline float4 mean(const ImageF& image, int x0, int y0, int x1, int y1) {
	float4 sum = float4_1(0);
	for(int y: range(y0, y1)) for(int x: range(x0, x1)) sum += image(x, y);
	return sum / float4_1((y1-y0)*(x1-x0));
}*/

// Box convolution with constant border
void box(const ImageF& target, const ImageF& source, const int width, const float4 border) {
	assert_(target.size.y == source.size.x && target.size.x == source.size.y && uint(target.stride) == target.width && uint(source.stride)==source.width);
	parallel_chunk(source.size.y, [&](uint, int Y0, int DY) { // Top
		const float4* const sourceData = source.data;
		float4* const targetData = target.begin();
		const uint sourceStride = source.stride;
		const uint targetStride = target.stride;
		const float4 scale = float4_1(1./(width+1+width));
		const float4 sum0 = float4_1(width)*border;
		for(int y: range(Y0, Y0+DY)) {
			const float4* const sourceRow = sourceData + y * sourceStride;
			float4* const targetColumn = targetData + y;
			float4 sum = sum0;
			for(uint x: range(width)) sum += sourceRow[x];
			for(uint x: range(width)) {
				sum += sourceRow[x+width];
				targetColumn[x * targetStride] = scale * sum;
				sum -= border;
			}
			for(uint x: range(width, sourceStride-width)) {
				float4 const* source = sourceRow + x;
				sum += source[width];
				targetColumn[x * targetStride] = scale * sum;
				sum -= source[-width];
			}
			for(uint x: range(sourceStride-width, sourceStride)) {
				sum += border;
				targetColumn[x * targetStride] = scale * sum;
				sum -= sourceRow[x-width];
			}
		}
	}, 1);
}

struct Mosaic {
	// Name
	string name = arguments() ? arguments()[0] : "mosaic";
	Graphics page;
	const float inchMM = 25.4;
	const vec2 pageSizeMM = vec2(500, 400); // 4:3
	//const vec2 pageSizeMM = vec2(400, 300); // 4:3
	//const vec2 pageSizeMM = vec2(1140,760); // 3:2
	Mosaic(float inchPx = 0) {
		const float mmPx = inchPx ? inchPx/inchMM : min(1680/pageSizeMM.x, (1050-32-24)/pageSizeMM.y);
		const vec2 pageSize = pageSizeMM * mmPx;

		Time total, load, blur; total.start();

		array<String> files = Folder(".").list(Files);
		files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});

		// - Layout definition
		array<string> images; // Images
		array<array<int>> rows; // Index into elements (-1: row extension, -2 column extension, -3: inner extension)
		if(existsFile(name)) { // -- Parses page definitions
			for(TextData s= readFile(name); s;) {
				array<int> row;
				for(string name; (name = s.whileNo(" \t\n"));) {
					/***/ if(name=="-") row.append(-1); //{ assert_(row); row.append(row.last()); }
					else if(name=="|") row.append(-2); //{ assert_(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
					else if(name=="\\") row.append(-3); //{ assert_(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
					else {
						row.append(images.size);
						images.append([&files](string name) {
							for(string file: files) if(startsWith(file, name)) return file;
							error("No such image"_, name, "in", files);
						}(name));
					}
					s.whileAny(" \t"_);
				}
				assert_(row);
				rows.append(move(row));
				if(!s) break;
				s.skip('\n');
			}
		} else { // Layouts all images
			error("No such file", name, "in", currentWorkingDirectory().name());
			int X = round(sqrt(float(files.size))), Y = X;
			for(int y: range(Y)) {
				array<int> row;
				for(int x: range(X)) {
					if(y*X+x >= int(files.size)) break;
					images.append(files[y*X+x]);
					row.append(y*X+x);
				}
				assert_(row);
				rows.append(move(row));
			}
		}
		assert_(rows);
		const size_t m = rows.size; // Row count
		const size_t n = rows[0].size; // Column count
		for(ref<int> row: rows) assert_(row.size = n); // Asserts correct table definition

		const float W = pageSize.x, H = pageSize.y; // Container size
		buffer<int2> imageSize = apply(images, [=](string image){ return ::imageSize(Map(image)); }); // Image sizes
		Vector aspectRatio = apply(imageSize, [=](int2 size){ return (float)size.x/size.y; }); // Image aspect ratios

		vec2 inner0 = vec2(8*mmPx), outer0 = vec2((8+5)*mmPx);

		const size_t k = images.size + 2 + 2; // Unknowns (heights + margins)
		bool sameWidth = true, sameHeight = false;
		size_t equationCount = m+n+(sameWidth?n*(m-1):0)+(sameHeight?m*(n-1):0) + 4; // m rows, n columns, m row of n-1 elements with same height constraint, similar border sizes
		assert_(equationCount >= k);
		Matrix A (equationCount, k); Vector b(equationCount); // Linear system
		size_t equationCounter = 0;
		for(const size_t rowIndex : range(m)) { // Row equations
			const size_t equationIndex = equationCounter+rowIndex;
			size_t imageCount = 0;
			for(const size_t columnIndex: range(n)) {
				int imageIndex = rows[rowIndex][columnIndex];
				if(imageIndex >=0) { // Normal image root origin instance
					assert_(A(equationIndex, imageIndex)==0);
					A(equationIndex, imageIndex) = aspectRatio[imageIndex];
					imageCount++;
				}
				else if(imageIndex == -2) { // Column extension across rows
					size_t sourceRowIndex = rowIndex-1;
					while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
					int imageIndex = rows[sourceRowIndex][columnIndex];
					assert_(A(equationIndex, imageIndex)==0);
					A(equationIndex, imageIndex) = aspectRatio[imageIndex];
					imageCount++;
				}
				else {
					assert_(imageIndex == -1 || imageIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, images.size) = 2; // Vertical outer margin width
			A(equationIndex, images.size+2) = imageCount-1; // Vertical inner margin width
			b[equationIndex] = W - 2*outer0.x - (imageCount-1)*inner0.x;
		}
		equationCounter += m;
		for(const size_t columnIndex : range(n)) { // Column equations
			size_t equationIndex = equationCounter+columnIndex;
			size_t imageCount = 0;
			for(const size_t rowIndex: range(m)) {
				int imageIndex = rows[rowIndex][columnIndex];
				if(imageIndex >=0) { // Normal image root origin instance
					assert_(A(equationIndex, imageIndex)==0);
					A(equationIndex, imageIndex) = 1;
					imageCount++;
				}
				else if(imageIndex == -1) { // Row extension across columns
					size_t sourceColumnIndex = columnIndex-1;
					while(rows[rowIndex][sourceColumnIndex] == -1) sourceColumnIndex--;
					int imageIndex = rows[rowIndex][sourceColumnIndex];
					assert_(A(equationIndex, imageIndex)==0);
					A(equationIndex, imageIndex) = 1;
					imageCount++;
				}
				else {
					assert_(imageIndex == -2 || imageIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, images.size+1) = 2; // Horizontal outer margin height
			A(equationIndex, images.size+3) = imageCount-1; // Horizontal inner margin height
			b[equationIndex] = H - 2*outer0.y - (imageCount-1)*inner0.y;
		}
		equationCounter += n;
		if(sameWidth) {
			for(const size_t columnIndex : range(n)) { // Column same width constraint
				const size_t equationIndexBase = m+n+columnIndex*(m-1);
				for(const size_t columnIndex: range(m-1)) b[equationIndexBase+columnIndex] = 0;

				size_t firstRowIndex = invalid;
				for(const size_t rowIndex: range(m-1)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex >= 0 && (columnIndex+1==n || rows[rowIndex][columnIndex+1]!=-1)) { firstRowIndex = rowIndex; break; }
				}
				if(firstRowIndex == invalid) continue; // n-1 "0 = 0" equations

				for(const size_t rowIndex: range(firstRowIndex+1, m)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex >=0 && (columnIndex==n-1 || rows[rowIndex][columnIndex+1]!=-1)) { // Single row image
						const size_t equationIndex = equationIndexBase+rowIndex-1;
						assert_(A(equationIndex, imageIndex)==0);
						//  - h[firstColumnIndex] + h[columnIndex] = 0
						A(equationIndex, rows[firstRowIndex][columnIndex]) = -aspectRatio[rows[firstRowIndex][columnIndex]];
						A(equationIndex, imageIndex) = aspectRatio[imageIndex];
					}
					// else 0=0
				}
			}
			equationCounter += n*(m-1);
		}
		if(sameHeight) {
			for(const size_t rowIndex : range(m)) { // Row same height constraint
				const size_t equationIndexBase = equationCounter+rowIndex*(n-1);
				for(const size_t columnIndex: range(n-1)) b[equationIndexBase+columnIndex] = 0;

				size_t firstColumnIndex = invalid;
				for(const size_t columnIndex: range(n-1)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex >= 0 && (rowIndex+1==m || rows[rowIndex+1][columnIndex]!=-2)) { firstColumnIndex = columnIndex; break; }
				}
				if(firstColumnIndex == invalid) continue; // n-1 "0 = 0" equations

				for(const size_t columnIndex: range(firstColumnIndex+1, n)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex >=0 && (rowIndex==m-1 || rows[rowIndex+1][columnIndex]!=-2)) { // Single row image
						const size_t equationIndex = equationIndexBase+columnIndex-1;
						assert_(A(equationIndex, imageIndex)==0);
						//  - h[firstColumnIndex] + h[columnIndex] = 0
						A(equationIndex, rows[rowIndex][firstColumnIndex]) = -1;
						A(equationIndex, imageIndex) = 1;
					}
					// else 0=0
				}
			}
			equationCounter += m*(n-1);
		}
		{
			const size_t marginIndexBase = images.size;
			A(equationCounter+0, marginIndexBase+0) = -1; A(equationCounter+0, marginIndexBase+1) = 1; // outer.x = outer.y
			A(equationCounter+1, marginIndexBase+0) = -1; A(equationCounter+1, marginIndexBase+2) = 1; // outer.x = inner.x
			A(equationCounter+2, marginIndexBase+2) = -1; A(equationCounter+2, marginIndexBase+3) = 1; // inner.x = inner.y
			A(equationCounter+3, marginIndexBase+1) = -1; A(equationCounter+3, marginIndexBase+3) = 1; // outer.y = inner.y
			equationCounter += 4;
		}
		//log(str(b)+"\n"+str(A));
		// Least square system
		// TODO: Weighted least square
		Matrix At = transpose(A);
		Matrix AtA = At * A;
		{// Regularize border parameters
			const float a = 1;
			for(size_t i: range(images.size,  images.size+4)) AtA(i,i) = AtA(i,i) + a*a;
		}
		Vector Atb = At * b;
		Vector x = solve(move(AtA),  Atb);
		Vector heights = copyRef(x.slice(0, images.size));
		Vector widths = apply(heights.size, [&](size_t i){ return aspectRatio[i] * heights[i]; });
		vec2 outer = outer0 + vec2(x[images.size], x[images.size+1]);
		vec2 inner = inner0 + vec2(x[images.size+2], x[images.size+3]);
		//log(heights, outer, outer0, vec2(x[images.size], x[images.size+1]), inner, inner0, vec2(x[images.size+2], x[images.size+3]));
		if(1) {
			array<char> s;
			for(size_t rowIndex : range(m)) {
				float sum = outer.x;
				for(size_t columnIndex : range(n)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex == -1) s.append("-");
					if(imageIndex == -3) s.append("\\");
					if(imageIndex == -2) {
						int sourceRowIndex = rowIndex-1;
						while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						imageIndex = rows[sourceRowIndex][columnIndex];
					}
					if(imageIndex >= 0) {
						float w = widths[imageIndex], h = heights[imageIndex];
						sum += w;
						if(columnIndex+1<n && rows[rowIndex][columnIndex+1]>=0) sum += inner.x;
						s.append(str(w, h));
					}
					s.append("\t");
				}
				sum += outer.x;
				s.append(str(sum)+"\n");
			}
			float heightSums = 0;
			for(size_t columnIndex : range(n)) {
				float sum = outer.y;
				for(size_t rowIndex : range(m)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex == -1) {
						int sourceColumnIndex = columnIndex-1;
						while(rows[rowIndex][sourceColumnIndex] == -2) sourceColumnIndex--;
						imageIndex = rows[rowIndex][sourceColumnIndex];
					}
					if(imageIndex >= 0) {
						float h = heights[imageIndex];
						sum += h;
						if(rowIndex+1<m && rows[rowIndex+1][columnIndex]>=0) sum += inner.y;
					}
				}
				sum += outer.y;
				heightSums += sum;
				s.append(str(sum)+"\t\t");
			}
			float dy = H - heightSums / n;
			if(0) log(s,"\tH", H, "\tW", W, "dy", dy);
			outer.y += dy; // Enforces vertical center
		}
		if(1) {
			float minScale = inf, maxScale = 0;
			for(size_t imageIndex: range(images.size)) {
				float scale = imageSize[imageIndex].x / widths[imageIndex];
				minScale = min(minScale, scale);
				maxScale = max(maxScale, scale);
			}
			log((inchPx?:300), "min", minScale, minScale*(inchPx?:300), "max", maxScale, maxScale*(inchPx?:300));
		}
		if(0) { // -- White background
			page.bounds = Rect(pageSize);
			for(const size_t rowIndex: range(m)) {
				float x = outer.x;
				for(const size_t columnIndex: range(n)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex == -2) { // Column extension
						int sourceRowIndex = rowIndex-1;
						while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						int imageIndex = rows[sourceRowIndex][columnIndex];
						float w = widths[imageIndex];
						x += w + inner.x;
					}
					if(imageIndex < 0) continue;
					float y = outer.y;
					for(const size_t i: range(rowIndex)) {
						int imageIndex = rows[i][columnIndex];
						if(imageIndex == -1) { // Row extension
							int sourceColumnIndex = columnIndex-1;
							while(rows[i][sourceColumnIndex] == -2) sourceColumnIndex--;
							imageIndex = rows[i][sourceColumnIndex];
						}
						if(imageIndex >= 0)
							y += heights[imageIndex] + inner.y;
					}
					float w = widths[imageIndex], h = heights[imageIndex];
					assert_(w > 0 && h > 0, w, h);
					load.start();
					Image image = decodeImage(Map(images[imageIndex]));
					image.alpha = false;
					load.stop();
					page.blits.append(vec2(x,y), vec2(w, h), move(image));
					x += w + inner.x;
				}
			}
		} else { // -- Blur background
		   page.bounds = Rect(round(pageSize));
		   ImageF target(int2(page.bounds.size()));
		   assert_(ptr(target.data)%sizeof(float4)==0);
		   /*if(target.Ref::size < 8*1024*1024)*/ target.clear(float4_1(0)); // Assumes larger allocation are clear pages
		   array<Rect> mask; // To copy unblurred data on blur background
		   array<float4> innerBackgroundColors;
		   bool constantMargin = false;
		   { // -- Copies source images to target mosaic
			   for(const size_t rowIndex: range(m)) {
				   float x0 = outer.x;
				   for(const size_t columnIndex: range(n)) {
					   int imageIndex = rows[rowIndex][columnIndex];
					   if(imageIndex == -2) { // Column extension
						   int sourceRowIndex = rowIndex-1;
						   while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						   int imageIndex = rows[sourceRowIndex][columnIndex];
						   float w = widths[imageIndex];
						   x0 += w + inner.x;
					   }
					   if(imageIndex < 0) continue;
					   float y0 = outer.y;
					   for(const size_t i: range(rowIndex)) {
						   int imageIndex = rows[i][columnIndex];
						   if(imageIndex == -1) { // Row extension
							   int sourceColumnIndex = columnIndex-1;
							   while(rows[i][sourceColumnIndex] == -2) sourceColumnIndex--;
							   imageIndex = rows[i][sourceColumnIndex];
						   }
						   if(imageIndex >= 0)
							   y0 += heights[imageIndex] + inner.y;
					   }
					   float w = widths[imageIndex], h = heights[imageIndex];
					   assert_(w > 0 && h > 0, w, h);

					   int ix0 = round(x0), iy0 = round(y0);
					   float leftMargin = (constantMargin || columnIndex) ? inner.x : outer.x;
					   float aboveMargin = (constantMargin || rowIndex) ? inner.y : outer.y;
					   float rightMargin = (constantMargin || (columnIndex+1<n && rows[rowIndex][columnIndex+1]>=0)) ? inner.x : outer.x;
					   float belowMargin = (constantMargin || (rowIndex+1<m && rows[rowIndex+1][columnIndex]>=0)) ? inner.y : outer.y;
					   int iw0 = ix0-round(x0-leftMargin), ih0 = iy0-round(y0-aboveMargin); // Previous border sizes
					   float x1 = x0+w, y1 = y0+h;
					   int ix1 = round(x1), iy1 = round(y1);
					   int iw1 = round(x1+rightMargin) - ix1, ih1 = min(target.size.y, int(round(y1+belowMargin)))/*round(y1+belowMargin)*/ - iy1; // Next border sizes
					   assert_(iw0 >= 1 && ih0 >= 1 && iw1 >= 1 && ih1 >= 1, iw0, ih0, iw1, ih1);
					   mask.append(vec2(ix0, iy0), vec2(ix1, iy1));
					   int2 size(ix1-ix0, iy1-iy0);
					   assert_(ix1+iw1 < target.size.x, ix1, iw1, target.size.x, inner, outer);
					   assert_(iy0 >= ih0 && iy0+size.y < target.size.y);
					   assert_(iy1+ih1 <= target.size.y, iy1, ih1, iy1+ih1, target.size.y, inner, outer);

					   load.start();
					   Image image = decodeImage(Map(images[imageIndex]));
					   image.alpha = false;
					   load.stop();
					   Image iSource = resize(size, image); // TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
					   ImageF source (size);
					   parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
						   extern float sRGB_reverse[0x100];
						   for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]], 1};
					   });
					   // -- Margins
					   if(0) { // -- Extends images over margins with a mirror transition
						   parallel_chunk(ih0, [&](uint, int Y0, int DY) { // Top
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy0-y-1) += float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, y); // Left
								   for(int x: range(size.x)) target(ix0+x, iy0-y-1) += float4_1(1-y/float(ih0-1)) * source(x, y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy0-y-1) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, y); // Right
							   }
						   });
						   parallel_chunk(size.y, [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy0+y) += float4_1(1-x/float(iw0-1)) * source(x, y); // Left
								   for(int x: range(size.x)) target(ix0+x, iy0+y) = source(x, y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy0+y) += float4_1(1-x/float(iw1-1)) * source(size.x-1-x, y); // Right
							   }
						   });
						   parallel_chunk(ih1, [&](uint, int Y0, int DY) { // Bottom
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy1+y) +=  float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, size.y-1-y); // Left
								   for(int x: range(size.x)) target(ix0+x, iy1+y) += float4_1(1-y/float(ih1-1)) * source(x, size.y-1-y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy1+y) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, size.y-1-y); // Right
							   }
						   });
					   } else if(1) { // -- Extends images over margins with a constant transition
						   //float4 innerBackgroundColor = ::mean(source, 0,0, source.size.x,source.size.y);
						   float4 innerBackgroundColor;
						   if(0) {
							   int histogram[16*16*16] = {}; // 16K
							   for(byte4 c: iSource) histogram[c.b/16*256+c.g/16*16+c.r/16]++; // TODO: most common hue (weighted by chroma (Max-min))
							   int mostCommonIndex = argmax(ref<int>(histogram));
							   byte4 mostCommonColor = byte4(mostCommonIndex/256*16, ((mostCommonIndex/16)%16)*16, (mostCommonIndex%16)*16);
							   extern float sRGB_reverse[0x100];
							   innerBackgroundColor = {sRGB_reverse[mostCommonColor[0]], sRGB_reverse[mostCommonColor[1]], sRGB_reverse[mostCommonColor[2]], 1};
						   } else {
							   int hueHistogram[0x100] = {}; mref<int>(hueHistogram).clear(0); // 1½K: 32bit / 0xFF -> 4K² images
							   int intensityHistogram[0x100] = {}; mref<int>(intensityHistogram).clear(0);
							   assert_(iSource.Ref::size < 1<<23);
							   for(byte4 c: iSource) {
								   const int B = c.b, G = c.g, R = c.r;
								   const int M = max(max(B, G), R);
								   const int m = min(min(B, G), R);
								   const int C = M - m;
								   assert_(C >=0 && C < 0x100);
								   const int I = (B+G+R)/3;
								   assert_(I >= 0 && I < 0x100, I);
								   intensityHistogram[I]++;
								   if(C) {
									   int H;
									   if(M==R) H = ((G-B)*43/C+0x100)%0x100; // 5-6 0-1
									   else if(M==G) H = (B-R)*43/C+85; // 1-3
									   else if(M==B) H = (R-G)*43/C+171; // 3-5
									   else error(B, G, R);
									   assert_(H >=0 && H < 0x100);
									   assert_(hueHistogram[H] >= 0 && hueHistogram[H] < 0x40000000, H, hueHistogram[H]);
									   hueHistogram[H] += C;
								   }
							   }
							   int H = argmax(ref<int>(hueHistogram));
							   int C = 0x40;
							   int X = C * (0xFF - abs((H%85)*6 - 0xFF)) / 0xFF;
							   assert_(X >= 0 && X < 0x100);
							   int I = argmax(ref<int>(intensityHistogram));
							   if(0) I = min(0x80, I);
							   int R,G,B;
							   if(H < 43) R=C, G=X, B=0;
							   else if(H < 85) R=X, G=C, B=0;
							   else if(H < 128) R=0, G=C, B=X;
							   else if(H < 171) R=0, G=X, B=C;
							   else if(H < 213) R=X, G=0, B=C;
							   else if(H < 256) R=C, G=0, B=X;
							   else error(H);
							   int m = I - (R+G+B)/3;
							   // Limits intensity within sRGB
							   m = min(m, 0xFF-R);
							   m = min(m, 0xFF-G);
							   m = min(m, 0xFF-B);
							   extern float sRGB_reverse[0x100];
							   assert_(m+B >= 0 && m+B < 0x100);
							   assert_(m+G >= 0 && m+G < 0x100);
							   assert_(m+R >= 0 && m+R < 0x100, m+R, m, R, G, B, I, (R+G+B)/3);
							   innerBackgroundColor = {sRGB_reverse[m+B], sRGB_reverse[m+G], sRGB_reverse[m+R], 1};
						   }
						   innerBackgroundColors.append(innerBackgroundColor);
						   for(int y: range(ih0)) {
							   float4* line = target.begin() + (iy0-y-1)*target.stride;
							   for(int x: range(iw0)) {
								   float w = (1-x/float(iw0))*(1-y/float(ih0));
								   line[ix0-x-1] += float4_1(w) * innerBackgroundColor; // Left
							   }
							   for(int x: range(size.x)) line[ix0+x] += float4_1(1-y/float(ih0)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-y/float(ih0))) * innerBackgroundColor; // Right
						   }
						   parallel_chunk(size.y, [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   float4* line = target.begin() + (iy0+y)*target.stride;
								   for(int x: range(iw0)) line[ix0-x-1] += float4_1(1-x/float(iw0)) * innerBackgroundColor; // Left
								   float4* sourceLine = source.begin() + y*source.stride;
								   for(int x: range(size.x)) line[ix0+x] = sourceLine[x]; // Center
								   for(int x: range(iw1)) line[ix1+x] += float4_1(1-x/float(iw1)) * innerBackgroundColor; // Right
							   }
						   });
						   for(int y: range(ih1)) {
							   float4* line = target.begin() + (iy1+y)*target.stride;
							   for(int x: range(iw0)) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-y/float(ih1))) * innerBackgroundColor; // Left
							   for(int x: range(size.x)) line[ix0+x] += float4_1(1-y/float(ih1)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-y/float(ih1))) * innerBackgroundColor; // Right
						   }
					   }
					   x0 += w + inner.x;
				   }
			   }
			}
			// -- Transitions exterior borders to background color
			const int iX = floor(outer.x);
			const int iY = floor(outer.y);
			//float4 outerBackgroundColor = float4_1(1);
			//float4 outerBackgroundColor = float4_1(1./2);
			//float4 outerBackgroundColor = ::mean(target, iX, iY, target.size.x-iX, target.size.y-iY);
			float4 outerBackgroundColor = ::mean(innerBackgroundColors);
			vec2 margin = outer-inner; // Transition on inner margin size, outer outer margin is constant
			// Outer background vertical sides
			parallel_chunk(target.size.y-iY-iY, [&](uint, int Y0, int DY) {
				for(int y: range(iY+Y0, iY+Y0+DY)) {
					float4* line = target.begin() + y*target.stride;
					for(int x: range(iX)) {
						float w = constantMargin ? (x>=margin.x ? 1 - (float(x)-margin.x) / float(inner.x) : 1) : float(outer.x-x) / outer.x;
						assert(w >= 0 && w <= 1);
						float4 c = float4_1(w) * outerBackgroundColor;;
						line[x] += c;
						line[target.size.x-1-x] += c;
					}
				}
			});
			// Outer background horizontal sides
			parallel_chunk(iY, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					float4* line0 = target.begin() + y*target.stride;
					float4* line1 = target.begin() + (target.size.y-1-y)*target.stride;
					float w = constantMargin ? (y>=margin.y ? 1 - float(y-margin.y) / float(inner.y) : 1) : float(outer.y-y) / outer.y;
					assert(w >= 0 && w <= 1);
					for(int x: range(iX, target.size.x-iX)) {
						float4 c = float4_1(w) * outerBackgroundColor;
						line0[x] += c;
						line1[x] += c;
					}
				}
			});
			// Outer background corners
			parallel_chunk(iY, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					float4* line0 = target.begin() + y*target.stride;
					float4* line1 = target.begin() + (target.size.y-1-y)*target.stride;
					float yw = constantMargin ? (y>=margin.y ? float(y-margin.y) / float(inner.y) : 0) : 1 - float(outer.y-y) / outer.y;
					for(int x: range(iX)) {
						float xw =  constantMargin ? (x>=margin.x ? float(x-margin.x) / float(inner.x) : 0) : 1 - float(outer.x-x) / outer.x;
						float w = /*xw*yw +*/ (1-xw)*yw + xw*(1-yw) + (1-xw)*(1-yw);
						assert(w >= 0 && w <= 1);
						float4 c = float4_1(w) * outerBackgroundColor;
						line0[x] += c;
						line0[target.size.x-1-x] += c;
						line1[x] += c;
						line1[target.size.x-1-x] += c;
					}
				}
			});
			if(1) {
				ImageF blur(target.size);
				{// -- Large gaussian blur approximated with box convolution
					ImageF transpose(target.size.y, target.size.x);
					//const int R = min(target.size.x, target.size.y)/2;
					const int R = min(min(widths), min(heights))/8;
					box(transpose, target, R, outerBackgroundColor);
					box(blur, transpose, R, outerBackgroundColor);
				}
				for(Rect r: mask){ // -- Copies source images over blur background
					parallel_chunk(r.size().y, [&](uint, int Y0, int DY) {
						int x0 = r.min.x, y0 =r.min.y, X = r.size().x;
						for(int y: range(Y0, Y0+DY)) for(int x: range(X)) blur(x0+x, y0+y) = target(x0+x, y0+y);
					});
				}
				target = move(blur);
			}
			// -- Convert back to 8bit sRGB
			Image iTarget (target.size);
			assert_(target.Ref::size == iTarget.Ref::size);
			parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
				extern uint8 sRGB_forward[0x1000];
				for(size_t i: range(I0, I0+DI)) {
					//for(uint c: range(3)) assert_(target[i][c] >= 0 && target[i][c] <= 1, target[i][c], i, c);
					iTarget[i] = byte4(sRGB_forward[int(round(0xFFF*min(1.f, target[i][0])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][1])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][2])))]);
				}
			});
			page.blits.append(0, page.bounds.size(), move(iTarget));
		}
		log(total, load, blur);
	}
};

struct MosaicPreview : Mosaic, Application {
	GraphicsWidget view {move(page)};
	Window window {&view, -1, [this](){return unsafeRef(name);}};
};
registerApplication(MosaicPreview);

struct MosaicExport : Mosaic, Application {
	MosaicExport() : Mosaic(300) {
		//writeFile(name+".pdf"_, toPDF(pageSize, page, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), Folder("out", currentWorkingDirectory(), true), true);
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(page.bounds.size())), page))/*, Folder("out"_, currentWorkingDirectory(), true), true*/);
	}
};
registerApplication(MosaicExport, export);
