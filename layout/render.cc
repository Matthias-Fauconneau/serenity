#include "render.h"
#include "time.h"
#include "serialization.h"
#include "parallel.h"

LayoutRender::LayoutRender(Layout&& _this, const float _mmPx, const float _inchPx) : Layout(move(_this)) {
	assert_((_mmPx>0) ^ (_inchPx>0));
	const float mmPx = _mmPx ? _mmPx : _inchPx/inchMM;
	assert_(mmPx);

	// -- Decodes sRGB8 images
	buffer<Image> sources = apply(elements, [=](const Element& e) { return e.source(); });

	if(0) {  // -- Evaluates resolution
		const float inchPx = _inchPx ? _inchPx : _mmPx*inchMM;
		assert_(inchPx);
		float minScale = inf, maxScale = 0;
		for(size_t elementIndex: range(elements.size)) {
			if(!sources[elementIndex]) continue;
			float scale = sources[elementIndex].size.x / elements[elementIndex]->size(mmPx).x;
			minScale = min(minScale, scale);
			maxScale = max(maxScale, scale);
		}
		log("@"+str(mmPx)+"ppmm "+str(inchPx)+"ppi: \t "+
			"min: "+str(minScale)+"x "+str(minScale*mmPx)+"ppmm "+str(minScale*inchPx)+"ppi \t"+
			"max: "+str(maxScale)+"x "+str(maxScale*mmPx)+"ppmm "+str(maxScale*inchPx)+"ppi");
	}

	// -- Evaluates each elements dominant color (or use user argument overrides)
	buffer<v4sf> elementColors = apply(sources.size, [&](const size_t elementIndex) {
		const Image& source = sources[elementIndex];
		int hueHistogram[0x100] = {}; mref<int>(hueHistogram).clear(0); // 1½K: 32bit / 0xFF -> 4K² images
		int intensityHistogram[0x100] = {}; mref<int>(intensityHistogram).clear(0);
		int chromaHistogram[0x100] = {}; mref<int>(chromaHistogram).clear(0);
		for(byte4 c: source) {
			const int B = c.b, G = c.g, R = c.r;
			const int M = max(max(B, G), R);
			const int m = min(min(B, G), R);
			const int C = M - m;
			const int I = (B+G+R)/3;
			intensityHistogram[I]++;
			chromaHistogram[C]++;
			if(C) {
				int H;
				if(M==R) H = ((G-B)*43/C+0x100)%0x100; // 5-6 0-1
				else if(M==G) H = (B-R)*43/C+85; // 1-3
				else if(M==B) H = (R-G)*43/C+171; // 3-5
				else ::error(B, G, R);
				hueHistogram[H] += C;
			}
		}
		int H = argmax(ref<int>(hueHistogram));
		if(arguments.contains("hue"_)) H = parse<float>(arguments.at("hue"_)) * 0xFF;
		int C = argmax(ref<int>(chromaHistogram));
		if(C && arguments.contains("chroma"_)) C = parse<float>(arguments.at("chroma"_)) * 0xFF;
		int X = C * (0xFF - abs((H%85)*6 - 0xFF)) / 0xFF;
		int I = argmax(ref<int>(intensityHistogram));
		if(arguments.contains("intensity"_)) I = parse<float>(arguments.at("intensity"_)) * 0xFF;
		assert_(I >= 0, I, arguments);
		int R,G,B;
		if(H < 43) R=C, G=X, B=0;
		else if(H < 85) R=X, G=C, B=0;
		else if(H < 128) R=0, G=C, B=X;
		else if(H < 171) R=0, G=X, B=C;
		else if(H < 213) R=X, G=0, B=C;
		else if(H < 256) R=C, G=0, B=X;
		else ::error(H);
		int m = I; //max(0, I - (R+G+B)/3);
		/*// Limits intensity within sRGB
		m = min(m, 0xFF-R);
		m = min(m, 0xFF-G);
		m = min(m, 0xFF-B);*/
		//log("H", H, "C", C, "X", X, "R", R, "G", G, "B", B, "I", I, "m", m, "m+B", m+B, "m+G", m+G, "m+R", m+R);
		extern float sRGB_reverse[0x100];
		return v4sf{sRGB_reverse[min(0xFF,m+B)], sRGB_reverse[min(0xFF,m+G)], sRGB_reverse[min(0xFF,m+R)], 0};
		//return v4sf{sRGB_reverse[m+B], sRGB_reverse[m+G], sRGB_reverse[m+R], 0};
	});

#define px(x) round((x)*mmPx)
	int2 size = int2(px(this->size));
	ImageF background(size);
	background.clear(float4(0));

	// -- Draws elements backgrounds and transitions between elements
	for(size_t elementIndex: range(elements.size)) {
		const Element& element = elements[elementIndex];
		if(!element.root) continue;
		v4sf elementColor = elementColors[elementIndex];
		int2 index = element.index;
		float xL = rowMargins[index.y]+sum(columnWidths.slice(0, index.x));
		int xL0 = px(index.x ? xL-columnSpaces[index.x-1] : 0);
		int xL1 = px(xL + columnSpaces[index.x]);
		float xR = xL+sum(columnWidths.slice(index.x, element.cellCount.x));
		int xR0 = px(xR - columnSpaces[index.x+element.cellCount.x-1]);
		int xR1 = px(size_t(index.x+element.cellCount.x)<table.columnCount ? xR + columnSpaces[index.x+element.cellCount.x] : this->size.x);

		float yT = columnMargins[index.x]+sum(rowHeights.slice(0, index.y));
		int yT0 = px(index.y ? yT-rowSpaces[index.y-1] : 0);
		int yT1 = px(yT + rowSpaces[index.y]);
		float yB = yT+sum(rowHeights.slice(index.y,element.cellCount.y));
		int yB0 = px(yB - rowSpaces[index.y+element.cellCount.y-1]);
		int yB1 = px(size_t(index.y+element.cellCount.y)<table.rowCount ? yB + rowSpaces[index.y+element.cellCount.y] : this->size.y);

		// Top
		for(int y: range(yT0, yT1)) {
			mref<v4sf> line = background.slice(y*background.stride, background.width);
			float wy = index.y ? (y-yT0)/float(yT1-yT0) : 1;
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = index.x ? (x-xL0)/float(xL1-xL0) : 1;
				line[x] += float4(wx*wy) * elementColor;
			}
			// Center
			for(int x: range(xL1, xR0)) {
				line[x] += float4(wy) * elementColor;
			}
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = size_t(index.x+element.cellCount.x)<table.columnCount ? (xR1-x)/float(xR1-xR0) : 1;
				line[x] += float4(wx*wy) * elementColor;
			}
		}
		// -- Center
		for(int y: range(yT1, yB0)) {
			mref<v4sf> line = background.slice(y*background.stride, background.width);
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = index.x ? (x-xL0)/float(xL1-xL0) : 1;
				line[x] += float4(wx) * elementColor;
			}
			// Center
			for(int x: range(xL1, xR0)) {
				line[x] = elementColor;
			}
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = size_t(index.x+element.cellCount.x)<table.columnCount ? (xR1-x)/float(xR1-xR0) : 1;
				line[x] += float4(wx) * elementColor;
			}
		}
		// Bottom
		for(int y: range(yB0, yB1)) {
			mref<v4sf> line = background.slice(y*background.stride, background.width);
			float wy = size_t(index.y+element.cellCount.y)<table.rowCount ? (yB1-y)/float(yB1-yB0) : 1;
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = index.x ? (x-xL0)/float(xL1-xL0) : 1;
				line[x] += float4(wx*wy) * elementColor;
			}
			// Center
			for(int x: range(xL1, xR0)) {
				line[x] += float4(wy) * elementColor;
			}
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = size_t(index.x+element.cellCount.x)<table.columnCount ? (xR1-x)/float(xR1-xR0) : 1;
				line[x] += float4(wx*wy) * elementColor;
			}
		}
	}

	// -- Renders elements
	buffer<ImageF> renders = apply(elements, [=](const Element& e) { return e.render(mmPx); });

	auto compose = [&]{
		ImageF target = copy(background);
		int2 feather = parse<float>(arguments.value("feather","1"_))*mmPx;
		// -- Composes unfeathered elements
		for(size_t elementIndex: range(elements.size)) {
			const Element& element = elements[elementIndex];
			const ImageF& source = renders[elementIndex];
			if(!source.alpha && feather) continue;

			int x0 = px(element.min.x);
			int x1 = px(element.max.x);
			int y0 = px(element.min.y);
			int y1 = px(element.max.y);
			parallel_chunk(y0, y1, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					mref<v4sf> targetLine = target.slice(y*target.stride, target.width);
					mref<v4sf> sourceLine = source.slice((y-y0)*source.stride, source.width);
					if(source.alpha) { // Applies mask
						if(arguments.contains("color")) {
							v4sf color = float4(parse<float>(arguments.at("color"_)));
							for(int x: range(x0, x1)) targetLine[x] = mix(targetLine[x], color, sourceLine[x-x0][3]);
						} else {
							for(int x: range(x0, x1)) targetLine[x] = mix(targetLine[x], sourceLine[x-x0], sourceLine[x-x0][3]);
						}
					} else { // Copies image
						for(int x: range(x0, x1)) targetLine[x] = sourceLine[x-x0];
					}
				}
			});
		}
		// -- Composes feathered elements
		if(feather) for(size_t elementIndex: range(elements.size)) {
			const Element& element = elements[elementIndex];
			const ImageF& source = renders[elementIndex];
			if(source.alpha) continue;
			int x0 = px(element.min.x);
			int x1 = px(element.max.x);
			int y0 = px(element.min.y);
			int y1 = px(element.max.y);
			int2 size = int2(x1-x0, y1-y0);
			assert_(source.size == size);

			for(int y: range(feather.y)) { // Top
				mref<v4sf> sourceLine = source.slice((y)*source.stride, source.width);
				mref<v4sf> backgroundLine = background.slice((y0+y)*background.stride, background.width);
				mref<v4sf> targetLine = target.slice((y0+y)*target.stride, target.width);
				for(int x: range(feather.x)) // Left
					targetLine[x0+x] = mix(backgroundLine[x0+x], sourceLine[x], (x/float(feather.x))*(y/float(feather.y)));
				for(int x: range(feather.x, size.x-feather.x)) // Center
					targetLine[x0+x] = mix(backgroundLine[x0+x], sourceLine[x], (y/float(feather.y)));
				for(int x: range(feather.x)) // Right
					targetLine[x1-1-x] = mix(backgroundLine[x1-1-x], sourceLine[size.x-1-x], (x/float(feather.x))*(y/float(feather.y)));
			}
			parallel_chunk(feather.y, size.y-feather.y, [&](uint, int Y0, int DY) { // Center
				for(int y: range(Y0, Y0+DY)) {
					mref<v4sf> sourceLine = source.slice((y)*source.stride, source.width);
					mref<v4sf> backgroundLine = background.slice((y0+y)*background.stride, background.width);
					mref<v4sf> targetLine = target.slice((y0+y)*target.stride, target.width);
					for(int x: range(feather.x)) targetLine[x0+x] = mix(backgroundLine[x0+x], sourceLine[x], (x/float(feather.x))); // Left
					for(int x: range(feather.x, size.x-feather.x)) targetLine[x0+x] = sourceLine[x];
					for(int x: range(feather.x)) targetLine[x1-1-x] = mix(backgroundLine[x1-1-x], sourceLine[size.x-1-x], (x/float(feather.x))); // Right
				}
			});
			for(int y: range(feather.y)) { // Bottom
				mref<v4sf> sourceLine = source.slice((size.y-1-y)*source.stride, source.width);
				mref<v4sf> backgroundLine = background.slice((y1-1-y)*background.stride, background.width);
				mref<v4sf> targetLine = target.slice((y1-1-y)*target.stride, target.width);
				for(int x: range(feather.x)) // Left
					targetLine[x0+x] = mix(backgroundLine[x0+x], sourceLine[x], (x/float(feather.x))*(y/float(feather.y)));
				for(int x: range(feather.x,size.x-feather.x)) // Center
					targetLine[x0+x] = mix(backgroundLine[x0+x], sourceLine[x], (y/float(feather.y)));
				for(int x: range(feather.x)) // Right
					targetLine[x1-1-x] = mix(backgroundLine[x1-1-x], sourceLine[size.x-1-x], (x/float(feather.x))*(y/float(feather.y)));
			}
		}
		return target;
	};
	ImageF target = compose();

	// -- Replaces background with blurred target
	if(arguments.value("blur","0"_)!="0"_) {
		// -- Large gaussian blur approximated with repeated box convolution
		ImageF blur(size);
		ImageF transpose(int2(size.y, size.x));
		const int R = min(size.x, size.y) * parse<float>(arguments.value("blur","1/8"_));
		box(transpose, target, R);
		box(blur, transpose, R);
		box(transpose, blur, R);
		box(blur, transpose, R);
		// -- Composes again over blur background
		background = move(blur);
		target = compose();
	}

	// -- Convert back to 8bit sRGB
	Image iTarget (target.size);
	assert(target.Ref::size == iTarget.Ref::size);
	parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
		extern uint8 sRGB_forward[0x1000];
		int clip = 0;
		for(size_t i: range(I0, I0+DI)) {
			int3 linear;
			for(uint c: range(3)) {
				float v = target[i][c];
				if(!(v >= 0 && v <= 1)) {
					if(v < 0) v = 0;
					else if(v > 1) v = 1;
					else v = 0; // NaN
					//if(!clip) log("Clip", v, i, c);
					clip++;
				}
				linear[c] = int(round(0xFFF*v));
			}
			iTarget[i] = byte4( sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]] );
		}
		//if(clip) log("Clip", clip);
		//assert(!clip);
	});
	this->target = move(iTarget);
}
