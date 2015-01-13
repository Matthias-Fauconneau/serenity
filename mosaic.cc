#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"

static int mirror(int x, int w) {
	if(x < 0) return -x;
	if(x >= w) return w-1-(x-w);
	return x;
}

typedef float float4 __attribute((__vector_size__ (16)));
inline float4 constexpr float4_1(float f) { return (float4){f,f,f,f}; }
inline float4 float4_4(byte4 v) { return (float4){float(v.b),float(v.g),float(v.r),float(v.a)}; }

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

	explicit operator bool() const { return data && width && height; }
	inline float4& operator()(size_t x, size_t y) const {assert(x<width && y<height, x, y); return at(y*stride+x); }
	inline float4& operator()(int2 p) const { return operator()(p.x, p.y); }

	union {
		int2 size = 0;
		struct { uint width, height; };
	};
	size_t stride = 0;
};

struct Mosaic {
	// Name
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
	array<Graphics> pages;
	Mosaic(const float inchPx = 90/2) {
		array<String> images = Folder(".").list(Files);
		images.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
		// -- Parses page definitions
		array<array<array<Image>>> pages;
		for(TextData s= readFile(name); s;) {
			array<array<Image>> page;
			do {
				array<Image> row;
				for(string name; (name = s.whileNo(" \n"));) {
					string file = [&images](string name) {
						for(string file: images) if(startsWith(file, name)) return file;
						error("No such image"_, name, "in", images);
					}(name);
					Image image = decodeImage(readFile(file));
					image.alpha = false;
					assert_(!image.alpha);
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
		const float inchMM = 25.4;
		const vec2 pageSize = vec2(400,300)/*mm*/ * (inchPx/inchMM);
		for(auto& images: pages) {
			// -- Layout images
			const float W = pageSize.x, H = pageSize.y; // Container size
			const int m = images.size; // Row count
			buffer<int> n = apply(images, [](ref<Image> row){ return int(row.size); }); // Columns count
			buffer<buffer<float>> a = apply(images, [](ref<Image> row){ return apply(row, [](const Image& image){ return (float)image.width/image.height; }); });  // Elements aspect ratios
			buffer<float> A = apply(a, sum<float>);  // Row aspect ratio: Aᵢ = Σj aᵢⱼ
			float alpha = 1./2; // FIXME: Optimize under constraint
			float X = (W * sum(apply(A, [](float A){ return 1/A; })) - H) / (sum(apply(m, [&](size_t i){ return (n[i]+1)/A[i]; })) - alpha*(m+1)); // Border: X = ( W Σᵢ 1/Aᵢ - H) / (Σᵢ (nᵢ+1)/Aᵢ - alpha*m+1) [Y=alpha*X]
			float Y = alpha*X;
			buffer<float> h = apply(m, [&](size_t i){ return (W - (n[i]+1)*X)/A[i]; }); // Row height: hᵢ = (W - (nᵢ+1)X) / Aᵢ
			buffer<buffer<float>> w = apply(m, [&](size_t i){ return apply(n[i], [&](size_t j){ return a[i][j] * h[i]; }); }); // Elements width wᵢⱼ = aᵢⱼ hᵢ
			//log("W",W,"H",H, "X", X, "Y", Y, "h", h, "w", w);
			Graphics& page = this->pages.append();
#if 1     // -- Extend images over background
			page.bounds = Rect(round(pageSize));
			ImageF target(int2(page.bounds.size()));
			target.clear(float4{0,0,0,0});
			// TODO: parallel, [gaussian], [factorize]
			// White Top
			for(int y: range(Y)) {
				for(int x: range(X)) {
					float xw = x / float(X-1);
					float yw = y / float(Y-1);
					float w = /*(xw*yw) +*/ ((1-xw)*yw) + (xw*(1-yw)) + ((1-xw)*(1-yw));
					target(x, y) += float4_1(w);
				}
				for(int x: range(X, target.size.x-(X-1))) {
					float w = float(Y-1-y) / float(Y-1);
					target(x, y) += float4_1(w);
				}
				for(int x: range(X)) {
					float xw = x / float(X-1);
					float yw = y / float(Y-1);
					float w = /*(xw*yw) +*/ ((1-xw)*yw) + (xw*(1-yw)) + ((1-xw)*(1-yw));
					target(target.size.x-1-x, y) += float4_1(w);
				}
			}
			// White Left
			for(int y: range(Y, target.size.y-(Y-1))) {
				for(int x: range(X)) {
					float w = float(X-1-x) / float(X-1);
					target(x, y) += float4_1(w);
				}
			}
			const int R = min(X, Y);
			float y0 = Y;
			for(size_t i: range(m)) {
				float x0 = X;
				for(size_t j: range(n[i])) {
					int ix0 = round(x0), iy0 = round(y0);
					int iw0 = ix0-round(x0-X), ih0 = iy0-round(y0-Y); // Previous border sizes
					float x1 = x0+w[i][j], y1 = y0+h[i];
					int ix1 = round(x1), iy1 = round(y1);
					int iw1 = round(x1+X)-ix1, ih1 = round(y1+Y)-iy1; // Next border sizes
					int2 size(ix1-ix0, iy1-iy0);
					ImageF source (size);
					{Image iSource = resize(size, images[i][j]); // TODO: single pass resize and float conversion
						extern float sRGB_reverse[0x100];
						for(size_t i: range(iSource.Ref::size)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]], 1};
					}
					// Top Left
					for(int y: range(ih0)) {
						for(int x: range(iw0)) {
							float4 s = {0,0,0,0};
							int r = max(x, y);
							for(int dy: range(r+R)) for(int dx: range(r+R)) s += source(dx, dy);
							float w = float((iw0-1-x) * (ih0-1-y)) / float( (r+R) * (iw0-1) * (r+R) * (ih0-1) );
							target(ix0-x-1, iy0-y-1) += float4_1(w) * s;
						}
					}
					// Top Center
					for(int y: range(ih0)) {
						float w = float(ih0-1-y) / float( (y+R) * ((y+R) - -(y+R)) * (ih0-1));
						for(int x: range(size.x)) {
							float4 s = {0,0,0,0};
							for(int dy: range(y+R)) for(int dx: range(-(y+R), (y+R))) s += source(mirror(x+dx, size.x), dy); // TODO: factor mirror out
							target(ix0+x, iy0-y-1) += float4_1(w) * s;
						}
					}
					// Top Right
					for(int y: range(ih0)) {
						for(int x: range(iw1)) {
							float4 s = {0,0,0,0};
							int r = max(x, y);
							for(int dy: range(r+R)) for(int dx: range(r+R)) s += source(size.x-1-dx, dy);
							float w = float((iw1-1-x) * (ih0-1-y)) / float( (r+R) * (iw1-1) * (r+R) * (ih0-1) );
							target(ix1+x, iy0-y-1) += float4_1(w) * s;
						}
					}
					// Center
					for(int y: range(size.y)) {
						// Center Left
						for(int x: range(iw0)) {
							float4 s = {0,0,0,0};
							for(int dy: range(-(x+R), (x+R))) for(int dx: range(x+R)) s += source(dx, mirror(y+dy, size.y));
							float w = float(iw0-1-x) / float( (x+R) * ((x+R) - -(x+R)) * (iw0-1));
							target(ix0-x-1, iy0+y) += float4_1(w) * s;
						}
						// Center Center
						for(int x: range(size.x)) target(ix0+x, iy0+y) = source(x, y);
						// Center Right
						for(int x: range(iw1)) {
							float4 s = {0,0,0,0};
							for(int dy: range(-(x+R), (x+R))) for(int dx: range(x+R)) s += source(size.x-1-dx, mirror(y+dy, size.y));
							float w = float(iw1-1-x) / float( (x+R) * ((x+R) - -(x+R)) * (iw1-1));
							target(ix1+x, iy0+y) += float4_1(w) * s;
						}
					}
					// Bottom Left
					for(int y: range(ih1)) {
						for(int x: range(iw0)) {
							float4 s = {0,0,0,0};
							int r = max(x, y);
							for(int dy: range(r+R)) for(int dx: range(r+R)) s += source(dx, size.y-1-dy);
							float w = float((iw0-1-x) * (ih0-1-y)) / float( (r+R) * (iw0-1) * (r+R) * (ih0-1) );
							target(ix0-x-1, iy1+y) += float4_1(w) * s;
						}
					}
					// Bottom Center
					for(int y: range(ih1)) {
						for(int x: range(size.x)) {
							float4 s = {0,0,0,0};
							for(int dy: range(y+R)) for(int dx: range(-(y+R), (y+R))) s += source(mirror(x+dx, size.x), size.y-1-dy);
							float w = float(ih1-1-y) / float( (y+R) * ((y+R) - -(y+R)) * (ih1-1));
							target(ix0+x, iy1+y) += float4_1(w) * s;
						}
					}
					// Bottom Right
					for(int y: range(ih1)) {
						for(int x: range(iw1)) {
							float4 s = {0,0,0,0};
							int r = max(x, y);
							for(int dy: range(r+R)) for(int dx: range(r+R)) s += source(size.x-1-dx, size.y-1-dy);
							float w = float((iw1-1-x) * (ih0-1-y)) / float( (r+R) * (iw1-1) * (r+R) * (ih0-1) );
							target(ix1+x, iy1+y) += float4_1(w) * s;
						}
					}
					x0 += w[i][j] + X;
				}
				y0 += h[i] + Y;
			}
			// White Right
			for(int y: range(Y, target.size.y-(Y-1))) {
				for(int x: range(X)) {
					float w = float(X-1-x) / float(X-1);
					target(target.size.x-1-x, y) += float4_1(w);
				}
			}
			// White Bottom
			for(int y: range(Y)) {
				for(int x: range(X)) {
					float xw = x / float(X-1);
					float yw = y / float(Y-1);
					float w = /*(xw*yw) +*/ ((1-xw)*yw) + (xw*(1-yw)) + ((1-xw)*(1-yw));
					target(x, target.size.y-1-y) += float4_1(w);
				}
				for(int x: range(X, target.size.x-(X-1))) {
					float w = float(Y-1-y) / float(Y-1);
					target(x, target.size.y-1-y) += float4_1(w);
				}
				for(int x: range(X)) {
					float xw = x / float(X-1);
					float yw = y / float(Y-1);
					float w = /*(xw*yw) +*/ ((1-xw)*yw) + (xw*(1-yw)) + ((1-xw)*(1-yw));
					target(target.size.x-1-x, target.size.y-1-y) += float4_1(w);
				}
			}
			Image iTarget (target.size);
			assert_(target.Ref::size == iTarget.Ref::size);
			for(size_t i: range(target.Ref::size)) {
				extern uint8 sRGB_forward[0x1000];
				iTarget[i] = byte4(sRGB_forward[int(round(0xFFF*min(1.f, target[i][0])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][1])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][2])))]);
			}
			page.blits.append(0, page.bounds.size(), move(iTarget));
