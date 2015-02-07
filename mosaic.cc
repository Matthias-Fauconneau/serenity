#include "window.h"
#include "layout.h"
#include "interface.h"
#include "render.h"
#include "jpeg.h"
#include "time.h"
#include "algebra.h"
#include "serialization.h" // parse<vec2>
#include "text.h"

inline float parseValue(string value) {
	TextData s(value);
	float num = s.decimal();
	if(s.match("/")) return num/s.decimal();
	else return num;
}

typedef float float4 __attribute((__vector_size__ (16)));
inline float4 constexpr float4_1(float f) { return (float4){f,f,f,f}; }
inline float4 mean(const ref<float4> x) { assert(x.size); return sum(x, float4_1(0)) / float4_1(x.size); }
template<> inline String str(const float4& v) { return "("+str(v[0], v[1], v[2], v[3])+")"; }

/// 2D array of floating-point 4 component vector pixels
struct ImageF : buffer<float4> {
	ImageF(){}
	ImageF(buffer<float4>&& data, int2 size, size_t stride) : buffer(::move(data)), size(size), stride(stride) {
		assert(buffer::size==size_t(size.y*stride), buffer::size, size, stride);
	}
	ImageF(int width, int height) : buffer(height*width), width(width), height(height), stride(width) {
		assert(size>int2(0), size, width, height);
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

// Box convolution with constant border
void box(const ImageF& target, const ImageF& source, const int width/*, const float4 border*/) {
	assert(target.size.y == source.size.x && target.size.x == source.size.y && uint(target.stride) == target.width && uint(source.stride)==source.width);
	parallel_chunk(source.size.y, [&](uint, int Y0, int DY) { // Top
		const float4* const sourceData = source.data;
		float4* const targetData = target.begin();
		const uint sourceStride = source.stride;
		const uint targetStride = target.stride;
		//const float4 scale = float4_1(1./(width+1/*+width*/));
		//const float4 sum0 = 0; //float4_1(width)*border;
		for(int y: range(Y0, Y0+DY)) {
			const float4* const sourceRow = sourceData + y * sourceStride;
			float4* const targetColumn = targetData + y;
			float4 sum = float4_1(0);//sum0;
			for(uint x: range(width)) sum += sourceRow[x];
			float N = width;
			for(uint x: range(width)) {
				sum += sourceRow[x+width];
				N++;
				const float4 scale = float4_1(1./N);
				targetColumn[x * targetStride] = scale * sum;
				//sum -= border;
			}
			const float4 scale = float4_1(1./N);
			for(uint x: range(width, sourceStride-width)) {
				float4 const* source = sourceRow + x;
				sum += source[width];
				targetColumn[x * targetStride] = scale * sum;
				sum -= source[-width];
			}
			for(uint x: range(sourceStride-width, sourceStride)) {
				//sum += border;
				const float4 scale = float4_1(1./N);
				targetColumn[x * targetStride] = scale * sum;
				sum -= sourceRow[x-width];
				N--;
			}
		}
	});
}

static float4 mix(float4 x, float4 y, float a) { return float4_1(1-a)*x + float4_1(a)*y; }

struct Mosaic {
	// Image source
	Folder folder;

	// - Layout definition
	ref<string> parameters = {"page-size"_,"outer","inner","same-outer"_,"same-inner","same-size","chroma","intensity","hue"};
	map<String, String> arguments;
	vec2 pageSizeMM = 0, pageSizePx = 0;
	struct Element {
		virtual ~Element() {}
		virtual vec2 size() abstract;
		virtual Image image(int2 size, float inchPx) abstract;
	};
	struct ImageElement : Element {
		Map file;
		ImageElement(string fileName, const Folder& folder) : file(fileName , folder) {}
		vec2 size() override { return vec2(::imageSize(file)); }
		Image image(int2, float) override {
			Image image = decodeImage(file);
			image.alpha = false;
			return image;
		}
	};
	struct TextElement : Element {
		String string;
		float textSize = 12;
		Text text;
		bool transpose;
		TextElement(::string text) : string(copyRef(text)), text(text, textSize/72*72, white, 1, 0, "LinLibertine") {}
		vec2 size() override {
			vec2 size = text.sizeHint();
			if(transpose) swap(size.x, size.y);
			return size;
		}
		Image image(int2 size, float inchPx) override {
			if(transpose) swap(size.x, size.y);
			text = Text(string, textSize/72*inchPx, white, 1, 0, "LinLibertine");
			Image image = ::render(size, text.graphics(vec2(size)));
			if(transpose) image = rotate(image);
			return image;
		}
	};
	array<unique<Element>> elements; // Image or text elements (TODO: polymorphic object)
	array<array<int>> rows; // Index into elements (-1: row extension, -2 column extension, -3: inner extension)
	buffer<vec2> sizes; // Elements sizes

	// - Layout solution
	vec2 innerMM = 0, outerMM = 0; // Margins
	Vector widthsMM, heightsMM; // Image sizes
	static constexpr bool constantMargin = false;

	// - Layout render
	Graphics page;

	array<char> logText;
	function<void(string)> logChanged;
	template<Type... Args> void log(const Args&... args) { auto message = str(args...); this->logText.append(message+"\n"); ::log(message); if(logChanged) logChanged(this->logText); }
	array<char> errors;
	template<Type... Args> void error(const Args&... args) {
		 if(logChanged) { auto message = str(args...); errors.append(message+"\n"); this->logText.append(message+"\n"); ::log(message);logChanged(this->logText); }
		 else ::error(args...);
	}
#undef assert
#define assert(expr, message...) ({ if(!(expr)) { error(#expr ""_, ## message); return; } })

	Mosaic(const Folder& folder, TextData&& s, function<void(string)> logChanged = {}) : folder("."_, folder), logChanged(logChanged) {
		// -- Parses arguments
		for(;;) {
			int nextLine = 0;
			while(s && s[nextLine] != '\n') nextLine++;
			string line = s.peek(nextLine);
			if(startsWith(line, "#")) { s.until('\n'); continue; }
			if(line.contains('=')) {
				string key = s.whileNo(" \t\n=");
				assert(parameters.contains(key), "Unknown parameter", key, ", expected", parameters);
				s.whileAny(" \t");
				s.skip("=");
				s.whileAny(" \t");
				string value = s.whileNo(" \t\n");
				s.whileAny(" \t");
				s.skip("\n");
				arguments.insert(copyRef(key), copyRef(value));
			} else break;
		}
		s.whileAny(" \n");

		// -- Parses mosaic definition
		array<String> files = folder.list(Files);
		files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
		bool rowStructure = false, columnStructure = false, tableStructure = true;

		while(s) {
			array<int> row;
			while(!s.match("\n")) {
				if(s.match("#")) { s.until('\n'); continue; }
				/***/ if(s.match("-")) row.append(-1); //{ assert(row); row.append(row.last()); }
				else if(s.match("|")) { columnStructure=true; row.append(-2); } //{ assert(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
				else if(s.match("\\")) row.append(-3); //{ assert(rows && rows.last().size==row.size+1); row.append(rows.last()[row.size]); }
				else {
					row.append(elements.size); // Appends element index to row
					if(s.match("\"")) { // Text
						unique<TextElement> text (replace(s.until('"'),"\\n","\n"));
						string textSize = s.whileDecimal();
						text->textSize = textSize ? parseDecimal(textSize) : 12;
						text->transpose = s.match("T");
						elements.append(move(text));
					} else { // Image
						string name = s.whileNo(" \t\n");
						string file = [&](string name) { for(string file: files) if(startsWith(file, name)) return file; return ""_; }(name);
						if(!file) { error("No such image"_, name, "in", files); return; }
						elements.append(unique<ImageElement>(file, folder));
					}
				}
				s.whileAny(" \t"_);
			}
			assert(row);
			// Automatically generate table structure from row structure
			for(auto& o : rows) {
				if(o.size < row.size) { assert(o.size==1); o.append(-1); rowStructure=true; tableStructure=false; }
				if(row.size < o.size) { /*assert(row.size==1);*/ row.append(-1); rowStructure=true; tableStructure=false; }
			}
			rows.append(move(row));
			if(!s) break;
		}
		if(rows.size==1) rowStructure=true;
		assert(rows);
		log(arguments);

		sizes = apply(elements, [&](Element& element) { return element.size(); });
		/*for(size_t imageIndex: range(elements.size)) {
			log(elements[imageIndex], strx(sizes[imageIndex]), (float)sizes[imageIndex].x/sizes[imageIndex].y);
		}*/
		Vector aspectRatios = apply(sizes, [=](vec2 size){ return (float)size.x/size.y; }); // Image aspect ratios

		// Page definition
		pageSizeMM = 10.f*parse<vec2>(arguments.at("page-size")); // 50x40, 40x30, 114x76
		assert_(pageSizeMM.x>0 && pageSizeMM.y>0);
		auto value = [this](string name, float default_) { return arguments.contains(name) ? parseValue(arguments.at(name)) : default_; };
		vec2 inner0 = vec2(value("inner"_,15)), outer0 = vec2(value("outer"_,20));
		log(inner0, outer0);

		// System
		const size_t k = elements.size + 2 + 2; // Unknowns (heights + margins)
		size_t uniformColumnCount = rows[0].size;
		//for(ref<int> row: rows) if(row.size != rows[0].size) { uniformColumnCount = 0; assert(rows[0].size == 1); break; }
		for(ref<int> row: rows) assert(row.size == rows[0].size);
		if(uniformColumnCount == 1) columnStructure=true;
		if(uniformColumnCount == rows.size) tableStructure = true;
		bool sameWidthsInColumn = columnStructure && tableStructure && !rowStructure; // Disable same widths constraints when layout definition had a row structure
		bool sameHeightsInRow = rowStructure || (/*!columnStructure &&*/ tableStructure && uniformColumnCount>1); // Disables same heights constraints when layout definition has no table structure or column count are not uniform
		bool sameElementSizes = arguments.value("same-size"_, ((rows.size==1&&rows[0].size==2) || (rows.size==2&&uniformColumnCount==1))?"1"_:"0"_) != "0"_; // Enables same sizes constraints on single row/column of two elements
		log("sameWidthsInColumn", sameWidthsInColumn, "sameHeightsInRow", sameHeightsInRow, "sameElementSizes", sameElementSizes,
		"tableStructure", tableStructure, "rowStructure", rowStructure, "columnStructure", columnStructure, "uniformColumnCount", uniformColumnCount);
		size_t preallocatedEquationCount =
				rows.size + // Fit row width
				uniformColumnCount + // Fit column height
				(sameWidthsInColumn?uniformColumnCount*(rows.size-1):0) + // Same widths in column
				(sameHeightsInRow?rows.size*(uniformColumnCount-1):0) + // Same heights in row
				(sameElementSizes?(elements.size-1)*2:0) + // Same image sizes (width, height)
				1 + // outer.x=outer.y
				1; // inner.x=inner.y
		Matrix A (preallocatedEquationCount, k); Vector b(preallocatedEquationCount); b.clear(0); // Linear system
		size_t equationIndex = 0;
		assert_(pageSizeMM);
		for(const size_t rowIndex : range(rows.size)) { // Row fit width equations
			size_t imageCount = 0;
			for(const size_t columnIndex: range(rows[rowIndex].size)) {
				int elementIndex = rows[rowIndex][columnIndex];
				if(elementIndex >=0) { // Normal image root origin instance
					assert(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = aspectRatios[elementIndex];
					imageCount++;
				}
				else if(elementIndex == -2) { // Column extension across rows
					size_t sourceRowIndex = rowIndex-1;
					while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
					int elementIndex = rows[sourceRowIndex][columnIndex];
					assert(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = aspectRatios[elementIndex];
					imageCount++;
				}
				else {
					assert(elementIndex == -1 || elementIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, elements.size) = 2; // Vertical outer margin width
			A(equationIndex, elements.size+2) = imageCount-1; // Vertical inner margin width
			b[equationIndex] = pageSizeMM.x - 2*outer0.x - (imageCount-1)*inner0.x;
			log("Row", rowIndex, "fit width");
			equationIndex++;
		}
		for(const size_t columnIndex : range(uniformColumnCount)) { // Column equations
			size_t imageCount = 0;
			for(const size_t rowIndex: range(rows.size)) { // Fit width
				int elementIndex = rows[rowIndex][columnIndex];
				if(elementIndex >=0) { // Normal image root origin instance
					assert(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = 1;
					imageCount++;
				}
				else if(elementIndex == -1) { // Row extension across columns
					size_t sourceColumnIndex = columnIndex-1;
					while(rows[rowIndex][sourceColumnIndex] == -1) sourceColumnIndex--;
					int elementIndex = rows[rowIndex][sourceColumnIndex];
					assert(A(equationIndex, elementIndex)==0);
					A(equationIndex, elementIndex) = 1;
					imageCount++;
				}
				else {
					assert(elementIndex == -2 || elementIndex == -3);
					//A(i, j) = 0; // Sparse
				}
			}
			A(equationIndex, elements.size+1) = 2; // Horizontal outer margin height
			A(equationIndex, elements.size+3) = imageCount-1; // Horizontal inner margin height
			b[equationIndex] = pageSizeMM.y - 2*outer0.y - (imageCount-1)*inner0.y;
			log("Column", columnIndex, "fit height");
			equationIndex++;
		}
		if(sameWidthsInColumn) { // Same element widths in column
			for(const size_t columnIndex : range(uniformColumnCount)) { // Column same width constraint
				size_t firstRowIndex = invalid;
				for(const size_t rowIndex: range(rows.size-1)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >= 0 && (columnIndex+1==uniformColumnCount || rows[rowIndex][columnIndex+1]!=-1)) { firstRowIndex = rowIndex; break; }
				}
				if(firstRowIndex == invalid) continue;

				for(const size_t rowIndex: range(firstRowIndex+1, rows.size)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >=0 && (columnIndex==uniformColumnCount-1 || rows[rowIndex][columnIndex+1]!=-1)) { // Single row image
						assert(A(equationIndex, elementIndex)==0);
						// - w[firstColumnIndex] + w[columnIndex] = 0
						A(equationIndex, rows[firstRowIndex][columnIndex]) = -aspectRatios[rows[firstRowIndex][columnIndex]];
						A(equationIndex, elementIndex) = aspectRatios[elementIndex];
						b[equationIndex] = 0;
						log("Column", columnIndex, "same element width", firstRowIndex, rowIndex);
						equationIndex++;
					}
				}
			}
		}
		if(sameHeightsInRow) { // Same element heights in row constraint
			for(const size_t rowIndex : range(rows.size)) {
				size_t firstColumnIndex = invalid;
				for(const size_t columnIndex: range(uniformColumnCount-1)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >= 0 && (rowIndex+1==rows.size || rows[rowIndex+1][columnIndex]!=-2)) { firstColumnIndex = columnIndex; break; }
				}
				if(firstColumnIndex == invalid) continue;

				for(const size_t columnIndex: range(firstColumnIndex+1, uniformColumnCount)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex >=0 && (rowIndex==rows.size-1 || rows[rowIndex+1][columnIndex]!=-2)) { // Single row image
						assert(A(equationIndex, elementIndex)==0);
						// - h[@firstColumnIndex] + h[@columnIndex] = 0
						A(equationIndex, rows[rowIndex][firstColumnIndex]) = -1;
						A(equationIndex, elementIndex) = 1;
						b[equationIndex] = 0;
						log("Row", rowIndex, "same element height", firstColumnIndex, columnIndex);
						equationIndex++;
					}
				}
			}
		}
		if(sameElementSizes) { // Same image sizes constraint (FIXME: not symetric (which is important for least square solution (when no exact solution exists))
			for(const size_t elementIndex : range(1, elements.size)) {
				assert(A(equationIndex, elementIndex)==0);
				// - heights[0] + heights[elementIndex] = 0
				A(equationIndex, 0) = -1;
				A(equationIndex, elementIndex) = 1;
				b[equationIndex] = 0;
				log("Same element height", 0, elementIndex);
				equationIndex++;
				// - widths[0] + widths[elementIndex] = 0
				A(equationIndex, 0) = -aspectRatios[0];
				A(equationIndex, elementIndex) = aspectRatios[elementIndex];
				b[equationIndex] = 0;
				log("Same element width", 0, elementIndex);
				equationIndex++;
				// FIXME: Same area would require quadratic programming
			}
		}
		{
			const size_t marginIndexBase = elements.size; // outer, inner
			//bool sameOuterSpacing = arguments.value("same-outer"_, "1"_ ) != "0"_;
			bool sameOuterSpacing = arguments.value("same-outer"_, equationIndex<k?"1"_:"0"_) != "0"_; // FIXME: equationIndex<k might underconstrain on redundant equations
			if(sameOuterSpacing) { // outer.x = outer.y
				A(equationIndex, marginIndexBase+0) = -1; A(equationIndex, marginIndexBase+1) = 1;
				log("Same outer spacing: outer.width = outer.height");
				equationIndex++;
			}
			//bool sameInnerSpacing = arguments.value("same-inner"_, "1"_) != "0"_;
			bool sameInnerSpacing = arguments.value("same-inner"_, equationIndex<k?"1"_:"0"_) != "0"_; // FIXME: equationIndex<k might underconstrain on redundant equations
			if(sameInnerSpacing) { // inner.x = inner.y
				A(equationIndex, marginIndexBase+2) = -1; A(equationIndex, marginIndexBase+3) = 1;
				log("Same inner spacing: inner.width = inner.height");
				equationIndex++;
			}
		}
		assert(k <= equationIndex && equationIndex <= A.m, k, equationIndex, A.m);
		A.m = equationIndex;
		b.size = equationIndex; assert_(b.size <= b.capacity);
		log(equationIndex, "equations (constraints) for", k, "unknowns (arguments)");
		//log(str(b)+"\n"+str(A));
		// Least square system
		// TODO: Weighted least square
		Matrix At = transpose(A);
		Matrix AtA = At * A;
		if(arguments.value("regularize"_, "1"_) != "0"_){ // Regularizes all border size difference unknowns
			const float a = 1;
			for(size_t i: range(elements.size,  elements.size+4)) AtA(i,i) = AtA(i,i) + a*a;
		}
		Vector Atb = At * b;
		//log(str(Atb)+"\n"_+str(AtA));
		Vector x = solve(move(AtA),  Atb);
		//log(x);
		heightsMM = copyRef(x.slice(0, elements.size));
		outerMM = outer0 + vec2(x[elements.size], x[elements.size+1]);
		innerMM = inner0 + vec2(x[elements.size+2], x[elements.size+3]);
		widthsMM = apply(heightsMM.size, [&](size_t i){ return aspectRatios[i] * heightsMM[i]; });

		for(size_t elementIndex: range(elements.size)) if(widthsMM[elementIndex]<=0 || heightsMM[elementIndex]<=0) error("Negative dimensions", widthsMM[elementIndex], heightsMM[elementIndex], "for", elementIndex);

		if(errors) {
			log(heightsMM, outerMM, outer0, vec2(x[elements.size], x[elements.size+1]), innerMM, inner0, vec2(x[elements.size+2], x[elements.size+3]));
			array<char> s;
			float widthSums = 0;
			for(size_t rowIndex : range(rows.size)) {
				float sum = outerMM.x;
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
						float w = widthsMM[elementIndex], h = heightsMM[elementIndex];
						sum += w;
						if(columnIndex+1<rows[rowIndex].size && rows[rowIndex][columnIndex+1]!=-1) sum += innerMM.x;
						s.append(str(w, h));
					}
					s.append("\t");
				}
				sum += outerMM.x;
				widthSums += sum;
				s.append(str(sum)+"\n");
			}
			float dx = pageSizeMM.x - widthSums / rows.size;
			//outer.x += dx/2; // Centers horizontally (i.e distribute tolerated error (least square residual) on each side) (Already done by each rows)
			float heightSums = 0;
			for(size_t columnIndex : range(uniformColumnCount)) {
				float sum = outerMM.y;
				for(size_t rowIndex : range(rows.size)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex == -1) {
						int sourceColumnIndex = columnIndex-1;
						while(rows[rowIndex][sourceColumnIndex] == -2) sourceColumnIndex--;
						elementIndex = rows[rowIndex][sourceColumnIndex];
					}
					if(elementIndex >= 0) {
						float h = heightsMM[elementIndex];
						sum += h;
						if(rowIndex+1<rows.size && rows[rowIndex+1][columnIndex]!=-2) sum += innerMM.y;
					}
				}
				sum += outerMM.y;
				heightSums += sum;
				s.append(str(sum)+"\t\t");
			}
			float dy = pageSizeMM.y - heightSums / uniformColumnCount;
			log(s,"\t", pageSizeMM, "dx", dx, "dy", dy, outerMM, innerMM);
			//else log("dx", dx, "dy", dy, pageSize.y, heightSums / n, n, outer.y, outer.y + dy);
			//outer.y += dy/2; // Centers vertically (i.e distribute tolerated error (least square residual) on each side) (Already done by each columns)
		} else {
			log("innerMM", innerMM);
			// TODO: Snaps small innerMM to zero with a "non linear resolution" by iterating over inner0 parameter
		}
	}
	void render(const float _mmPx, const float _inchPx=0) {
		if(errors) return;

		// -- Evaluates resolution
		assert_((_mmPx>0) ^ (_inchPx>0));
		const float inchMM = 25.4;
		const float mmPx = _mmPx ? _mmPx : _inchPx/inchMM;
		assert_(mmPx);
		pageSizePx = pageSizeMM * mmPx;
		const float inchPx = _inchPx ? _inchPx : _mmPx*inchMM;
		{
			assert_(inchPx);
			float minScale = inf, maxScale = 0;
			for(size_t elementIndex: range(elements.size)) {
				float scale = sizes[elementIndex].x / (widthsMM[elementIndex]*mmPx);
				minScale = min(minScale, scale);
				maxScale = max(maxScale, scale);
			}
			log("@"+str(mmPx)+"ppmm "+str(inchPx)+"ppi: \t "
				"min: "+str(minScale)+"x "+str(minScale*mmPx)+"ppmm "+str(minScale*inchPx)+"ppi \t"
				"max: "+str(maxScale)+"x "+str(maxScale*mmPx)+"ppmm "+str(maxScale*inchPx)+"ppi");
		}

		// -- Explicitly evaluate element layout given by solved element sizes
		struct Element { int2 index; Rect rect; int2 size; };
		buffer<Element> elementLayout (elements.size);
		for(const size_t currentRowIndex: range(rows.size)) {
			float x0 = outerMM.x;

			// -- Horizontal centers individual row (i.e distribute tolerated error (least square residual) on each side)
			float W = outerMM.x;
			for(const size_t columnIndex: range(rows[currentRowIndex].size)) {
				int elementIndex = rows[currentRowIndex][columnIndex];
				if(elementIndex == -2) { // Column extension
					int sourceRowIndex = currentRowIndex-1;
					while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
					elementIndex = rows[sourceRowIndex][columnIndex];
				}
				if(elementIndex < 0) continue;
				float w = widthsMM[elementIndex];
				size_t nextColumnIndex=columnIndex+1; while(nextColumnIndex < rows.size && rows[currentRowIndex][nextColumnIndex]==-1) nextColumnIndex++;
				bool lastColumn = nextColumnIndex==rows[currentRowIndex].size;
				float rightMargin = (!lastColumn || constantMargin) ? innerMM.x : outerMM.x;
				W += w + rightMargin;
			}
			float dx = (pageSizeMM.x - W) / 2;
			log("dx=", dx);
			//x0 += dx; // FIXME

			for(const size_t columnIndex: range(rows[currentRowIndex].size)) {
				int elementIndex = rows[currentRowIndex][columnIndex];
				if(elementIndex == -2) { // Column extension
					int sourceRowIndex = currentRowIndex-1;
					while(rows[sourceRowIndex][columnIndex] == -2) sourceRowIndex--;
					int elementIndex = rows[sourceRowIndex][columnIndex];
					float w = widthsMM[elementIndex];
					x0 += w + innerMM.x;
				}
				if(elementIndex < 0) continue;

				// -- Horizontal centers individual row (i.e distribute tolerated error (least square residual) on each side)
				float H = outerMM.y, y0 = outerMM.y;
				for(const size_t rowIndex: range(rows.size)) {
					int elementIndex = rows[rowIndex][columnIndex];
					if(elementIndex == -1) { // Row extension
						int sourceColumnIndex = columnIndex-1;
						while(rows[rowIndex][sourceColumnIndex] == -2) sourceColumnIndex--;
						elementIndex = rows[rowIndex][sourceColumnIndex];
					}
					if(elementIndex < 0) continue;
					size_t nextRowIndex=rowIndex+1; while(nextRowIndex < rows.size && rows[nextRowIndex][columnIndex]==-2) nextRowIndex++;
					bool lastRow = nextRowIndex==rows.size;
					float belowMargin = (!lastRow || constantMargin) ? innerMM.y : outerMM.y;
					H += heightsMM[elementIndex] + belowMargin;
					//log(H, heights[elementIndex], belowMargin, rowIndex-1, y0);
					if(rowIndex==currentRowIndex-1) y0 = H;
				}
				float dy = (pageSizeMM.y - H) / 2;
				y0 += dy;

				float w = widthsMM[elementIndex], h = heightsMM[elementIndex];
				float x1 = x0+w, y1 = y0+h;

				int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
				int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
				int2 size(ix1-ix0, iy1-iy0);
				assert(size.x>0 && size.y>0);
				elementLayout[elementIndex] = Element{int2(currentRowIndex, columnIndex), Rect(vec2(x0, y0), vec2(x1, y1)), size};

				x0 += w + innerMM.x;
			}
		}

		// -- Decodes/renders each element
		//log("Decoding"_, elements);
		Time load;
		buffer<Image> images = apply(elements.size, [&](size_t elementIndex) {
			int2 size = elementLayout[elementIndex].size;
			return elements[elementIndex]->image(size, inchPx);
		});
		log("+", load);

		// -- Evaluates each elements dominant color (or use user argument overrides)
		buffer<float4> innerBackgroundColors = apply(images.size, [&](const size_t elementIndex) {
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
			if(arguments.contains("hue"_)) H = parseValue(arguments.at("hue"_)) * 0xFF;
			int C = parseValue(arguments.value("chroma"_, "1/4"_)) * 0xFF;
			int X = C * (0xFF - abs((H%85)*6 - 0xFF)) / 0xFF;
			int I = argmax(ref<int>(intensityHistogram));
			if(arguments.contains("intensity"_)) I = parseValue(arguments.at("intensity"_)) * 0xFF;
			int R,G,B;
			if(H < 43) R=C, G=X, B=0;
			else if(H < 85) R=X, G=C, B=0;
			else if(H < 128) R=0, G=C, B=X;
			else if(H < 171) R=0, G=X, B=C;
			else if(H < 213) R=X, G=0, B=C;
			else if(H < 256) R=C, G=0, B=X;
			else ::error(H);
			int m = max(0, I - (R+G+B)/3);
			// Limits intensity within sRGB
			m = min(m, 0xFF-R);
			m = min(m, 0xFF-G);
			m = min(m, 0xFF-B);
			extern float sRGB_reverse[0x100];
			return float4{sRGB_reverse[m+B], sRGB_reverse[m+G], sRGB_reverse[m+R], 0};
		});

		page.bounds = Rect(round(pageSizePx));
		ImageF target(int2(page.bounds.size()));
		assert(ptr(target.data)%sizeof(float4)==0);
		if(target.Ref::size < 8*1024*1024) target.clear(float4_1(0)); // Assumes larger allocation are clear pages

		// -- Transitions exterior borders to background color
		const int iX = floor(outerMM.x*mmPx);
		const int iY = floor(outerMM.y*mmPx);
		float4 outerBackgroundColor = float4_1(1) * ::mean(innerBackgroundColors);
		vec2 margin = (outerMM-innerMM)*mmPx; // Transition on inner margin size, outer outer margin is constant
		vec2 innerPx = innerMM*mmPx;
		int2 size = target.size;
		// Outer background vertical sides
		if(iX > 0) parallel_chunk(max(0, iY), min(size.y, size.y-iY), [&](uint, int Y0, int DY) {
			for(int y: range(Y0, Y0+DY)) {
				float4* line = target.begin() + y*target.stride;
				for(int x: range(iX)) {
					float w = constantMargin ? (x>=margin.x ? 1 - (float(x)-margin.x) / float(innerPx.x) : 1) : float(iX-x) / iX;
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
				float w = constantMargin ? (y>=margin.y ? 1 - float(y-margin.y) / float(innerPx.y) : 1) : float(iY-y) / iY;
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
				float yw = constantMargin ? (y>=margin.y ? float(y-margin.y) / float(innerPx.y) : 0) : 1 - float(iY-y) / iY;
				for(int x: range(iX)) {
					float xw =  constantMargin ? (x>=margin.x ? float(x-margin.x) / float(innerPx.x) : 0) : 1 - float(iX-x) / iX;
					float w = (1-xw)*yw + xw*(1-yw) + (1-xw)*(1-yw);
					assert(w >= 0 && w <= 1);
					float4 c = float4_1(w) * outerBackgroundColor;
					line0[x] += c;
					line0[size.x-1-x] += c;
					line1[x] += c;
					line1[size.x-1-x] += c;
				}
			}
		});

		// -- Copies source images to target mosaic
		for(size_t elementIndex: range(elements.size)) {
			Element element = elementLayout[elementIndex];
			int currentRowIndex = element.index[0];
			int columnIndex = element.index[1];
			float x0 = element.rect.min.x, x1 = element.rect.max.x;
			float y0 = element.rect.min.y, y1 = element.rect.max.y;
			int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
			int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
			int2 size(ix1-ix0, iy1-iy0);
			assert(size.x>0 && size.y>0);

			// Element margins
			float leftMarginMM = (constantMargin || columnIndex) ? innerMM.x : outerMM.x;
			float aboveMarginMM = (constantMargin || currentRowIndex) ? innerMM.y : outerMM.y;
			size_t nextColumnIndex=columnIndex+1;
			while(nextColumnIndex < rows.size && rows[currentRowIndex][nextColumnIndex]==-1) nextColumnIndex++;
			bool lastColumn = nextColumnIndex==rows[currentRowIndex].size;
			float rightMarginMM = (!lastColumn || constantMargin) ? innerMM.x : outerMM.x;
			size_t nextRowIndex=currentRowIndex+1;
			while(nextRowIndex < rows.size && rows[nextRowIndex][columnIndex]==-2) nextRowIndex++;
			bool lastRow = nextRowIndex==rows.size;
			float belowMarginMM = (!lastRow || constantMargin) ? innerMM.y : outerMM.y;

			// Element min,size,max pixel coordinates
			int iw0 = ix0-round((x0-leftMarginMM)*mmPx), ih0 = iy0-round((y0-aboveMarginMM)*mmPx); // Previous border sizes
			int iw1 = round((x1+rightMarginMM)*mmPx) - ix1, ih1 = round((y1+belowMarginMM)*mmPx) - iy1; // Next border sizes

			const Image& image = images[elementIndex];
			 // TODO: single pass linear resize, float conversion (or decode to float and resize) + direct output to target
			Image iSource = size == image.size ? share(image) : resize(size, image);
			ImageF source (size);
			parallel_chunk(iSource.Ref::size, [&](uint, size_t I0, size_t DI) {
				extern float sRGB_reverse[0x100];
				for(size_t i: range(I0, I0+DI)) source[i] = {sRGB_reverse[iSource[i][0]], sRGB_reverse[iSource[i][1]], sRGB_reverse[iSource[i][2]],
															 image.alpha ? float(iSource[i][2])/0xFF : 1};
			});

			// -- Margins
			if(0) { // -- Extends images over margins with a mirror transition
				// Clamps border transition size to image size to mirror once
				if(iw0 > size.x) iw0 = size.x; if(iw1 > size.x) iw1 = size.x;
				if(ih0 > size.y) ih0 = size.y; if(ih1 > size.y) ih1 = size.y;
				for(int y: range(min(ih0, iy0))) {
					mref<float4> line = target.slice((iy0-y-1)*target.stride, target.width);
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-y/float(ih0))) * source(x, y); // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-y/float(ih0)) * source(x-ix0, y); // Center
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)*(1-y/float(ih0))) * source(size.x-1-x, y); // Right
				}
				parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
					for(int y: range(Y0, Y0+DY)) {
						float4* line = target.begin() + y*target.stride;
						for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1(1-x/float(iw0)) * source(x, y-iy0); // Left
						float4* sourceLine = source.begin() + (y-iy0)*source.stride;
						for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Copy image
						for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)) * source(size.x-1-x, y-iy0); // Right
					}
				});
				for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
					float4* line = target.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * source(x-ix0, size.y-1-(y-iy1)); // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-(y-iy1)/float(ih1)) * source(x-ix0, size.y-1-(y-iy1)); // Center
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)*(1-(y-iy1)/float(ih1))) * source(size.x-1-x, size.y-1-(y-iy1)); // Right
				}
			} else if(1) { // -- Blends inner background over margins with a linear transition
				float4 innerBackgroundColor = innerBackgroundColors[elementIndex];
				for(int y: range(min(ih0, iy0))) {
					mref<float4> line = target.slice((iy0-y-1)*target.stride, target.width);
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-y/float(ih0))) * innerBackgroundColor; // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-y/float(ih0)) * innerBackgroundColor; // Center
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)*(1-y/float(ih0))) * innerBackgroundColor; // Right
				}
				parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
					for(int y: range(Y0, Y0+DY)) {
						float4* line = target.begin() + y*target.stride;
						for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1(1-x/float(iw0)) * innerBackgroundColor; // Left
						float4* sourceLine = source.begin() + (y-iy0)*source.stride;
						for(int x: range(max(0, ix0), min(target.size.x, ix0+size.x))) line[x] = sourceLine[x-ix0]; // Copy image
						for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)) * innerBackgroundColor; // Right
					}
				});
				for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
					float4* line = target.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] += float4_1((1-x/float(iw0))*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] += float4_1(1-(y-iy1)/float(ih1)) * innerBackgroundColor; // Center
					for(int x: range(max(0, ix1), min(ix1+iw1, target.size.x))) line[x] += float4_1(1-(x-ix1)/float(iw1)*(1-(y-iy1)/float(ih1))) * innerBackgroundColor; // Right
				}
			}
		}

		// -- Large gaussian blur approximated with repeated box convolution
		log("Blur");
		Time blur;
		if(1) {
			ImageF blur(target.size);
			{
				ImageF transpose(target.size.y, target.size.x);
				const int R = min(target.size.x, target.size.y) / 8;
				//const int R = max(min(widths), min(heights))/4; //8
				box(transpose, target, R/*, outerBackgroundColor*/);
				box(blur, transpose, R/*, outerBackgroundColor*/);
				box(transpose, blur, R/*, outerBackgroundColor*/);
				box(blur, transpose, R/*, outerBackgroundColor*/);
			}
			if(1) for(auto element: elementLayout) { // -- Copies source images over blur background
				float x0 = element.rect.min.x, x1 = element.rect.max.x;
				float y0 = element.rect.min.y, y1 = element.rect.max.y;
				int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
				int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
				parallel_chunk(max(0, iy0), min(target.size.y, iy1), [&](uint, int Y0, int DY) {
					for(int y: range(Y0, Y0+DY)) {
						float4* blurLine = blur.begin() + y*blur.stride;
						const float4* targetLine = target.begin() + y*target.stride;
						for(int x: range(max(0, int(ix0)), min(target.size.x, int(ix1)))) {
							// TODO: only blend if image.alpha
							blurLine[x] = mix(blurLine[x], targetLine[x], targetLine[x][3]);
						}
					}
				});
			}
			if(1) for(auto element: elementLayout) { // -- Feathers
				float x0 = element.rect.min.x, x1 = element.rect.max.x;
				float y0 = element.rect.min.y, y1 = element.rect.max.y;
				int ix0 = round(x0*mmPx), iy0 = round(y0*mmPx);
				int ix1 = round(x1*mmPx), iy1 = round(y1*mmPx);
				int2 size(ix1-ix0, iy1-iy0);
				assert(size.x>0 && size.y>0);

				// Element min,size,max pixel coordinates
				//int2 feather = int2(floor(clamp(vec2(0), innerMM/2.f, vec2(1))*mmPx));
				int2 feather = int2(floor(innerMM/2.f*mmPx));
				int iw0 = feather.x, ih0 = feather.y; // Previous border sizes
				int iw1 = feather.x, ih1 = feather.y; // Next border sizes

				for(int y: range(min(ih0, iy0))) {
					mref<float4> sourceLine = target.slice((iy0+y)*target.stride, target.width);
					mref<float4> line = blur.slice((iy0-y-1)*target.stride, target.width);
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))*(1-y/float(ih0))); // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] = mix(line[x], sourceLine[x], sourceLine[x][3]*(1-y/float(ih0))); // Center
					for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1+x], sourceLine[ix1+x][3]*(1-x/float(iw1))*(1-y/float(ih0))); // Right
				}
				parallel_chunk(max(0, iy0), min(iy0+size.y, target.size.y), [&](uint, int Y0, int DY) { // Center
					for(int y: range(Y0, Y0+DY)) {
						float4* sourceLine = target.begin() + y*target.stride;
						float4* line = blur.begin() + y*target.stride;
						for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))); // Left
						for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1-x-1], sourceLine[ix1-x-1][3]*(1-x/float(iw1))); // Right
					}
				});
				for(int y: range(max(0, iy1), min(iy1+ih1, target.size.y))) {
					float4* sourceLine = target.begin() + (iy1-1-(y-iy1))*target.stride;
					float4* line = blur.begin() + y*target.stride;
					for(int x: range(min(iw0, ix0))) line[ix0-x-1] = mix(line[ix0-x-1], sourceLine[ix0+x], sourceLine[ix0+x][3]*(1-x/float(iw0))*(1-(y-iy1)/float(ih1))); // Left
					for(int x: range(max(0,ix0),min(target.size.x, ix0+size.x))) line[x] = mix(line[x], sourceLine[x], sourceLine[x][3]*(1-(y-iy1)/float(ih1))); // Center
					for(int x: range(iw1)) line[ix1+x] = mix(line[ix1+x], sourceLine[ix1-x-1], sourceLine[ix1-x-1][3]*(1-x/float(iw1))*(1-(y-iy1)/float(ih1))); // Right
				}
			}
			target = move(blur);
		}
		log("+", blur);

		// -- Convert back to 8bit sRGB
		Image iTarget (target.size);
		assert(target.Ref::size == iTarget.Ref::size);
		parallel_chunk(target.Ref::size, [&](uint, size_t I0, size_t DI) {
			extern uint8 sRGB_forward[0x1000];
			int clip = 0;
			for(size_t i: range(I0, I0+DI)) {
				for(uint c: range(3)) if(!(target[i][c] >= 0 && target[i][c] <= 1)) { if(!clip) log("Clip", target[i][c], i, c); clip++; }
				iTarget[i] = byte4(sRGB_forward[int(round(0xFFF*min(1.f, target[i][0])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][1])))], sRGB_forward[int(round(0xFFF*min(1.f, target[i][2])))]);
			}
			if(clip) log("Clip", clip);
			//assert(!clip);
		});
		page.blits.append(0, page.bounds.size(), move(iTarget));
	}
};

