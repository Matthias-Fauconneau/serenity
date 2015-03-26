#include "parse.h"
#include "file.h"
#include "jpeg.h"
#include "png.h"
#include "text.h"
#include "thread.h"
#include "simd.h"

static void blend(const Image4f& target, uint x, uint y, v4sf source_linear, float opacity) {
	v4sf& target_linear = target(x,y);
	v4sf linear = mix(target_linear, source_linear, opacity);
	linear[3] = min(1.f, target_linear[3]+opacity); // Additive opacity accumulation
	target_linear = linear;
}

static void blit(const Image4f& target, int2 origin, const Image& source, v4sf color, float opacity) {
	int2 min = ::max(int2(0), origin);
	int2 max = ::min(target.size, origin+source.size);
	for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
		byte4 BGRA = source(x-origin.x, y-origin.y);
		extern float sRGB_reverse[0x100];
		v4sf linear = {sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]], 0};
		blend(target, x, y, color*linear, opacity*BGRA.a/0xFF);
	}
}

struct ImageElement : Element {
	Map file;
	Image image = decodeImage(file);
	ImageElement(string fileName, const Folder& folder) : file(fileName , folder) {
		aspectRatio = (float)image.size.x/image.size.y;
		sizeHint = vec2(image.size)/600.f*inchMM;
	}
	Image source() const override { return share(image); }
	Image4f render(float mmPx) override {
		int2 size = this->size(mmPx);
		assert_(size.x > 0 && size.y > 0);
		Image4f render = size == image.size ? convert(image) : convert(resize(size, image)); // TODO: direct linear float resize
		image = Image(); // Releases source;
		return render;
	}
};
struct TextElement : Element {
	String string;
	float textSize = 12;
	bool transpose = false;
	int align = 0;
	bgr3f color = white;
	TextElement(::string text, float textSize, bool transpose, int align, bgr3f color)
		: string(copyRef(text)), textSize(textSize), transpose(transpose), align(align), color(color) {
		vec2 size = Text(text, textSize/72*72, color, 1, 0, "LinLibertine", false, 1, align).sizeHint();
		if(transpose) swap(size.x, size.y);
		aspectRatio = (float)size.x/size.y;
	}
	Image source() const override { return {}; }
	Image4f render(float mmPx) override {
		int2 size = this->size(mmPx);
		if(transpose) swap(size.x, size.y);
		if(!(size.x > 0 && size.y > 0))  return {};
		Text text(string, textSize/72*inchMM*mmPx, color, 1, (floor(max.x*mmPx) - ceil(min.x*mmPx) - 4 -10*mmPx /*FIXME*/),
				  "LinLibertine", false, 1, align);
		Image4f target (size, true); target.clear(float4(0));
		auto graphics = text.graphics(vec2(size));
		for(const Glyph& e: graphics->glyphs) {
			Font::Glyph glyph = e.font.font(e.fontSize).render(e.index);
			if(glyph.image) blit(target, int2(round(e.origin))+glyph.offset, glyph.image, v4sf{e.color.b ,e.color.g, e.color.r, 0}, e.opacity);
		}
		if(transpose) {
			Image4f source = move(target);
			target = Image4f(size.y, size.x, source.alpha);
			assert_(target.size.x == source.size.y && target.size.y == source.size.x);
			for(size_t y: range(size.y)) for(size_t x: range(size.x)) target(y, target.size.y-1-x) = source(x, y);
		}
		return target;
	}
};

#undef assert
#define assert(expr, message...) ({ if(!(expr)) { error(#expr ""_, ## message); return; } })

