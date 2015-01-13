#include "window.h"
#include "layout.h"
#include "interface.h"
#include "pdf.h"
#include "jpeg.h"
#include "render.h"
#include "jpeg-encoder.h"

struct Mosaic {
	// Name
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
	// Page
	static constexpr float inchMM = 25.4, inchPx = 90;
	const vec2 pageSize = vec2(400,300)/*mm*/ * (inchPx/inchMM);
	array<Graphics> pages;
	Mosaic() {
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
		for(auto& images: pages) {
			const float W = pageSize.x, H = pageSize.y; // Container size
			const int m = images.size; // Row count
			buffer<int> n = apply(images, [](ref<Image> row){ return int(row.size); }); // Columns count
			buffer<buffer<float>> a = apply(images, [](ref<Image> row){ return apply(row, [](const Image& image){ return (float)image.width/image.height; }); });  // Elements aspect ratios
			buffer<float> A = apply(a, sum<float>);  // Row aspect ratio: Aᵢ = Σj aᵢⱼ
			float alpha = 1./2;
			float X = (W * sum(apply(A, [](float A){ return 1/A; })) - H) / (sum(apply(m, [&](size_t i){ return (n[i]+1)/A[i]; })) - alpha*(m+1)); // Border: X = ( W Σᵢ 1/Aᵢ - H) / (Σᵢ (nᵢ+1)/Aᵢ - alpha*m+1) [Y=alpha*X]
			float Y = alpha*X;
			buffer<float> h = apply(m, [&](size_t i){ return (W - (n[i]+1)*X)/A[i]; }); // Row height: hᵢ = (W - (nᵢ+1)X) / Aᵢ
			/*while(X < 0 || Y < 0 || Y < X/2) {
				X++;
				h = apply(m, [&](size_t i){ return (W - (n[i]+1)*X)/A[i]; }); // Update row height to respect W = (nᵢ+1)X + Aᵢhᵢ => hᵢ = (W - (nᵢ+1)X) / Aᵢ
				Y = (H - sum(h)) / (m+1); // Update Y to respect constraint H = (m+1)Y + Σᵢhᵢ
			}*/
			buffer<buffer<float>> w = apply(m, [&](size_t i){ return apply(n[i], [&](size_t j){ return a[i][j] * h[i]; }); });  // Elements width wᵢⱼ = aᵢⱼ hᵢ
			log("W",W,"H",H, "X", X, "Y", Y, "h", h, "w", w);
			{Graphics& page = this->pages.append();
			   page.bounds = Rect(pageSize);
			   float y = Y;
			   for(size_t i: range(m)) {
				   float x = X;
				   for(size_t j: range(n[i])) {
					   assert_(w[i][j] > 0 && h[i] > 0, w[i][j], h[i]);
					   page.blits.append(vec2(x,y), vec2(w[i][j], h[i]), move(images[i][j]));
					   x += w[i][j] + X;
				   }
				   assert_(round(x) == round(W), x, W);
				   y += h[i] + Y;
			   }
			   assert_(round(y) == round(H), y, H);
			}
		}
	}
};

struct MosaicPreview : Mosaic, Application {
	Scroll<HList<GraphicsWidget>> pages {apply(Mosaic::pages, [](Graphics& o) { return GraphicsWidget(move(o)); })};
	Window window {&pages, int2(pageSize), [this](){return unsafeRef(name);}};
};
registerApplication(MosaicPreview);

struct MosaicExport : Mosaic, Application {
	MosaicExport() {
		writeFile(name+".pdf"_, toPDF(pageSize, pages, 72/*PostScript point per inch*/ / inchPx /*px/inch*/), currentWorkingDirectory(), true);
		writeFile(name+".jpg"_, encodeJPEG(render(int2(round(pageSize*float(300./90))), pages[0], 0, 300./90)), currentWorkingDirectory(), true);
	}
};
registerApplication(MosaicExport, export);