#else   // -- No background
			page.bounds = Rect(pageSize);
			Graphics& page = this->pages.append();
			page.bounds = Rect(pageSize);
			float y0 = Y;
			for(size_t i: range(m)) {
				float x = X;
				for(size_t j: range(n[i])) {
					assert_(w[i][j] > 0 && h[i] > 0, w[i][j], h[i]);
					page.blits.append(vec2(x,y0), vec2(w[i][j], h[i]), move(images[i][j]));
					x += w[i][j] + X;
				}
				assert_(round(x) == round(W), x, W);
				y0 += h[i] + Y;
			}
#endif
			assert_(round(y0) == round(H));
		}
	}
};

struct MosaicPreview : Mosaic, Application {
	Scroll<HList<GraphicsWidget>> pages {apply(Mosaic::pages, [](Graphics& o) { return GraphicsWidget(move(o)); })};
	Window window {&pages, int2(round(pages[0].bounds.size())), [this](){return unsafeRef(name);}};
};
registerApplication(MosaicPreview);

struct MosaicExport : Mosaic, Application {
	MosaicExport() : Mosaic(300) {
		//writeFile(name+".pdf"_, toPDF(pageSize, pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), currentWorkingDirectory(), true);
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(pages[0].bounds.size())), pages[0])), currentWorkingDirectory(), true);
	}
};
registerApplication(MosaicExport, export);