LayoutParse::LayoutParse(const Folder& folder, TextData&& s, function<void(string)> logChanged, FileWatcher* watcher)
	: logChanged(logChanged) {
	const ref<string> parameters = {"size","margin","space","color","chroma","intensity","hue","blur","feather"};
	// -- Parses arguments
	for(;;) {
		int nextLine = 0;
		while(s && s[nextLine] != '\n') nextLine++;
		string line = s.peek(nextLine);
		if(startsWith(line, "#"_)) { s.until('\n'); continue; }
		if(line.contains('=')) {
			string key = s.whileNo(" \t\n=");
			assert(parameters.contains(key), "Unknown parameter", key, ", expected", parameters);
			s.whileAny(" \t");
			s.skip("=");
			s.whileAny(" \t");
			string value = s.whileNo(" \t\n");
			s.whileAny(" \t");
			if(s.match("#")) s.whileNot('\n');
			s.skip("\n");
			arguments.insert(copyRef(key), copyRef(value));
		} else break;
	}
	size = argument<vec2>("size"), margin = value<vec2>("margin"_, 10), space = value<vec2>("space"_, 5);
	s.whileAny(" \n");

	// -- Parses table
	array<String> files = folder.list(Files);
	files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
	array<array<int>> rows;

	while(s) {
		array<int> row;
		while(s.available(1) && !s.match("\n")) {
			if(s.match("#")) { s.until('\n'); continue; }
			/***/ if(s.match("-")) row.append(-1);
			else if(s.match("|")) { columnStructure=true; row.append(-2); }
			else if(s.match("\\")) row.append(-3);
			else {
				unique<Element> element = nullptr;
				/**/ if(s.match("@")) { // Indirect text
					string file = s.whileNo("* \t\n");
					if(!existsFile(file)) { error(str(s.lineIndex)+": No such text"_, file, "in", files); return; }
					if(watcher) watcher->addWatch(file);
					element = unique<TextElement>(readFile(file), 12.f, false, -1, value<float>("color", 1));
					freeAspects.append(elements.size);
				} else if(s.match("\"")) { // Text
					String text = replace(s.until('"'),"\\n","\n");
					string textSize = s.whileDecimal();
					bool transpose = s.match('T');
					int align = s.match('R');
					element = unique<TextElement>(text, textSize ? parseDecimal(textSize) : 12, transpose, align, value<float>("color", 1));
					//freeAspects.append(elements.size);
				} else { // Image
					string name = s.whileNo("*! \t\n");
					string file = [&](string name) { for(string file: files) if(startsWith(file, name+"."_)) return file; return ""_; }(name);
					if(!file) { error(str(s.lineIndex)+": No such image"_, name, "in", files); return; }
					if(watcher) watcher->addWatch(file);
					element = unique<ImageElement>(file, folder);
				}
				if(s.match("!")) element->anchor.x = 1./2;
				if(s.match("*")) preferredSize.append(elements.size);
				if(element->anchor.x) horizontalAnchors.append(elements.size);
				if(element->anchor.y) verticalAnchors.append(elements.size);
				element->index = int2(row.size, rows.size);
				row.append(elements.size); // Appends element index to row
				elements.append(move(element));
			}
			s.whileAny(" \t"_);
		}
		assert(row);
		// Automatically generate table structure from row-wise definition
		for(auto& o : rows) {
			if(o.size < row.size) { assert(o.size==1); o.append(-1); rowStructure=true; gridStructure=false; }
			if(row.size < o.size) { row.append(-1); rowStructure=true; gridStructure=false; }
		}
		rows.append(move(row));
		if(!s) break;
	}
	if(rows.size==1) rowStructure=true;
	assert(rows);
	size_t columnCount = rows[0].size;
	table = int2(columnCount, rows.size);
	for(const size_t y: range(table.rowCount)) {
		assert(rows[y].size == columnCount);
		for(const size_t x: range(table.columnCount)) {
			int2 index (x,y);
			if(rows[y][x] >= 0) {
				table(index) = {size_t(rows[y][x]), index, 1, false, false};
			} else {
				Cell& cell = table(x, y);
				if(rows[y][x] == -1) cell.horizontalExtension = true;
				if(rows[y][x] == -2) cell.verticalExtension = true;
				if(rows[y][x] == -3) cell.horizontalExtension = true, cell.verticalExtension = true;
				int2 size = 1;
				while(rows[index.y][index.x] == -3) index.x--, index.y--, size.x++, size.y++;
				while(rows[index.y][index.x] == -2) index.y--, size.y++;
				while(rows[index.y][index.x] == -1) index.x--, size.x++;
				Cell& parent = table(index);
				parent.parentSize = max(parent.parentSize, size);
				cell.parentIndex = parent.parentIndex;
			}
		}
	}
	for(Cell& cell : table.cells) {
		cell.parentElementIndex = table(cell.parentIndex).parentElementIndex;
		cell.parentSize = table(cell.parentIndex).parentSize;
		assert_(elements[cell.parentElementIndex]->index == cell.parentIndex);
		elements[cell.parentElementIndex]->cellCount = cell.parentSize;
	}
	for(size_t elementIndex : range(elements.size)) {
		const Element& element = elements[elementIndex];
		for(const size_t y: range(element.index.y+1, element.index.y+element.cellCount.y)) {
			for(const size_t x: range(element.index.x+1, element.index.x+element.cellCount.x)) {
				if(table(x, y).parentElementIndex != elementIndex) {
					elements[table(x, y).parentElementIndex]->root = false;
				}
			}
		}
	}
	if(table.columnCount == 1) columnStructure=true;
	if(table.columnCount == rows.size) gridStructure = true;
	if(1) log(strx(int2(size)), strx(int2(margin)), strx(int2(space)), strx(table.size),
		gridStructure?"grid":"", rowStructure?"row":"", columnStructure?"column":"");
}
