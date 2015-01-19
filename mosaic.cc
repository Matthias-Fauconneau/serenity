#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"
#include "time.h"
#include "algebra.h"
#include "serialization.h" // parse<vec2>
#include "text.h"

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
	array<String> files = Folder(".").list(Files);
	String name = arguments() && existsFile(arguments()[0]) ? copyRef(arguments()[0]) : copyRef(filter(files, [](string name){return !endsWith(name,"mosaic");})[0]);

	// Page definition
	vec2 pageSizeMM, pageSize;

	// - Layout definition
	array<string> images; // Images
	array<array<int>> rows; // Index into elements (-1: row extension, -2 column extension, -3: inner extension)

	// - Layout solution
	vec2 inner, outer; // Margins
	Vector widths, heights; // Image sizes

	// - Layout render
	Graphics page;

	Mosaic(const float inchPx = 0, bool render=false) {
		files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});

		// - Layout definition
		if(existsFile(name)) { // -- Parses page definition
			TextData s = readFile(name);
			pageSizeMM = 10.f*parse<vec2>(s); // 50x40, 40x30, 114x76
			assert_(pageSizeMM, "Expected page size, got '"+s.line()+"'");
			s.whileAny(" \n");
			while(s) {
				array<int> row;
				for(string name; (name = s.whileNo(" \t\n"));) {
					/***/ if(name=="-") row.append(-1); //{ assert_(row); row.append(row.last()); }
					else if(name=="|") row.append(-2); //{ assert_(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
					else if(name=="\\") row.append(-3); //{ assert_(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
					else {
						row.append(images.size);
						images.append([this](string name) {
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

		const float inchMM = 25.4;
		const float mmPx = inchPx ? inchPx/inchMM : min(1680/pageSizeMM.x, (1050-32-24)/pageSizeMM.y);
		pageSize = pageSizeMM * mmPx;
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
			b[equationIndex] = pageSize.x - 2*outer0.x - (imageCount-1)*inner0.x;
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
			b[equationIndex] = pageSize.y - 2*outer0.y - (imageCount-1)*inner0.y;
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
		heights = copyRef(x.slice(0, images.size));
		widths = apply(heights.size, [&](size_t i){ return aspectRatio[i] * heights[i]; });
		outer = outer0 + vec2(x[images.size], x[images.size+1]);
		inner = inner0 + vec2(x[images.size+2], x[images.size+3]);
		//log(heights, outer, outer0, vec2(x[images.size], x[images.size+1]), inner, inner0, vec2(x[images.size+2], x[images.size+3]));
		if(1) {
			array<char> s;
			float widthSums = 0;
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
						if(columnIndex+1<n && rows[rowIndex][columnIndex+1]!=-1) sum += inner.x;
						s.append(str(w, h));
					}
					s.append("\t");
				}
				sum += outer.x;
				widthSums += sum;
				s.append(str(sum)+"\n");
			}
			float dx = pageSize.x - widthSums / m;
			outer.x += dx/2; // Enforces horizontal center
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
						if(rowIndex+1<m && rows[rowIndex+1][columnIndex]!=-2) sum += inner.y;
					}
				}
				sum += outer.y;
				heightSums += sum;
				s.append(str(sum)+"\t\t");
			}
			float dy = pageSize.y - heightSums / n;
			if(0) log(s,"\t", pageSize, "dx", dx, "dy", dy);
			else log("dx", dx, "dy", dy, pageSize.y, heightSums / n, n, outer.y, outer.y + dy);
			outer.y += dy/2; // Enforces vertical center
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
		if(render) this->render();
	}
	void render() {
		Time load;
		if(0) { // -- White background
			page.bounds = Rect(pageSize);
			for(const size_t rowIndex: range(rows.size)) {
				float x = outer.x;
				for(const size_t columnIndex: range(rows[rowIndex].size)) {
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
					Image image = decodeImage(Map(images[imageIndex])); // FIXME: turbo jpeg
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
			   for(const size_t rowIndex: range(rows.size)) {
				   float x0 = outer.x;
				   for(const size_t columnIndex: range(rows[rowIndex].size)) {
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
					   float rightMargin = (constantMargin || (columnIndex+1<rows[rowIndex].size && rows[rowIndex][columnIndex+1]!=-1)) ? inner.x : outer.x;
					   float belowMargin = (constantMargin || (rowIndex+1<rows.size && rows[rowIndex+1][columnIndex]!=-2)) ? inner.y : outer.y;
					   int iw0 = ix0-round(x0-leftMargin), ih0 = iy0-round(y0-aboveMargin); // Previous border sizes
					   float x1 = x0+w, y1 = y0+h;
					   int ix1 = round(x1), iy1 = round(y1);
					   int iw1 = /*min(target.size.x,*/ int(round(x1+rightMargin))/*)*/ - ix1, ih1 = /*min(target.size.y,*/ int(round(y1+belowMargin))/*)*/ - iy1; // Next border sizes
					   //assert_(iw0 >= 1 && ih0 >= 1 && iw1 >= 1 && ih1 >= 1, iw0, ih0, iw1, ih1);
					   mask.append(vec2(ix0, iy0), vec2(ix1, iy1));
					   int2 size(ix1-ix0, iy1-iy0);
					   assert_(/*0 <= iw0 &&*/ iw0 <= ix0 /*&& ix0+size.x < target.size.x*/, /*iw0, ix0, size.x,*/ ix0+size.x, target.size.x);
					   //assert_(ix1+iw1 <= target.size.x, ix1, iw1, target.size.x, inner, outer);
					   //assert_(iy0 >= ih0 && iy0+size.y < target.size.y);
					   //assert_(iy1+ih1 <= target.size.y, iy1, ih1, iy1+ih1, target.size.y, inner, outer);

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
						   if(ih0 > 0) parallel_chunk(ih0, [&](uint, int Y0, int DY) { // Top
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy0-y-1) += float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy0-y-1) += float4_1(1-y/float(ih0-1)) * source(x, y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy0-y-1) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, y); // Right
							   }
						   });
						   parallel_chunk(size.y, [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy0+y) += float4_1(1-x/float(iw0-1)) * source(x, y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy0+y) = source(x, y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy0+y) += float4_1(1-x/float(iw1-1)) * source(size.x-1-x, y); // Right
							   }
						   });
						   if(ih1 > 0) parallel_chunk(ih1, [&](uint, int Y0, int DY) { // Bottom
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(iw0)) target(ix0-x-1, iy1+y) +=  float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, size.y-1-y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy1+y) += float4_1(1-y/float(ih1-1)) * source(x, size.y-1-y); // Center
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
							   int C = 0x80; // FIXME: Parse user parameter
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
							   assert_(R >= 0 && R < 0x100);
							   assert_(G >= 0 && G < 0x100);
							   assert_(B >= 0 && B < 0x100);
							   int m = max(0, I - (R+G+B)/3);
							   assert_(m >= 0 && m < 0x100, I, R, G, B, m);
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
							   mref<float4> line = target.slice((iy0-y-1)*target.stride, target.width);
							   for(int x: range(iw0)) {
								   float w = (1-x/float(iw0))*(1-y/float(ih0));
								   line[ix0-x-1] += float4_1(w) * innerBackgroundColor; // Left
							   }
							   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-y/float(ih0)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-y/float(ih0))) * innerBackgroundColor; // Right
						   }
						   parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   float4* line = target.begin() + y*target.stride;
								   for(int x: range(iw0)) line[ix0-x-1] += float4_1(1-x/float(iw0)) * innerBackgroundColor; // Left
								   float4* sourceLine = source.begin() + (y-iy0)*source.stride;
								   for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Center
								   for(int x: range(iw1)) line[ix1+x] += float4_1(1-x/float(iw1)) * innerBackgroundColor; // Right
							   }
						   });
						   for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
							   float4* line = target.begin() + y*target.stride;
							   for(int x: range(iw0)) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Left
							   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-(y-iy1)/float(ih1)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Right
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
			if(iX > 0) parallel_chunk(max(0, iY), min(target.size.y, target.size.y-iY), [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
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
			if(iY > 0) parallel_chunk(iY, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					float4* line0 = target.begin() + y*target.stride;
					float4* line1 = target.begin() + (target.size.y-1-y)*target.stride;
					float w = constantMargin ? (y>=margin.y ? 1 - float(y-margin.y) / float(inner.y) : 1) : float(outer.y-y) / outer.y;
					assert(w >= 0 && w <= 1);
					for(int x: range(max(0, iX), min(target.size.x, target.size.x-iX))) {
						float4 c = float4_1(w) * outerBackgroundColor;
						line0[x] += c;
						line1[x] += c;
					}
				}
			});
			// Outer background corners
			if(iX > 0 && iY > 0) parallel_chunk(iY, [&](uint, int Y0, int DY) {
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
				{// -- Large gaussian blur approximated with repeated box convolution
					ImageF transpose(target.size.y, target.size.x);
					//const int R = min(target.size.x, target.size.y)/8;
					const int R = max(min(widths), min(heights))/8;
					box(transpose, target, R, outerBackgroundColor);
					box(blur, transpose, R, outerBackgroundColor);
					box(transpose, blur, R, outerBackgroundColor);
					box(blur, transpose, R, outerBackgroundColor);
				}
				for(Rect r: mask){ // -- Copies source images over blur background
					parallel_chunk(max(0, int(r.min.y)), min(target.size.y, int(r.max.y)), [&](uint, int Y0, int DY) {
						for(int y: range(Y0, Y0+DY)) {
							float4* blurLine = blur.begin() + y*blur.stride;
							const float4* targetLine = target.begin() + y*target.stride;
							for(int x: range(max(0, int(r.min.x)), min(target.size.x, int(r.max.x)))) blurLine[x] = targetLine[x];
						}
					});
				}
				target = move(blur);
			}
			// -- Convert back to 8bit sRGB
			Image iTarget (target.size);
			assert_(target.Ref::size == iTarget.Ref::size);
			parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
				extern uint8 sRGB_forward[0x1000];
				int clip = 0;
				for(size_t i: range(I0, I0+DI)) {
					for(uint c: range(3)) if(!(target[i][c] >= 0 && target[i][c] <= 1)) { if(!clip) log("Clip", target[i][c], i, c); clip++; }
					iTarget[i] = byte4(sRGB_forward[int(round(0xFFF*min(1.f, target[i][0])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][1])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][2])))]);
				}
				if(clip) log("Clip", clip);
				//assert_(!clip);
			});
			page.blits.append(0, page.bounds.size(), move(iTarget));
		}
		log(load);
	}
};

struct MosaicPreview : Mosaic, Application {
	GraphicsWidget view {move(page)};
	Window window {&view, -1, [this](){return copyRef(name);}};
	MosaicPreview() : Mosaic(0, true) {}
};
registerApplication(MosaicPreview, preview);

struct MosaicExport : Mosaic, Application, Poll {
	Text text {" Processing "+name+" ... "};
	Window window {&text, -1, [this](){return copyRef(name);}};
	MosaicExport() : Mosaic(300) { queue(); }
	void event() {
		render(); // TODO: asynchronous
		//writeFile(name+".pdf"_, toPDF(pageSize, page, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), Folder("out", currentWorkingDirectory(), true), true);
		writeFile((endsWith(name, ".mosaic")?section(name,'.',0,-2):name)+'.'+strx(int2(round(pageSizeMM/10.f)))+".jpg"_, encodeJPEG(::render(int2(round(page.bounds.size())), page)), currentWorkingDirectory(), endsWith(name, ".mosaic"));
		window.unregisterPoll();
		unregisterPoll();
		requestTermination(0);
	}
};
registerApplication(MosaicExport);
