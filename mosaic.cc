#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"
#include "time.h"

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
	array<Graphics> pages;
	const float inchMM = 25.4;
	const vec2 pageSizeMM = vec2(400, 300); //vec2(1140,760);
	Mosaic(float inchPx = 0) {
		if(!inchPx) inchPx = min(1680/pageSizeMM.x, 1050/pageSizeMM.y)*inchMM;
		const vec2 pageSize = pageSizeMM * (inchPx/inchMM);
		Time total, load, blur; total.start();
		array<String> images = Folder(".").list(Files);
		images.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
		array<array<array<Image>>> pages;
		if(existsFile(name, Folder("def"))) { // -- Parses page definitions
			for(TextData s= readFile(name, Folder("def")); s;) {
				array<array<Image>> page;
				do {
					array<Image> row;
					for(string name; (name = s.whileNo(" \n"));) {
						string file = [&images](string name) {
							for(string file: images) if(startsWith(file, name)) return file;
							error("No such image"_, name, "in", images);
						}(name);
						load.start();
						Image image = decodeImage(Map(file));
						load.stop();
						image.alpha = false;
						row.append(move(image));
						s.match(' ');
					}
					assert_(row);
					page.append(move(row));
					if(!s) break;
					s.skip('\n');
				} while(s && !s.match('\n')); // Blank line breaks page
				pages.append(move(page));
			}
		} else { // Loads all images
			array<array<Image>> page;
			int X = round(sqrt(float(images.size))), Y = X;
			for(int y: range(Y)) {
					array<Image> row;
					for(int x: range(X)) {
						if(y*X+x >= int(images.size)) break;
						string file = images[y*X+x];
						load.start();
						Image image = decodeImage(Map(file));
						load.stop();
						image.alpha = false;
						row.append(move(image));
					}
					assert_(row);
					page.append(move(row));
			}
			pages.append(move(page));
		}
		for(auto& images: pages) {
			// -- Layout images
			const float W = pageSize.x, H = pageSize.y; // Container size
			const int m = images.size; // Row count
			buffer<int> n = apply(images, [](ref<Image> row){ return int(row.size); }); // Columns count
			buffer<buffer<float>> a = apply(images, [](ref<Image> row){ return apply(row, [](const Image& image){ return (float)image.width/image.height; }); });  // Elements aspect ratios
			buffer<float> A = apply(a, sum<float>);  // Row aspect ratio: Aᵢ = Σj aᵢⱼ
			 // FIXME: (X, Y, h) solution is not guaranteed to be positive. Need to optimize under constraint
			//float alpha = 0;
			float alpha = 1./4;
			//float alpha = 1./2;
			//float alpha = pageSize.y / pageSize.x;
			const bool outer = true;
			float fX = (W * sum(apply(A, [](float A){ return 1/A; })) - H) / (sum(apply(m, [&](size_t i){ return (n[i]+(outer?1:-1))/A[i]; })) - alpha*(m+(outer?1:-1))); // Border: X = ( W Σᵢ 1/Aᵢ - H) / (Σᵢ (nᵢ+1)/Aᵢ - alpha*m+1) [Y=alpha*X]
			float fY = alpha*fX;
			buffer<float> h = apply(m, [&](size_t i){ return (W - (n[i]+(outer?1:-1))*fX)/A[i]; }); // Row height: hᵢ = (W - (nᵢ+1)X) / Aᵢ
			buffer<buffer<float>> w = apply(m, [&](size_t i){ return apply(n[i], [&](size_t j){ return a[i][j] * h[i]; }); }); // Elements width wᵢⱼ = aᵢⱼ hᵢ
			log("W",W,"H",H, "X", fX, "Y", fY, "h", h, "w", w);
			Graphics& page = this->pages.append();
			if(1) {
				page.bounds = Rect(round(pageSize));
				ImageF target(int2(page.bounds.size()));
				/*if(target.Ref::size < 8*1024*1024)*/ target.clear(float4_1(0)); // Assumes larger allocation are clear pages
				{ // -- Copies source images to target mosaic
					float y0 = outer * fY;
					for(int i : range(m)) {
						float x0 = outer * fX;
						for(size_t j: range(n[i])) {
							int ix0 = round(x0), iy0 = round(y0);
							int iw0 = ix0-round(x0-fX), ih0 = iy0-round(y0-fY); // Previous border sizes
							float x1 = x0+w[i][j], y1 = y0+h[i];
							int ix1 = round(x1), iy1 = round(y1);
							int iw1 = round(x1+fX)-ix1, ih1 = round(y1+fY)-iy1; // Next border sizes
							assert_(iw0 > 1 && ih0 > 1 && iw1 > 1 && ih1 > 1);
							int2 size(ix1-ix0, iy1-iy0);
							ImageF source (size);
							Image iSource = resize(size, images[i][j]); // TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
							parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
								extern float sRGB_reverse[0x100];
								for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]], 1};
							});
							// TODO: background (=white|DC) transition, pointer index arithmetic optimization
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
							x0 += w[i][j] + fX;
						}
						y0 += h[i] + fY;
					}
					assert_(round(y0) == round(H));
				}
				// -- Transitions exterior borders to total mean color
				assert_(fX > 1 && fY > 1);
				const int iX = floor(fX);
				const int iY = floor(fY);
				float4 mean = ::mean(target, iX, iY, target.size.x-iX, target.size.y-iY);
				// White vertical sides
				parallel_chunk(target.size.y-iY-iY, [&](uint, int Y0, int DY) {
					for(int y: range(iY+Y0, iY+Y0+DY)) {
						for(int x: range(iX)) {
							float4 w = float4_1(float(fX-1-x) / float(fX-1)) * mean;;
							target(x, y) += w;
							target(target.size.x-1-x, y) += w;
						}
					}
				});
				// White horizontal sides
				parallel_chunk(iY, [&](uint, int Y0, int DY) {
					for(int y: range(Y0, Y0+DY)) {
						for(int x: range(iX, target.size.x-iX)) {
							float4 w = float4_1(float(fY-1-y) / float(fY-1)) * mean;
							target(x, y) += w;
							target(x, target.size.y-1-y) += w;
						}
					}
				});
				// White corners
				parallel_chunk(iY, [&](uint, int Y0, int DY) {
					for(int y: range(Y0, Y0+DY)) {
						for(int x: range(iX)) {
							float xw = x / float(fX-1);
							float yw = y / float(fY-1);
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
				{ // -- Copies source images over blur background
					float y0 = fY;
					for(int i : range(m)) {
						float x0 = fX;
						for(size_t j: range(n[i])) {
							int ix0 = round(x0), iy0 = round(y0);
							float x1 = x0+w[i][j], y1 = y0+h[i];
							int ix1 = round(x1), iy1 = round(y1);
							int2 size(ix1-ix0, iy1-iy0);
							// TODO: pointer index arithmetic optimization
							parallel_chunk(size.y, [&](uint, int Y0, int DY) {
								for(int y: range(Y0, Y0+DY)) for(int x: range(size.x)) blur(ix0+x, iy0+y) = target(ix0+x, iy0+y);
							});
							x0 += w[i][j] + fX;
						}
						y0 += h[i] + fY;
					}
					target = move(blur);
				}
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
			} else { // -- No background
				page.bounds = Rect(pageSize);
				float y = outer * fY;
				for(size_t i: range(m)) {
					float x = outer * fX;
					for(size_t j: range(n[i])) {
						assert_(w[i][j] > 0 && h[i] > 0, w[i][j], h[i]);
						log(x, y, w[i][j], h[i], images[i][j].size);
						page.blits.append(vec2(x,y), vec2(w[i][j], h[i]), move(images[i][j]));
						x += w[i][j] + fX;
					}
					//assert_(round(x) == round(W), x, W);
					y += h[i] + fY;
				}
				//assert_(round(y) == round(H), y, H, fX, fY);
			}
		}
		log(total, load, blur);
	}
};

struct MosaicPreview : Mosaic, Application {
	Scroll<HList<GraphicsWidget>> pages {apply(Mosaic::pages, [](Graphics& o) { return GraphicsWidget(move(o)); })};
	Window window {&pages, int2(round(pages[0].bounds.size())), [this](){return unsafeRef(name);}};
};
registerApplication(MosaicPreview);

struct MosaicExport : Mosaic, Application {
	MosaicExport() : Mosaic(300) {
		//writeFile(name+".pdf"_, toPDF(pageSize, pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), Folder("out", currentWorkingDirectory(), true), true);
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(pages[0].bounds.size())), pages[0])), Folder("out", currentWorkingDirectory(), true), true);
	}
};
registerApplication(MosaicExport, export);
