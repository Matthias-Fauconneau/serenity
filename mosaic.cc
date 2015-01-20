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
inline ImageF copy(const ImageF& o) {
	if(o.width == o.stride) return ImageF(copy((const buffer<float4>&)o), o.size, o.stride);
	ImageF target(o.size);
	for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
	return target;
}
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
	array<string> elements; // Images
	array<array<int>> rows; // Index into elements (-1: row extension, -2 column extension, -3: inner extension)

	// - Layout solution
	vec2 inner, outer; // Margins
	Vector widths, heights; // Image sizes

	// - Layout render
	Graphics page;

	Mosaic(const float inchPx = 0, bool render=false) {
		files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
		bool rowStructure = false, tableStructure = false;

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
						row.append(elements.size);
						elements.append([this](string name) {
							for(string file: files) if(startsWith(file, name)) return file;
							error("No such image"_, name, "in", files);
						}(name));
					}
					s.whileAny(" \t"_);
				}
				assert_(row);
				// Automatically generate table structure from row structure
				for(auto& o : rows) {
					if(o.size < row.size) { assert_(o.size==1); o.append(-1); rowStructure=true; tableStructure=false; }
					if(row.size < o.size) { assert_(row.size==1); row.append(-1); rowStructure=true; tableStructure=false; }
				}
				rows.append(move(row));
				if(!s) break;
				s.skip('\n');
			}
		} else { // Layouts all elements
			error("No such file", name, "in", currentWorkingDirectory().name());
			int X = round(sqrt(float(files.size))), Y = X;
			for(int y: range(Y)) {
				array<int> row;
				for(int x: range(X)) {
					if(y*X+x >= int(files.size)) break;
					elements.append(files[y*X+x]);
					row.append(y*X+x);
				}
				assert_(row);
				rows.append(move(row));
			}
		}
		assert_(rows);
		const size_t m = rows.size; // Row count

		const float inchMM = 25.4;
		const float mmPx = inchPx ? inchPx/inchMM : min(1680/pageSizeMM.x, (1050-32-24)/pageSizeMM.y);
		pageSize = pageSizeMM * mmPx;
		buffer<int2> sizes = apply(elements, [=](string image){ return ::imageSize(Map(image)); }); // Image sizes
		Vector aspectRatios = apply(sizes, [=](int2 size){ return (float)size.x/size.y; }); // Image aspect ratios

		vec2 inner0 = vec2(8*mmPx), outer0 = vec2((8+5)*mmPx);

		const size_t k = elements.size + 2 + 2; // Unknowns (heights + margins)
		size_t uniformColumnCount = rows[0].size;
		for(ref<int> row: rows) {
			if(row.size != rows[0].size) { uniformColumnCount = 0; assert_(rows[0].size == 1); break; }
		}
		bool sameWidthsInColumn = tableStructure && !rowStructure; // Disable same width constraints when layout definition had a row structure
		bool sameHeightsInRow = tableStructure && uniformColumnCount>1; // Disable same height constraints when layout definition has no table structure or column count are not uniform
		bool sameElementSizes = !sameWidthsInColumn && !sameHeightsInRow; // Enables same size constraints when no same side in linear constraints are enabled
		size_t equationCount =
				m + // Fit row width
				uniformColumnCount + // Fit column height
				(sameWidthsInColumn?uniformColumnCount*(m-1):0) + // Same widths in column
				(sameHeightsInRow?m*(uniformColumnCount-1):0) + // Same heights in row
				(sameElementSizes?(elements.size-1)*2:0) + // Same image sizes (width, height)
				4; // Same margins
		assert_(equationCount >= k);
		Matrix A (equationCount, k); Vector b(equationCount); // Linear system
		size_t equationIndex = 0;
		for(const size_t rowIndex : range(m)) { // Row equations
			size_t imageCount = 0;
			for(const size_t columnIndex: range(rows[rowIndex].size)) { // Fit height
				int elementIndex = rows[rowIndex][columnIndex];
				if(elementIndex >=0) { // Normal image root origin instance
					assert_(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = aspectRatios[elementIndex];
					imageCount++;
				}
				else if(elementIndex == -2) { // Column extension across rows
					size_t sourceRowIndex = rowIndex-1;
					while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
					int elementIndex = rows[sourceRowIndex][columnIndex];
					assert_(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = aspectRatios[elementIndex];
					imageCount++;
				}
				else {
					assert_(elementIndex == -1 || elementIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, elements.size) = 2; // Vertical outer margin width
			A(equationIndex, elements.size+2) = imageCount-1; // Vertical inner margin width
			b[equationIndex] = pageSize.x - 2*outer0.x - (imageCount-1)*inner0.x;
			equationIndex++;
		}
		for(const size_t columnIndex : range(uniformColumnCount)) { // Column equations
			size_t imageCount = 0;
			for(const size_t rowIndex: range(m)) { // Fit width
				int elementIndex = rows[rowIndex][columnIndex];
				if(elementIndex >=0) { // Normal image root origin instance
					assert_(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = 1;
					imageCount++;
				}
				else if(elementIndex == -1) { // Row extension across columns
					size_t sourceColumnIndex = columnIndex-1;
					while(rows[rowIndex][sourceColumnIndex] == -1) sourceColumnIndex--;
					int elementIndex = rows[rowIndex][sourceColumnIndex];
					assert_(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = 1;
					imageCount++;
				}
				else {
					assert_(elementIndex == -2 || elementIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, elements.size+1) = 2; // Horizontal outer margin height
			A(equationIndex, elements.size+3) = imageCount-1; // Horizontal inner margin height
			b[equationIndex] = pageSize.y - 2*outer0.y - (imageCount-1)*inner0.y;
			equationIndex++;
		}
		if(sameWidthsInColumn) { // Same width in column
			for(const size_t columnIndex : range(uniformColumnCount)) { // Column same width constraint
				size_t firstRowIndex = invalid;
				for(const size_t rowIndex: range(m-1)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >= 0 && (columnIndex+1==uniformColumnCount || rows[rowIndex][columnIndex+1]!=-1)) { firstRowIndex = rowIndex; break; }
				}
				if(firstRowIndex == invalid) continue;

				for(const size_t rowIndex: range(firstRowIndex+1, m)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >=0 && (columnIndex==uniformColumnCount-1 || rows[rowIndex][columnIndex+1]!=-1)) { // Single row image
						assert_(A(equationIndex, elementIndex)==0);
						// - w[firstColumnIndex] + w[columnIndex] = 0
						A(equationIndex, rows[firstRowIndex][columnIndex]) = -aspectRatios[rows[firstRowIndex][columnIndex]];
						A(equationIndex, elementIndex) = aspectRatios[elementIndex];
						b[equationIndex] = 0;
						equationIndex++;
					}
				}
			}
		}
		if(sameHeightsInRow) { // Same heights in row constraint
			for(const size_t rowIndex : range(m)) {
				size_t firstColumnIndex = invalid;
				for(const size_t columnIndex: range(uniformColumnCount-1)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >= 0 && (rowIndex+1==m || rows[rowIndex+1][columnIndex]!=-2)) { firstColumnIndex = columnIndex; break; }
				}
				if(firstColumnIndex == invalid) continue;

				for(const size_t columnIndex: range(firstColumnIndex+1, uniformColumnCount)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >=0 && (rowIndex==m-1 || rows[rowIndex+1][columnIndex]!=-2)) { // Single row image
						assert_(A(equationIndex, elementIndex)==0);
						// - h[@firstColumnIndex] + h[@columnIndex] = 0
						A(equationIndex, rows[rowIndex][firstColumnIndex]) = -1;
						A(equationIndex, elementIndex) = 1;
						b[equationIndex] = 0;
						equationIndex++;
					}
				}
			}
		}
		if(sameElementSizes) { // Same image sizes constraint (FIXME: not symetric (which is important for least square solution (when no exact solution exists))
			for(const size_t elementIndex : range(1, elements.size)) {
				assert_(A(equationIndex, elementIndex)==0);
				// - heights[0] + heights[elementIndex] = 0
				A(equationIndex, 0) = -1;
				A(equationIndex, elementIndex) = 1;
				b[equationIndex] = 0;
				equationIndex++;
				// - widths[0] + widths[elementIndex] = 0
				A(equationIndex, 0) = -aspectRatios[0];
				A(equationIndex, elementIndex) = aspectRatios[elementIndex];
				b[equationIndex] = 0;
				equationIndex++;
				// FIXME: Same area would require quadratic programming
			}
		}
		{
			const size_t marginIndexBase = elements.size;
			A(equationIndex, marginIndexBase+0) = -1; A(equationIndex, marginIndexBase+1) = 1; equationIndex++; // outer.x = outer.y
			A(equationIndex, marginIndexBase+0) = -1; A(equationIndex, marginIndexBase+2) = 1; equationIndex++; // outer.x = inner.x
			A(equationIndex, marginIndexBase+2) = -1; A(equationIndex, marginIndexBase+3) = 1; equationIndex++; // inner.x = inner.y
			A(equationIndex, marginIndexBase+1) = -1; A(equationIndex, marginIndexBase+3) = 1; equationIndex++; // outer.y = inner.y
		}
		A.m = equationIndex;
		//log(str(b)+"\n"+str(A));
		// Least square system
		// TODO: Weighted least square
		Matrix At = transpose(A);
		Matrix AtA = At * A;
		{// Regularize border parameters
			const float a = 1;
			for(size_t i: range(elements.size,  elements.size+4)) AtA(i,i) = AtA(i,i) + a*a;
		}
		Vector Atb = At * b;
		Vector x = solve(move(AtA),  Atb);
		heights = copyRef(x.slice(0, elements.size));
		widths = apply(heights.size, [&](size_t i){ return aspectRatios[i] * heights[i]; });
		outer = outer0 + vec2(x[elements.size], x[elements.size+1]);
		inner = inner0 + vec2(x[elements.size+2], x[elements.size+3]);
		//log(heights, outer, outer0, vec2(x[elements.size], x[elements.size+1]), inner, inner0, vec2(x[elements.size+2], x[elements.size+3]));
		if(1) {
			array<char> s;
			float widthSums = 0;
			for(size_t rowIndex : range(m)) {
				float sum = outer.x;
				for(size_t columnIndex : range(rows[rowIndex].size)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex == -1) s.append("-");
					if(elementIndex == -3) s.append("\\");
					if(elementIndex == -2) {
						int sourceRowIndex = rowIndex-1;
						while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						elementIndex = rows[sourceRowIndex][columnIndex];
					}
					if(elementIndex >= 0) {
						float w = widths[elementIndex], h = heights[elementIndex];
						sum += w;
						if(columnIndex+1<rows[rowIndex].size && rows[rowIndex][columnIndex+1]!=-1) sum += inner.x;
						s.append(str(w, h));
					}
					s.append("\t");
				}
				sum += outer.x;
				widthSums += sum;
				s.append(str(sum)+"\n");
			}
			float dx = pageSize.x - widthSums / m;
			outer.x += dx/2; // Centers horizontally (i.e distribute tolerated error (least square residual) on each side)
			float heightSums = 0;
			for(size_t columnIndex : range(uniformColumnCount)) {
				float sum = outer.y;
				for(size_t rowIndex : range(m)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex == -1) {
						int sourceColumnIndex = columnIndex-1;
						while(rows[rowIndex][sourceColumnIndex] == -2) sourceColumnIndex--;
						elementIndex = rows[rowIndex][sourceColumnIndex];
					}
					if(elementIndex >= 0) {
						float h = heights[elementIndex];
						sum += h;
						if(rowIndex+1<m && rows[rowIndex+1][columnIndex]!=-2) sum += inner.y;
					}
				}
				sum += outer.y;
				heightSums += sum;
				s.append(str(sum)+"\t\t");
			}
			float dy = pageSize.y - heightSums / uniformColumnCount;
			if(0) log(s,"\t", pageSize, "dx", dx, "dy", dy);
			//else log("dx", dx, "dy", dy, pageSize.y, heightSums / n, n, outer.y, outer.y + dy);
			outer.y += dy/2; // Centers vertically (i.e distribute tolerated error (least square residual) on each side)
		}
		if(1) {
			float minScale = inf, maxScale = 0;
			for(size_t elementIndex: range(elements.size)) {
				float scale = sizes[elementIndex].x / widths[elementIndex];
				minScale = min(minScale, scale);
				maxScale = max(maxScale, scale);
			}
			log((inchPx?:300), "min", minScale, minScale*(inchPx?:300), "max", maxScale, maxScale*(inchPx?:300));
		}
		if(render) this->render();
	}
	void render() {
		Time load;
		load.start();
		buffer<Image> images = apply(elements.size, [this](const size_t elementIndex) {
			Image image = decodeImage(Map(elements[elementIndex]));
			image.alpha = false;
			return move(image);
		});
		load.stop();

		   buffer<float4> innerBackgroundColors = apply(images.size, [&](const size_t elementIndex) {
			   const Image& iSource = images[elementIndex];
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
				   assert_(iSource.Ref::size < 1<<24);
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
				   int C = 0x20; // FIXME: Parse user parameter
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
			   return innerBackgroundColor;
		   });

		   page.bounds = Rect(round(pageSize));
		   ImageF target(int2(page.bounds.size()));
		   assert_(ptr(target.data)%sizeof(float4)==0);
		   if(target.Ref::size < 8*1024*1024) target.clear(float4_1(0)); // Assumes larger allocation are clear pages

		   // -- Transitions exterior borders to background color
		   const int iX = floor(outer.x);
		   const int iY = floor(outer.y);
		   //float4 outerBackgroundColor = float4_1(1);
		   //float4 outerBackgroundColor = float4_1(1./2);
		   //float4 outerBackgroundColor = ::mean(target, iX, iY, target.size.x-iX, target.size.y-iY);
		   float4 outerBackgroundColor = ::mean(innerBackgroundColors);
		   vec2 margin = outer-inner; // Transition on inner margin size, outer outer margin is constant
		   int2 size = target.size;
		   bool constantMargin = false;
		   // Outer background vertical sides
		   if(iX > 0) parallel_chunk(max(0, iY), min(size.y, size.y-iY), [&](uint, int Y0, int DY) {
			   for(int y: range(Y0, Y0+DY)) {
				   float4* line = target.begin() + y*target.stride;
				   for(int x: range(iX)) {
					   float w = constantMargin ? (x>=margin.x ? 1 - (float(x)-margin.x) / float(inner.x) : 1) : float(outer.x-x) / outer.x;
					   assert(w >= 0 && w <= 1);
					   float4 c = float4_1(w) * outerBackgroundColor;;
					   line[x] += c;
					   line[size.x-1-x] += c;
				   }
			   }
		   });
		   // Outer background horizontal sides
		   if(iY > 0) parallel_chunk(iY, [&](uint, int Y0, int DY) {
			   for(int y: range(Y0, Y0+DY)) {
				   float4* line0 = target.begin() + y*target.stride;
				   float4* line1 = target.begin() + (size.y-1-y)*target.stride;
				   float w = constantMargin ? (y>=margin.y ? 1 - float(y-margin.y) / float(inner.y) : 1) : float(outer.y-y) / outer.y;
				   assert(w >= 0 && w <= 1);
				   for(int x: range(max(0, iX), min(size.x, size.x-iX))) {
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
					   line0[size.x-1-x] += c;
					   line1[x] += c;
					   line1[size.x-1-x] += c;
				   }
			   }
		   });

		   array<Rect> mask; // To copy unblurred data on blur background
		   { // -- Copies source images to target mosaic
			   for(const size_t rowIndex: range(rows.size)) {
				   float x0 = outer.x;

				   // -- Horizontal centers individual row (i.e distribute tolerated error (least square residual) on each side)
				   float W = outer.x;
				   for(const size_t columnIndex: range(rows[rowIndex].size)) {
					   int elementIndex = rows[rowIndex][columnIndex];
					   if(elementIndex == -2) { // Column extension
						   int sourceRowIndex = rowIndex-1;
						   while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						   elementIndex = rows[sourceRowIndex][columnIndex];
					   }
					   if(elementIndex < 0) continue;
					   float w = widths[elementIndex];
					   float rightMargin = (constantMargin || (columnIndex+1<rows[rowIndex].size && rows[rowIndex][columnIndex+1]!=-1)) ? inner.x : outer.x;
					   W += w + rightMargin;
				   }
				   float dx = (pageSize.x - W) / 2;
				   x0 += dx;

				   for(const size_t columnIndex: range(rows[rowIndex].size)) {
					   int elementIndex = rows[rowIndex][columnIndex];
					   if(elementIndex == -2) { // Column extension
						   int sourceRowIndex = rowIndex-1;
						   while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						   int elementIndex = rows[sourceRowIndex][columnIndex];
						   float w = widths[elementIndex];
						   x0 += w + inner.x;
					   }
					   if(elementIndex < 0) continue;

					   // -- Horizontal centers individual row (i.e distribute tolerated error (least square residual) on each side)
					   float H = outer.y, y0 = outer.y;
					   for(const size_t i: range(rows.size)) {
						   int elementIndex = rows[i][columnIndex];
						   if(elementIndex == -1) { // Row extension
							   int sourceColumnIndex = columnIndex-1;
							   while(rows[i][sourceColumnIndex] == -2) sourceColumnIndex--;
							   elementIndex = rows[i][sourceColumnIndex];
						   }
						   if(elementIndex < 0) continue;
						   float belowMargin = (constantMargin || (rowIndex+1<rows.size && rows[rowIndex+1][columnIndex]!=-2)) ? inner.y : outer.y;
						   H += heights[elementIndex] + belowMargin;
						   if(i==rowIndex-1) y0 = H;
					   }
					   float dy = (pageSize.y - H) / 2;
					   y0 += dy;

					   float w = widths[elementIndex], h = heights[elementIndex];
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
					   //assert_(/*0 <= iw0 &&*/ iw0 <= ix0 /*&& ix0+size.x < target.size.x*/, /*iw0, ix0, size.x,*/ ix0+size.x, target.size.x);
					   //assert_(ix1+iw1 <= target.size.x, ix1, iw1, target.size.x, inner, outer);
					   //assert_(iy0 >= ih0 && iy0+size.y < target.size.y);
					   //assert_(iy1+ih1 <= target.size.y, iy1, ih1, iy1+ih1, target.size.y, inner, outer);
					   assert_(ix0 < target.size.x);

					   const Image& image = images[elementIndex];
					   Image iSource = size == image.size ? share(image) : resize(size, image); // TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
					   ImageF source (size);
					   parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
						   extern float sRGB_reverse[0x100];
						   for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]], 1};
					   });
					   // -- Margins
					   if(0) { // -- Extends images over margins with a mirror transition
						   if(ih0 > 0) parallel_chunk(ih0, [&](uint, int Y0, int DY) { // Top
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(min(iw0, ix0))) target(ix0-x-1, iy0-y-1) += float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy0-y-1) += float4_1(1-y/float(ih0-1)) * source(x, y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy0-y-1) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, y); // Right
							   }
						   });
						   parallel_chunk(size.y, [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(min(iw0, ix0))) target(ix0-x-1, iy0+y) += float4_1(1-x/float(iw0-1)) * source(x, y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy0+y) = source(x, y); // Center (= and not += to mask outer transition over uneven rows/columns)
								   for(int x: range(iw1)) target(ix1+x, iy0+y) += float4_1(1-x/float(iw1-1)) * source(size.x-1-x, y); // Right
							   }
						   });
						   if(ih1 > 0) parallel_chunk(ih1, [&](uint, int Y0, int DY) { // Bottom
							   for(int y: range(Y0, Y0+DY)) {
								   for(int x: range(min(iw0, ix0))) target(ix0-x-1, iy1+y) +=  float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, size.y-1-y); // Left
								   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) target(x, iy1+y) += float4_1(1-y/float(ih1-1)) * source(x, size.y-1-y); // Center
								   for(int x: range(iw1)) target(ix1+x, iy1+y) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, size.y-1-y); // Right
							   }
						   });
					   } else if(1) { // -- Extends images over margins with a constant transition
						   float4 innerBackgroundColor = innerBackgroundColors[elementIndex];
						   for(int y: range(min(ih0, iy0))) {
							   mref<float4> line = target.slice((iy0-y-1)*target.stride, target.width);
							   for(int x: range(min(iw0, ix0))) {
								   float w = (1-x/float(iw0))*(1-y/float(ih0));
								   line[ix0-x-1] += float4_1(w) * innerBackgroundColor; // Left
							   }
							   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-y/float(ih0)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-y/float(ih0))) * innerBackgroundColor; // Right
						   }
						   parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
							   for(int y: range(Y0, Y0+DY)) {
								   float4* line = target.begin() + y*target.stride;
								   for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1(1-x/float(iw0)) * innerBackgroundColor; // Left
								   float4* sourceLine = source.begin() + (y-iy0)*source.stride;
								   for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Center
								   for(int x: range(iw1)) line[ix1+x] += float4_1(1-x/float(iw1)) * innerBackgroundColor; // Right
							   }
						   });
						   for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
							   float4* line = target.begin() + y*target.stride;
							   for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Left
							   for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-(y-iy1)/float(ih1)) * innerBackgroundColor; // Center
							   for(int x: range(iw1)) line[ix1+x] += float4_1((1-x/float(iw1))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Right
						   }
					   }
					   x0 += w + inner.x;
				   }
			   }
			}
			if(1) {
				ImageF blur(target.size);
				{// -- Large gaussian blur approximated with repeated box convolution
					ImageF transpose(target.size.y, target.size.x);
					const int R = min(target.size.x, target.size.y)/8;
					//const int R = max(min(widths), min(heights))/4; //8
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
