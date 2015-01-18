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
inline float4 mean(const ImageF& image, int x0, int y0, int x1, int y1) {
	float4 sum = float4_1(0);
	for(int y: range(y0, y1)) for(int x: range(x0, x1)) sum += image(x, y);
	return  sum / float4_1((y1-y0)*(x1-x0));
}

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
		const float mmPx = inchPx ? inchPx/inchMM : min(1680/pageSizeMM.x, (1050-32)/pageSizeMM.y);
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
		buffer<float> aspectRatio = apply(imageSize, [=](int2 size){ return (float)size.x/size.y; }); // Image aspect ratios

		vec2 inner0 = vec2(8*mmPx), outer0 = vec2(8+5*mmPx);

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
			A(equationCounter+0, marginIndexBase+0) = -1; A(equationCounter+0, marginIndexBase+1) = 1; // inner.x = inner.y
			A(equationCounter+1, marginIndexBase+0) = -1; A(equationCounter+1, marginIndexBase+2) = 1; // inner.x = outer.x
			A(equationCounter+2, marginIndexBase+2) = -1; A(equationCounter+2, marginIndexBase+3) = 1; // outer.x = outer.y
			A(equationCounter+3, marginIndexBase+1) = -1; A(equationCounter+3, marginIndexBase+3) = 1; // inner.y = outer.y
			equationCounter += 4;
		}
		//log(str(b)+"\n"+str(A));
		// "Least square" system
		// TODO: positive constraint
		Matrix At = transpose(A);
		Matrix AtA = At * A;
		{// Regularize border parameters
			const float a = 1;
			for(size_t i: range(images.size,  images.size+4)) AtA(i,i) = AtA(i,i) + a*a;
		}
		Vector Atb = At * b;
		Vector x = solve(move(AtA),  Atb);
		Vector heights = copyRef(x.slice(0, images.size));
		vec2 outer = outer0 + vec2(x[images.size], x[images.size+1]);
		vec2 inner = inner0 + vec2(x[images.size+2], x[images.size+3]);
		//log(heights, outer, outer0, vec2(x[images.size], x[images.size+1]), inner, inner0, vec2(x[images.size+2], x[images.size+3]));
		if(0) {
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
						float h = heights[imageIndex];
						float w = aspectRatio[imageIndex] * h;
						sum += w;
						if(columnIndex+1<n && rows[rowIndex][columnIndex+1]>=0) sum += inner.x;
						s.append(str(w, h));
					}
					s.append("\t");
				}
				sum += outer.x;
				s.append(str(sum)+"\n");
			}
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
				s.append(str(sum)+"\t\t");
			}
			log(s,"\tH", H, "\tW", W);
		}
		if(1) {
			float minScale = inf, maxScale = 0;
			for(size_t imageIndex: range(images.size)) {
				float scale = imageSize[imageIndex].y / heights[imageIndex];
				minScale = min(minScale, scale);
				maxScale = max(maxScale, scale);
			}
			log((inchPx?:300), "min", minScale, minScale*(inchPx?:300), "max", maxScale, maxScale*(inchPx?:300));
		}
		if(0) { // -- No background
			page.bounds = Rect(pageSize);
			for(const size_t rowIndex: range(m)) {
				float x = outer.x;
				for(const size_t columnIndex: range(n)) {
					int imageIndex = rows[rowIndex][columnIndex];
					if(imageIndex == -2) { // Column extension
						int sourceRowIndex = rowIndex-1;
						while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						int imageIndex = rows[sourceRowIndex][columnIndex];
						float h = heights[imageIndex];
						float w = aspectRatio[imageIndex] * h;
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
					float h = heights[imageIndex];
					float w = aspectRatio[imageIndex] * h;
					assert_(w > 0 && h > 0, w, h);
					load.start();
					Image image = decodeImage(Map(images[imageIndex]));
					image.alpha = false;
					load.stop();
					page.blits.append(vec2(x,y), vec2(w, h), move(image));
					x += w + inner.x;
				}
			}
		} else {
		   page.bounds = Rect(round(pageSize));
		   ImageF target(int2(page.bounds.size()));
		   if(target.Ref::size < 8*1024*1024) target.clear(float4_1(0)); // Assumes larger allocation are clear pages
		   array<Rect> mask; // To copy unblurred data on blur background
		   { // -- Copies source images to target mosaic
			   for(const size_t rowIndex: range(m)) {
				   float x0 = outer.x;
				   for(const size_t columnIndex: range(n)) {
					   int imageIndex = rows[rowIndex][columnIndex];
					   if(imageIndex == -2) { // Column extension
						   int sourceRowIndex = rowIndex-1;
						   while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
						   int imageIndex = rows[sourceRowIndex][columnIndex];
						   float h = heights[imageIndex];
						   float w = aspectRatio[imageIndex] * h;
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
					   float h = heights[imageIndex];
					   float w = aspectRatio[imageIndex] * h;
					   assert_(w > 0 && h > 0, w, h);

					   int ix0 = round(x0), iy0 = round(y0);
					   int iw0 = ix0-round(x0-inner.x), ih0 = iy0-round(y0-inner.y); // Previous border sizes (TODO: select inner/outer correctly)
					   float x1 = x0+w, y1 = y0+h;
					   int ix1 = round(x1), iy1 = round(y1);
					   int iw1 = round(x1+inner.x) - ix1, ih1 = round(y1+inner.y) - iy1; // Next border sizes (TODO: select inner/outer correctly)
					   assert_(iw0 > 1 && ih0 > 1 && iw1 > 1 && ih1 > 1);
					   mask.append(vec2(ix0, iy0), vec2(ix1, iy1));
					   int2 size(ix1-ix0, iy1-iy0);
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
					   // -- Extends images over margins with a mirror transition
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
							   for(int x: range(iw1)) target(ix1+x, iy0+y)  += float4_1(1-x/float(iw1-1)) * source(size.x-1-x, y); // Right
						   }
					   });
					   parallel_chunk(ih1, [&](uint, int Y0, int DY) { // Bottom
						   for(int y: range(Y0, Y0+DY)) {
							   for(int x: range(iw0)) target(ix0-x-1, iy1+y) +=  float4_1((1-x/float(iw0-1))*(1-y/float(ih0-1))) * source(x, size.y-1-y); // Left
							   for(int x: range(size.x)) target(ix0+x, iy1+y) += float4_1(1-y/float(ih1-1)) * source(x, size.y-1-y); // Center
							   for(int x: range(iw1)) target(ix1+x, iy1+y) += float4_1((1-x/float(iw1-1))*(1-y/float(ih0-1))) * source(size.x-1-x, size.y-1-y); // Right
						   }
					   });
					   x0 += w + inner.x;
				   }
			   }
			}
			// -- Transitions exterior borders to total mean color
			const int iX = floor(outer.x);
			const int iY = floor(outer.y);
			float4 mean = ::mean(target, iX, iY, target.size.x-iX, target.size.y-iY);
			// White vertical sides
			parallel_chunk(target.size.y-iY-iY, [&](uint, int Y0, int DY) {
				for(int y: range(iY+Y0, iY+Y0+DY)) {
					for(int x: range(iX)) {
						float4 w = float4_1(float(outer.x-1-x) / float(outer.x-1)) * mean;;
						target(x, y) += w;
						target(target.size.x-1-x, y) += w;
					}
				}
			});
			// White horizontal sides
			parallel_chunk(iY, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					for(int x: range(iX, target.size.x-iX)) {
						float4 w = float4_1(float(outer.y-1-y) / float(outer.y-1)) * mean;
						target(x, y) += w;
						target(x, target.size.y-1-y) += w;
					}
				}
			});
			// White corners
			parallel_chunk(iY, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					for(int x: range(iX)) {
						float xw = x / float(outer.x-1);
						float yw = y / float(outer.y-1);
						float4 w = float4_1(/*(xw*yw) +*/ ((1-xw)*yw) + (xw*(1-yw)) + ((1-xw)*(1-yw))) * mean;
						target(x, y) += w;
						target(target.size.x-1-x, y) += w;
						target(x, target.size.y-1-y) += w;
						target(target.size.x-1-x, target.size.y-1-y) += w;
					}
				}
			});
			ImageF blur(target.size);
			{// -- Large gaussian blur approximated with repeated box convolution
				ImageF transpose(target.size.y, target.size.x);
				const int R = min(target.size.x, target.size.y)/2;
				box(transpose, target, R, mean);
				box(blur, transpose, R, mean);
			}
			for(Rect r: mask){ // -- Copies source images over blur background
				parallel_chunk(r.size().y, [&](uint, int Y0, int DY) {
					int x0 = r.min.x, y0 =r.min.y, X = r.size().x;
					for(int y: range(Y0, Y0+DY)) for(int x: range(X)) blur(x0+x, y0+y) = target(x0+x, y0+y);
				});
			}
			target = move(blur);
			// -- Convert back to 8bit sRGB
			Image iTarget (target.size);
			assert_(target.Ref::size == iTarget.Ref::size);
			parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
				extern uint8 sRGB_forward[0x1000];
				//for(size_t i: range(I0, I0+DI)) iTarget[i] = byte4(sRGB_forward[int(round(0xFFF*target[i][0]))], sRGB_forward[int(round(0xFFF*target[i][1]))], sRGB_forward[int(round(0xFFF*target[i][2]))]); //FIXME
				for(size_t i: range(I0, I0+DI)) {
					for(uint c: range(3)) assert_(target[i][c] >= 0 && target[i][c] <= 1, target[i][c], i, c);
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
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(page.bounds.size())), page)), Folder("out", currentWorkingDirectory(), true), true);
	}
};
registerApplication(MosaicExport, export);
