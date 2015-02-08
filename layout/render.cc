#include "render.h"
#include "time.h"
#include "serialization.h"
#include "parallel.h"

LayoutRender::LayoutRender(Layout&& _this, const float _mmPx, const float _inchPx) : Layout(move(_this)) {
	assert_((_mmPx>0) ^ (_inchPx>0));
	const float mmPx = _mmPx ? _mmPx : _inchPx/inchMM;
	assert_(mmPx);

	// -- Renders each element
	//Time loadTime;
	buffer<Image> images = apply(elements, [=](const Element& e) { return e.size(mmPx)>int2(0) ? e.image(mmPx) : Image(); });
	//log(loadTime);

	if(0) {  // -- Evaluates resolution
		const float inchPx = _inchPx ? _inchPx : _mmPx*inchMM;
		assert_(inchPx);
		float minScale = inf, maxScale = 0;
		for(size_t elementIndex: range(elements.size)) {
			float scale = images[elementIndex].size.x / elements[elementIndex]->size(mmPx).x;
			minScale = min(minScale, scale);
			maxScale = max(maxScale, scale);
		}
		log("@"+str(mmPx)+"ppmm "+str(inchPx)+"ppi: \t "+
			"min: "+str(minScale)+"x "+str(minScale*mmPx)+"ppmm "+str(minScale*inchPx)+"ppi \t"+
			"max: "+str(maxScale)+"x "+str(maxScale*mmPx)+"ppmm "+str(maxScale*inchPx)+"ppi");
	}

	// -- Evaluates each elements dominant color (or use user argument overrides)
	buffer<v4sf> elementColors = apply(images.size, [&](const size_t elementIndex) {
		const Image& iSource = images[elementIndex];
		int hueHistogram[0x100] = {}; mref<int>(hueHistogram).clear(0); // 1½K: 32bit / 0xFF -> 4K² images
		int intensityHistogram[0x100] = {}; mref<int>(intensityHistogram).clear(0);
		for(byte4 c: iSource) {
			const int B = c.b, G = c.g, R = c.r;
			const int M = max(max(B, G), R);
			const int m = min(min(B, G), R);
			const int C = M - m;
			const int I = (B+G+R)/3;
			intensityHistogram[I]++;
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
		int C = parse<float>(arguments.value("chroma"_, "1/4"_)) * 0xFF;
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
	ImageF target(size);
	target.clear(float4(0));

	// -- Fills background color transition on exterior borders
	v4sf backgroundColor = ::mean(elementColors);
	// Left vertical side
	for(int row: range(table.rowCount)) {
		float yT = columnMargins[0]+sum(rowHeights.slice(0, row));
		float yB = yT+rowHeights[row];
		float dx = rowMargins[row];
		for(int y: range(px(yT), px(yB))) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			for(int x: range(px(dx))) {
				v4sf c = float4(1-x/px(dx)) * backgroundColor;
				line[x] += c;
			}
		}
	}
	// Right vertical side
	for(int row: range(table.rowCount)) {
		float yT = columnMargins.last()+sum(rowHeights.slice(0, row));
		float yB = yT+rowHeights[row];
		float dx = rowMargins[row];
		for(int y: range(px(yT), px(yB))) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			for(int x: range(px(dx))) {
				v4sf c = float4(1-x/px(dx)) * backgroundColor;
				line[size.x-1-x] += c;
			}
		}
	}
	// Top horizontal side
	for(int column: range(table.columnCount)) {
		float xL = rowMargins[0]+sum(columnWidths.slice(0, column));
		float xR = xL+columnWidths[column];
		float dy = columnMargins[column];
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			v4sf c = float4(1-y/px(dy)) * backgroundColor;
			for(int x: range(px(xL), px(xR))) {
				line[x] += c;
			}
		}
	}
	// Bottom horizontal side
	for(int column: range(table.columnCount)) {
		float xL = rowMargins.last()+sum(columnWidths.slice(0, column));
		float xR = xL+columnWidths[column];
		float dy = columnMargins[column];
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice((size.y-1-y)*target.stride, target.width);
			v4sf c = float4(1-y/px(dy)) * backgroundColor;
			for(int x: range(px(xL), px(xR))) {
				line[x] += c;
			}
		}
	}
	{  // Top Left Corner
		float dx = rowMargins[0];
		float dy = columnMargins[0];
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			for(int x: range(px(dx))) {
				v4sf c = float4(1-(x/px(dx))*(y/px(dy))) * backgroundColor;
				line[x] += c;
			}
		}
	}
	{  // Top Right Corner
		float dx = size.x - px(rowMargins[0]+sum(columnWidths));
		float dy = columnMargins.last();
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			for(int x: range((dx))) {
				v4sf c = float4(1-(x/(dx))*(y/px(dy))) * backgroundColor;
				line[size.x-1-x] += c;
			}
		}
	}
	{  // Bottom Left Corner
		float dx = rowMargins.last();
		float dy = columnMargins[0];
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice((size.y-1-y)*target.stride, target.width);
			for(int x: range(px(dx))) {
				v4sf c = float4(1-((x/px(dx))*(y/px(dy)))) * backgroundColor;
				line[x] += c;
			}
		}
	}
	{  // Bottom Right Corner
		float dx = size.x - px(rowMargins.last()+sum(columnWidths));
		float dy = columnMargins.last();
		for(int y: range(px(dy))) {
			mref<v4sf> line = target.slice((size.y-1-y)*target.stride, target.width);
			for(int x: range(dx)) {
				v4sf c = float4(1-((x/(dx))*(y/px(dy)))) * backgroundColor;
				line[size.x-1-x] += c;
			}
		}
	}

	// -- Copies elements
	for(size_t elementIndex: range(elements.size)) {
		const Element& element = elements[elementIndex];
		int2 size = element.size(mmPx);
		const Image& image = images[elementIndex];
		//if(size == image.size) log(size); else log(image.size, "→", size);
		// TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
		Image iSource = size == image.size ? share(image) : resize(size, image);
		ImageF source (size);
		parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
			extern float sRGB_reverse[0x100];
			for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]],
														 image.alpha ? float(iSource[i][2])/0xFF : 1};
		});
		int x0 = px(element.min.x);
		int x1 = px(element.max.x);
		int y0 = px(element.min.y);
		int y1 = px(element.max.y);
		parallel_chunk(y0, y1, [&](uint, int Y0, int DY) {
			for(int y: range(Y0, Y0+DY)) {
				mref<v4sf> line = target.slice(y*target.stride, target.width);
				mref<v4sf> sourceLine = source.slice((y-y0)*source.stride, source.width);
				if(image.alpha) {
					for(int x: range(x0, x1)) if(sourceLine[x-x0][3]) line[x] = sourceLine[x-x0]; // Masks image
				} else {
					for(int x: range(x0, x1)) line[x] = sourceLine[x-x0]; // Copies image
				}
			}
		});
	}
	// -- Draws transitions between elements
	for(size_t elementIndex: range(elements.size)) {
		const Element& element = elements[elementIndex];
		if(!element.root) continue;
		int2 index = element.index;
		float xL = rowMargins[index.y]+sum(columnWidths.slice(0, index.x));
		int xL0 = px(index.x ? xL-columnSpaces[index.x-1] : 0);
		int xL1 = px(xL + columnSpaces[index.x]);
		int x0 = px(element.min.x);
		int x1 = px(element.max.x);
		float xR = xL+sum(columnWidths.slice(index.x, element.cellCount.x));
		int xR0 = px(xR - columnSpaces[index.x+element.cellCount.x-1]);
		int xR1 = px(size_t(index.x+element.cellCount.x)<table.columnCount ? xR + columnSpaces[index.x+element.cellCount.x] : this->size.x);
		//log("x", columnSpaces[index.x], xL0, int(px(xL)), xL1, "x0", x0, "x1", x1, xR0, int(px(xR)), xR1);

		float yT = columnMargins[index.x]+sum(rowHeights.slice(0, index.y));
		int yT0 = px(index.y ? yT-rowSpaces[index.y-1] : 0);
		int yT1 = px(yT + rowSpaces[index.y]);
		int y0 = px(element.min.y);
		int y1 = px(element.max.y);
		float yB = yT+sum(rowHeights.slice(index.y,element.cellCount.y));
		int yB0 = px(yB - rowSpaces[index.y+element.cellCount.y-1]);
		int yB1 = px(size_t(index.y+element.cellCount.y)<table.rowCount ? yB + rowSpaces[index.y+element.cellCount.y] : this->size.y);
		//log("y", rowSpaces[index.y], yT0, int(px(yT)), yT1, "y0", y0, "y1", y1, yB0, int(px(yB)), yB1);

		v4sf elementColor = elementColors[elementIndex];

		// Top
		for(int y: range(yT0, yT1)) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			float wy = (y-yT0)/float(yT1-yT0);
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = (x-xL0)/float(xL1-xL0);
				line[x] += float4(wx*wy) * elementColor;
			}
			// Center
			for(int x: range(max(xL0,xL1), min(xR0,xR1))) {
				line[x] += float4(wy) * elementColor;
			}
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = (xR1-x)/float(xR1-xR0);
				line[x] += float4(wx*wy) * elementColor;
			}
		}
		// FIXME: yT1 - y0
		// -- Center
		for(int y: range(yT1, yB0)) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = (x-xL0)/float(xL1-xL0);
				line[x] += float4(wx) * elementColor;
			}
		}
		//FIXME: xL1 - y0
		//FIXME: x1 - yB0
		for(int y: range(yT1, yB0)) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = (xR1-x)/float(xR1-xR0);
				line[x] += float4(wx) * elementColor;
			}
		}

		//FIXME: y1 - yB0
		// Bottom
		for(int y: range(yB0, yB1)) {
			mref<v4sf> line = target.slice(y*target.stride, target.width);
			float wy = (yB1-y)/float(yB1-yB0);
			// Left
			for(int x: range(xL0, xL1)) {
				float wx = (x-xL0)/float(xL1-xL0);
				line[x] += float4(wx*wy) * elementColor;
			}
			// Center
			for(int x: range(max(xL0,xL1), min(xR0,xR1))) {
				line[x] += float4(wy) * elementColor;
			}
			// Right
			for(int x: range(xR0, xR1)) {
				float wx = (xR1-x)/float(xR1-xR0);
				line[x] += float4(wx*wy) * elementColor;
			}
		}
	}

	if(arguments.value("blur","1/8"_)!="0"_) { // -- Large gaussian blur approximated with repeated box convolution
		//log("Blur");
		//Time blurTime;
		ImageF source = move(target);
		ImageF blur(source.size);
		{
			ImageF transpose(target.size.y, target.size.x);
			const int R = min(source.size.x, source.size.y) * parse<float>(arguments.value("blur","1/8"_));
			//const int R = max(min(widths), min(heights))/4; //8
			box(transpose, source, R/*, outerBackgroundColor*/);
			box(blur, transpose, R/*, outerBackgroundColor*/);
			box(transpose, blur, R/*, outerBackgroundColor*/);
			box(blur, transpose, R/*, outerBackgroundColor*/);
			target = copy(blur);
		}
		// -- Copies source images over blur background
		for(size_t elementIndex: range(elements.size)) { // -- Blends transparent images
			if(!images[elementIndex].alpha) continue;
			const Element& element = elements[elementIndex];
			float x0 = element.min.x, x1 = element.max.x;
			float y0 = element.min.y, y1 = element.max.y;
			int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
			int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
			int2 size(ix1-ix0, iy1-iy0);
			if(!(size.x>0 && size.y>0)) continue;
			assert(size.x>0 && size.y>0);
			parallel_chunk(size.y, [&](uint, int Y0, int DY) {
				for(int y: range(Y0, Y0+DY)) {
					v4sf* sourceLine = source.begin() + (iy0+y)*source.stride;
					v4sf* targetLine = target.begin() + (iy0+y)*source.stride;
					for(int x: range(size.x)) targetLine[ix0+x] = mix(targetLine[ix0+x], sourceLine[ix0+x], sourceLine[ix0+x][3]);
					//for(int x: range(size.x)) targetLine[ix0+x] += sourceLine[ix0+x];
				}
			});
		}
		for(size_t elementIndex: range(elements.size)) { // -- Copy opaque images feathered to blur background
			if(images[elementIndex].alpha) continue;
			const Element& element = elements[elementIndex];
			float x0 = element.min.x, x1 = element.max.x;
			float y0 = element.min.y, y1 = element.max.y;
			int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
			int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
			int2 size(ix1-ix0, iy1-iy0);
			if(!(size.x>0 && size.y>0)) continue;
			assert(size.x>0 && size.y>0);

			int2 feather = parse<float>(arguments.value("feather","1"_))*mmPx;

			for(int y: range(feather.y)) { // Top
				mref<v4sf> sourceLine = source.slice((iy0+y)*source.stride, source.width);
				mref<v4sf> blurLine = blur.slice((iy0+y)*source.stride, source.width);
				mref<v4sf> targetLine = target.slice((iy0+y)*source.stride, source.width);
				for(int x: range(feather.x)) // Left
					targetLine[ix0+x] = mix(blurLine[ix0+x], sourceLine[ix0+x], (x/float(feather.x))*(y/float(feather.y)));
				for(int x: range(feather.x, size.x-feather.x)) // Center
					targetLine[ix0+x] = mix(blurLine[ix0+x], sourceLine[ix0+x], (y/float(feather.y)));
				for(int x: range(feather.x)) // Right
					targetLine[ix1-1-x] = mix(blurLine[ix1-1-x], sourceLine[ix1-1-x], (x/float(feather.x))*(y/float(feather.y)));
			}
			parallel_chunk(feather.y, size.y-feather.y, [&](uint, int Y0, int DY) { // Center
				for(int y: range(Y0, Y0+DY)) {
					v4sf* sourceLine = source.begin() + (iy0+y)*source.stride;
					v4sf* blurLine = blur.begin() + (iy0+y)*source.stride;
					v4sf* targetLine = target.begin() + (iy0+y)*source.stride;
					for(int x: range(feather.x)) targetLine[ix0+x] = mix(blurLine[ix0+x], sourceLine[ix0+x], (x/float(feather.x))); // Left
					for(int x: range(feather.x, size.x-feather.x)) targetLine[ix0+x] = sourceLine[ix0+x];
					for(int x: range(feather.x)) targetLine[ix1-1-x] = mix(blurLine[ix1-1-x], sourceLine[ix1-1-x], (x/float(feather.x))); // Right
				}
			});
			for(int y: range(feather.y)) { // Bottom
				v4sf* sourceLine = source.begin() + (iy1-1-y)*source.stride;
				v4sf* blurLine = blur.begin() + (iy1-1-y)*source.stride;
				v4sf* targetLine = target.begin() + (iy1-1-y)*source.stride;
				for(int x: range(feather.x)) // Left
					targetLine[ix0+x] = mix(blurLine[ix0+x], sourceLine[ix0+x], (x/float(feather.x))*(y/float(feather.y)));
				for(int x: range(feather.x,size.x-feather.x)) // Center
					targetLine[ix0+x] = mix(blurLine[ix0+x], sourceLine[ix0+x], (y/float(feather.y)));
				for(int x: range(feather.x)) // Right
					targetLine[ix1-1-x] = mix(blurLine[ix1-1-x], sourceLine[ix1-1-x], (x/float(feather.x))*(y/float(feather.y)));
			}
		}
		//log(blurTime);
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