struct MosaicPreview : Application {
	String name = arguments() ? (endsWith(arguments()[0],"mosaic") ? copyRef(arguments()[0]) : arguments()[0]+".mosaic") :
		move(Folder(".").list(Files).filter([](string name){return !endsWith(name,"mosaic");})[0]);
	Mosaic mosaic {"."_, readFile(name)};
	GraphicsWidget view;
	unique<Window> window = nullptr;
	MosaicPreview() {
		//mosaic.render(min(window->Display::size.x/mosaic.pageSizeMM.x, (window->Display::size.y-32-24)/mosaic.pageSizeMM.y)); // window is not instantiated yet
		mosaic.render(min(1050/mosaic.pageSizeMM.x, (1680-32-24)/mosaic.pageSizeMM.y));
		if(mosaic.errors) ::error(mosaic.errors);
		else {
			view = move(mosaic.page);
			window = unique<Window>(&view, -1, [this](){return copyRef(name);});
		}
	}
};
registerApplication(MosaicPreview, preview);

struct MosaicExport : Application {
	// UI
	Text text;
	//Window window {&text};
	Timer autoclose { [this]{
			//window.unregisterPoll(); // Lets process termination close window to assert no windowless process remains
			mainThread.post(); // Lets main UI thread notice window unregistration and terminate
			requestTermination(); // FIXME: should not be necessary
					  }};
	//MosaicExport() { window.actions[Space] = [this]{ autoclose.timeout = {}; }; } // Disables autoclose on input
	// Work
	Thread workerThread {0, true}; // Separate thread to render mosaics while main thread handles UI
	void setText(string logText){ text = logText; Locker lock(mainThread.runLock); /*window.setSize(int2(ceil(text.sizeHint(0)))); window.render();*/ }
	Job job {workerThread, [this]{
			String fileName;
			if(::arguments() && existsFile(::arguments()[0])) fileName = copyRef(::arguments()[0]);
			else if(::arguments() && existsFile(::arguments()[0]+".mosaic"_)) fileName = ::arguments()[0]+".mosaic"_;
			else {
				array<String> files = Folder(".").list(Files|Sorted);
				files.filter([](string fileName){return !endsWith(fileName,"mosaic");});
				if(files.size == 1) fileName = move(files[0]);
			}
			array<String> folders = Folder(".").list(Folders|Sorted);
			if(folders && (!fileName || File(fileName).size()<=2)) { // Batch processes subfolders and write to common output folder
				log("Collection", fileName);
				Time total, render, encode; total.start();
				for(string folderName: folders) {
					Folder folder = folderName;
					array<String> files = folder.list(Files|Sorted);
					files.filter([](string name){return !endsWith(name,"mosaic");});
					if(files.size == 0 && folderName=="Output") continue;
					else if(files.size==0) { error("No mosaics in", folderName); continue; }
					else if(files.size>1) { error("Several mosaics in", folderName); }
					String fileName = move(files[0]);
					string name = endsWith(fileName, ".mosaic") ? section(fileName,'.',0,-2) : fileName;
					log(name);
					//window.setTitle(name); // FIXME: thread safety
					setText(name);
					if(File(fileName, folder).size()<=2) error("Empty file", fileName); // TODO: Nested collections
					Mosaic mosaic {folder, readFile(fileName, folder), {this, &MosaicExport::setText}};
					render.start();
					mosaic.render(0, 300 /*pixel per inch*/);
					render.stop();
					if(mosaic.errors) return;
					encode.start();
					buffer<byte> file = encodeJPEG(::render(int2(round(mosaic.page.bounds.size())), mosaic.page));
					encode.stop();
					writeFile(name+'.'+strx(int2(round(mosaic.pageSizeMM/10.f)))+".jpg"_, file, Folder("Output"_, currentWorkingDirectory(), true), true);
				}
				total.stop();
				log(total, render, encode);
			}
			else if(existsFile(fileName)) {
				Time total; total.start();
				string name = endsWith(fileName, ".mosaic") ? section(fileName,'.',0,-2) : fileName;
				setText(name);
				if(File(fileName).size()<=2) error("Empty file", fileName);  // TODO: Collection argument
				Mosaic mosaic {"."_, readFile(fileName), {this, &MosaicExport::setText}};
				log("Rendering", name); Time render; render.start();
				mosaic.render(0, 300 /*pixel per inch*/);
				log("=", render);
				if(mosaic.errors) return;
				log("Encoding", name);
				Time encode; encode.start();
				buffer<byte> file = encodeJPEG(::render(int2(round(mosaic.page.bounds.size())), mosaic.page));
				encode.stop();
				writeFile(name+'.'+strx(int2(round(mosaic.pageSizeMM/10.f)))+".jpg"_, file, currentWorkingDirectory(), true);
				log("+", encode);
				log("=","total", total);
			}
			autoclose.setRelative(1000); // Automatically close after one second of inactivity unless space bar is pressed
	}};
};
registerApplication(MosaicExport);
