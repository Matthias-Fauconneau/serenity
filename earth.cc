#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "tiff.h"
#include "time.h"
FILE(shader)

generic const T& raw(ref<byte> data) { assert_(data.size==sizeof(T)); return *(T*)data.data; }

/// Decodes packed bitstreams
struct BitReader : ref<byte> {
	size_t bitSize = 0;
	size_t index = 0;
	BitReader() {}
	BitReader(ref<byte> data) : ref<byte>(data) { bitSize=8*data.size; index=0; }
	/// Reads one lsb bit
	uint bit() {
		uint8 bit = uint8(uint8(data[index/8])<<(7-(index&7)))>>7;
		index++;
		return bit;
	}
	/// Reads \a size bits
	uint binary(uint size) {
		uint value = (*(uint64*)(data+index/8) << (64-size-(index&7))) >> int8(64-size);
		index += size;
		return value;
	}
};

buffer<byte> inflate(ref<byte> compressed, buffer<byte>&& target) { //inflate huffman encoded data
	BitReader s(compressed);
	size_t targetIndex = 0;
	while(s.index < s.bitSize) {
		struct Huffman {
			uint count[16] = {};
			uint lookup[288] = {}; // Maximum DEFLATE alphabet size
			Huffman() {}
			// Computes explicit Huffman tree defined implicitly by code lengths
			Huffman(ref<uint> lengths) {
				// Counts the number of codes for each code length
				for(uint length: lengths) if(length) count[length]++;
				// Initializes cumulative offset
				uint offsets[16];
				{int sum = 0; for(size_t i: range(16)) { offsets[i] = sum; sum += count[i]; }}
				// Evaluates code -> symbol translation table
				for(size_t symbol: range(lengths.size)) if(lengths[symbol]) lookup[offsets[lengths[symbol]]++] = symbol;
			}
			uint decode(BitReader& s) {
				int code=0; uint length=0, sum=0;
				do { // gets more bits while code value is above sum
					code <<= 1;
					code |= s.bit();
					length += 1;
					sum += count[length];
					code -= count[length];
				} while (code >= 0);
				return lookup[sum + code];
			}
		};
		Huffman literal;
		Huffman distance;

		bool BFINAL = s.bit();
		int BTYPE = s.binary(2);
		if(BTYPE==0) { // Literal
			s.index=align(8, s.index);
			uint16 length = raw<uint16>(s.slice(s.index/8, 2)); s.index+=16;
			uint16 unused nlength = raw<uint16>(s.slice(s.index/8, 2)); s.index+=16;
			ref<byte> source = s.slice(s.index/8, length);
			for(size_t i: range(length)) target[targetIndex+i] = source[i];
			targetIndex += length;
		} else { // Compressed
			if(BTYPE==1) { // Static Huffman codes
				mref<uint>(literal.count).copy({0,0,0,0,0,0,24,24+144,112});
				for(int i: range(24)) literal.lookup[i] = 256+i; // 256-279: 7
				for(int i: range(144)) literal.lookup[24+i] = i; // 0-143: 8
				for(int i: range(8)) literal.lookup[24+144+i] = 256+24+i; // 280-287 : 8
				for(int i: range(112)) literal.lookup[24+144+8+i] = 144+i; // 144-255: 9
				mref<uint>(distance.count).copy({0,0,0,0,0,32});
				for(int i: range(32)) distance.lookup[i] = i; // 0-32: 5
			} else if(BTYPE==2) { // Dynamic Huffman codes
				uint HLITT = 257+s.binary(5);
				uint HDIST = 1+s.binary(5);
				uint HCLEN = 4+s.binary(4); // Code length
				uint codeLengths[19] = {}; // Code lengths to encode codes
				for(uint i: range(HCLEN)) codeLengths[ref<uint>{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}[i]] = s.binary(3);
				Huffman code = ref<uint>(codeLengths);
				// Decodes litteral and distance trees
				uint lengths[HLITT+HDIST]; // Code lengths
				for(uint i=0; i<HLITT+HDIST;) {
					uint symbol = code.decode(s);
					if(symbol < 16) lengths[i++] = symbol;
					else if(symbol == 16) { // Repeats last
						uint last = lengths[i-1];
						for(int unused t: range(3+s.binary(2))) lengths[i++] = last;
					}
					else if(symbol == 17) for(int unused t: range(3+s.binary(3))) lengths[i++] = 0; // Repeats zeroes [3bit]
					else if(symbol == 18) for(int unused t: range(11+s.binary(7))) lengths[i++] = 0; // Repeats zeroes [3bit]
					else error(symbol);
				}
				literal = ref<uint>(lengths, HLITT);
				distance = ref<uint>(lengths+HLITT, HDIST);
			} else error("Reserved BTYPE", BTYPE);

			for(;;) {
				uint symbol = literal.decode(s);
				if(symbol < 256) target[targetIndex++] = symbol; // Literal
				else if(symbol == 256) break; // Block end
				else if(symbol < 286) { // Length-distance
					uint length = 0;
					if(symbol==285) length = 258;
					else {
						uint code[7] = {257, 265, 269, 273, 277, 281, 285};
						uint lengths[6] = {3, 11, 19, 35, 67, 131};
						for(int i: range(6)) if(symbol < code[i+1]) { length = ((symbol-code[i])<<i)+lengths[i]+(i?s.binary(i):0); break; }
					}
					uint distanceSymbol = distance.decode(s);
					uint code[15] = {0, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
					uint lengths[14] = {1, 5, 9, 17, 33, 65, 129, 257, 513, 1025, 2049, 4097, 8193, 16385};
					uint distance = 0; for(int i: range(14)) if(distanceSymbol < code[i+1]) { distance = ((distanceSymbol-code[i])<<i)+lengths[i]+(i?s.binary(i):0); break; }
					for(size_t i : range(length)) target[targetIndex+i] = target[targetIndex-distance+i]; // length may be larger than distance
					targetIndex += length;
				} else error(symbol);
			}
		}
		if(BFINAL) break;
	}
	return move(target);
}

struct LocalHeader {
	byte signature[4] = {'P','K', 3, 4}; // Local file header signature
	uint16 features; // Version needed to extract
	uint16 flag; // General purpose bit flag
	uint16 compression; // Compression method
	uint16 modifiedTime; // Last modified file time
	uint16 modifiedDate; // Last modified file date
	uint32 crc; // CRC-32
	uint32 compressedSize;
	uint32 size; // Uncompressed size
	uint16 nameLength; // File name length
	uint16 extraLength; // Extra field length
	// File name
	// Extra field
} packed;

struct DataDescriptor {
	uint32 crc; // CRC-32
	uint32 compressedSize;
	uint32 size; // Uncompressed size
} packed;

struct FileHeader {
	byte signature[4] = {'P','K', 1, 2}; // Central file header signature
	uint16 zipVersion; // Version made by
	uint16 features; // Version needed to extract
	uint16 flag; // General purpose bit flag
	uint16 compression; // Compression method
	uint16 modifiedTime; // Last modified file time
	uint16 modifiedDate; // Last modified file date
	uint32 crc; // CRC-32
	uint32 compressedSize;
	uint32 size; // Uncompressed size
	uint16 nameLength; // File name length
	uint16 extraLength; // Extra field length
	uint16 commentLength; // File comment length
	uint16 disk; // Disk number start
	uint16 attributes; // Internal file attributes
	uint32 externalAttributes; // External file attributes
	uint32 offset; // Relative offset of local header
	// File name
	// Extra field
	// File comment
} packed;

struct DirectoryEnd {
	byte signature[4] = {'P','K', 5, 6}; // End of central directory signature
	uint16 disk; // Number of this disk
	uint16 startDisk; // Number of the disk with the start of the central directory
	uint16 nofEntries; // Total number of entries in the central directory on this disk
	uint16 nofTotalEntries; // Number of entries in the central directory
	uint32 size; // Size of the central directory
	uint32 offset; // Offset of start of central directory with respect to the starting disk number
	uint16 commentLength; // .ZIP file comment length
} packed;

buffer<byte> extractZIPFile(ref<byte> zip, ref<byte> fileName) {
	for(int i=zip.size-sizeof(DirectoryEnd); i>=0; i--) {
		if(zip.slice(i, sizeof(DirectoryEnd::signature)) == ref<byte>(DirectoryEnd().signature, 4)) {
			const DirectoryEnd& directory = raw<DirectoryEnd>(zip.slice(i, sizeof(DirectoryEnd)));
			size_t offset = directory.offset;
			array<string> files;
			for(size_t unused entryIndex: range(directory.nofEntries)) {
				const FileHeader& header =  raw<FileHeader>(zip.slice(offset, sizeof(FileHeader)));
				string name = zip.slice(offset+sizeof(header), header.nameLength);
				if(name.last() != '/') {
					const LocalHeader& local = raw<LocalHeader>(zip.slice(header.offset, sizeof(LocalHeader)));
					ref<byte> compressed = zip.slice(header.offset+sizeof(local)+local.nameLength+local.extraLength, local.compressedSize);
					assert_(header.compression == 8);
					if(name == fileName) return inflate(compressed, buffer<byte>(local.size));
					files.append(name);
				}
				offset += sizeof(header)+header.nameLength+header.extraLength+header.commentLength;
			}
			error("No such file", fileName,"in",files);
			return {};
		}
	}
	error("Missing end of central directory signature");
	return {};
}

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
	int2 size = 0;
	ImageF(int2 size) : buffer(size.y*size.x), size(size) { assert(size>int2(0)); }
	inline float& operator()(size_t x, size_t y) const { assert(x<uint(size.x) && y<uint(size.y), x, y); return at(y*size.x+x); }
};

struct TriangleStrip : buffer<uint> {
	uint restart = 0xFFFFFFFF;
	uint last[2] = {restart, restart};
	bool even = true;

	TriangleStrip(uint capacity) : buffer<uint>(capacity, 0) {}
	void operator()(uint v0, uint v1, uint v2) {
		assert_(v0 < restart && v1 < restart && v2 < restart);
		if(last[0] == v0 && last[1] == v1) append(v2);
		else {
			if(size) append(restart);
			append(v0); append(v1); append(v2);
			even = true;
		}
		if(even) last[0] = v2, last[1] = v1; else last[0] = v0, last[1] = v2;
		even = !even;
	}
};

struct SRTM {
	Lock lock;
	function<void(int2)> changed;
	SRTM(function<void(int2)> changed) : changed(changed) {}
	static constexpr size_t W = 360/5/*72*/, H=24;
	struct Tile { Map map; Image16 image; } tiles[W*H] = {}; // 432000x144000 (120Gs), 13 GB, 872 images (60Gs)
	const int2 tileSize = 6000;
	static constexpr int D = 16; // /2⁴ = 27000x9000 (243Ms)
	Thread workerThread {0, true}; // Separate thread to decode images while main thread renders globe
	Job job {workerThread, [this]{
		Folder srtm("srtm");
		Folder cache("cache");
		for(size_t Y: range(H)) for(size_t X: range(W)) {
			String name = str(X, 2u)+"_"+str(Y, 2u);
			Map map = existsFile(name, cache)  ? Map(name, cache) : Map();
			if(map.size != size_t(tileSize.y/D)*(tileSize.x/D)*2) {
				if(!existsFile("srtm_"+name+".zip", srtm)) continue; // Ocean
				Time time; time.start();
				buffer<byte> file = extractZIPFile(Map("srtm_"+name+".zip", srtm), "srtm_"+name+".tif");
				Image16 source = parseTIFF(file); // WARNING: unsafe weak reference to file
				if(source.size != tileSize && source.size != tileSize+int2(1)) error(tileSize, source.size); // first/last points are identical // FIXME

				map = Map(File(name+".part"_, cache, Flags(ReadWrite|Create)).resize((tileSize.y/D)*(tileSize.x/D)*2), Map::Prot(Map::Read|Map::Write));
				Image16 target (cast<int16>(map), tileSize/D); // 375x375
				for(size_t y: range(tileSize.y/D)) for(size_t x: range(tileSize.x/D)) {
					int sum = 0; int N = 0;
					int16* s = &source(x*D, y*D);
					size_t stride = tileSize.y;
					for(size_t dy: range(D)) {
						int16* sY = s + dy * stride;
						for(size_t dx: range(D)) {
							int16 v = sY[dx];
							if(v>-500) N++, sum += v;
						}
					}
					target(x, y) = N ? sum / N : -32768;
				}
				rename(name+".part"_, name, cache);
			}
			Image16 target (cast<int16>(map), tileSize/D); // 375x375
			{Locker lock(this->lock); tiles[Y*W+X] = {move(map), move(target)};}
			changed(int2(X, Y));
		}
	}};
	int2 size() const { return int2(W,H)*tileSize/D; }
	int16 operator ()(size_t Xx, size_t Yy) {
		size_t X = Xx / (tileSize.x/D);
		size_t Y = Yy / (tileSize.y/D);
		if(!tiles[Y*W+X].image) return -32768;
		size_t x = Xx % (tileSize.x/D);
		size_t y = Yy % (tileSize.y/D);
		Locker lock(this->lock);
		assert_(Y < H && X < W && x<size_t(tileSize.x/D) && ((tileSize.y/D-1)-y)<size_t(tileSize.y/D), X, Y, x, y);
		return tiles[Y*W+X].image(x, y);
	}
};

String str(range r) { return str(r.start, r.stop); }
struct Globe : Image16 {
	static constexpr size_t W =4, H=4;
	Globe() : Image16(int2(4*10800, 6000+4800+4800+6000))/*890 Ms*/ {
		int y0 = 0;
		for(size_t Y: range(H)) {
			const int h = (Y==0 || Y==H-1) ? 4800 : 6000;
			for(size_t X: range(W)) {
				buffer<int16> raw= cast<int16>(extractZIPFile(Map("globe.zip"), "all10/"_+char('a'+Y*W+X)+"10g"_));
				assert_(raw.size%10800==0);
				Image16 source (raw, int2(10800, raw.size/10800)); // WARNING: unsafe weak reference to file
				assert_(source.size.y == h);
				int16* target = &at(y0*size.x+X*10800);
				for(size_t y: range(source.size.y)) for(size_t x: range(source.size.x)) target[y*size.x+x] = source[y*source.size.x+x];
				log(X, Y, source.size);
			}
			y0 += h;
		}
		//size_t unary = 0;
		int predictor = 0;
		//range valueRange = 0, residualRange = 0;
		uint valueHistogram[16384] = {}, residualHistogram[16384] = {};
		for(size_t y: range(size.y)) {
			for(size_t x: range(size.x)) {
				int value = 500+at(y*size.x + x);
				assert_(value >= 0 && value <= 500+8752,value);
				valueHistogram[value]++;
				//valueRange.start = min(valueRange.start, value), valueRange.stop = max(valueRange.stop, value);
				int residual = value - predictor;
				assert_(residual >= -2862 && residual <= 3232);
				residualHistogram[4096+residual]++;
				//residualRange.start = min(residualRange.start, residual), residualRange.stop = max(residualRange.stop, residual);
				predictor = value;
				//unary += 1+abs(residual)+(residual<0);
			}
		}
		writeFile("valueHistogram", cast<byte>(ref<uint>(valueHistogram)));
		writeFile("residualHistogram", cast<byte>(ref<uint>(residualHistogram)));
		//log(valueRange, residualRange, unary/(1024.*1024), (float)(16*4*10800*(2*6000+2*4800))/unary, unary/2575691952.);
	}
};

struct Terrain {
	function<void()> changed;
	bool needUpdate = true; // FIXME: progressive
	Globe globe;
	//SRTM srtm;
	GLShader shader {::shader(), {"terrain"}};
	GLBuffer elevation;
	GLVertexArray vertexArray;
	GLTexture textureBuffer; // shader read access (texelFetch) to elevation buffer
	GLIndexBuffer indexBuffer;

	Terrain(function<void()> changed) : changed(changed)/*, srtm([this](int2){ needUpdate=true; this->changed(); })*/ {}

	void update() {
		int maxZ = 0;
		static constexpr int D = 8; // /2⁴/2³ = 3375x1125
		ImageF elevation (globe.size/D); // int16 stutters //TODO: map //FIXME: share first/last line of each tile
		struct TriangleStrip triangleStrip (elevation.Ref::size*6);
		for(int y: range(elevation.size.y)) for(int x: range(elevation.size.x)) {
			int z = globe(x*D, y*D);
			elevation(x, y) = z;
			maxZ = ::max(maxZ, z);
		}
		//log(maxZ); 5594
		for(int y: range(elevation.size.y-1)) for(int x: range(elevation.size.x)) { // TODO: Z-order triangle strips for framebuffer locality ?
			int v00 = (y+0)*elevation.size.x+x, v10 = (y+0)*elevation.size.x+((x+1)%elevation.size.x);
			int v01 = (y+1)*elevation.size.x+x, v11 = (y+1)*elevation.size.x+((x+1)%elevation.size.x);
			if(elevation[v00]>-500 && elevation[v11]>-500) {
				if(elevation[v10]>-500) triangleStrip(v10, v00, v11);
				if(elevation[v01]>-500) triangleStrip(v11, v00, v01);
			}
		}
		if(!triangleStrip) return; // No data loaded yet
		indexBuffer = triangleStrip;
		this->elevation = GLBuffer(elevation);
		textureBuffer = GLTexture(this->elevation, elevation.size);
		vertexArray = GLVertexArray();
		vertexArray.bindAttribute(shader.attribLocation("aElevation"_), 1, Float, this->elevation);
		needUpdate = false;
	}

	void draw(const mat4 viewProjection) {
		if(needUpdate) update();
		if(!indexBuffer.id) return; // No data loaded yet
		shader.bind();
		shader["W"_] = textureBuffer.size.x;
		shader["dx"_] = float(2*PI/textureBuffer.size.x);
		shader["dy"_] = float(2*PI/(/*3*/2*(textureBuffer.size.y-1)));
		static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
		shader["R"_] = R;
		shader["tElevation"_] = 0;
		textureBuffer.bind(0);
		shader["modelViewProjectionTransform"_] = mat4(viewProjection); //.scale(vec3(vec2(dx*D),1)); //.translate(vec3(vec2(N/D*tile.tileIndex),0));
		vertexArray.bind();
		indexBuffer.draw(0);
	}
};

/// Views a scene
struct View : Widget {
	Window window {this, 896 /*%128==0*/, []{ return "Editor"__; }};
	Terrain terrain {[this]{ window.render(); }};

	// View
	vec2 lastPos; // Last cursor position to compute relative mouse movements
	vec2 rotation = vec2(0, 0); // Current view angles (longitude, latitude)
	float scale = 1;

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) {
			 rotation += float(2.f*PI) * delta / size;
			 rotation.y = clip<float>(-PI, rotation.y, PI);
		 }
		 else if(event==Press && button==WheelUp) scale = min(2., scale * pow(2,1./16));
		 else if(event==Press && button==WheelDown) scale = max(1., scale / pow(2,1./16));
		 else return false;
		 return true;
	}

	vec2 sizeHint(vec2) override { return 0; }
	View() {
		glDepthTest(true);
		glCullFace(true);
	}
	shared<Graphics> graphics(vec2 unused size) override {
		mat4 view = mat4()
				.scale(scale)
				.translate(vec3(0,0,-1)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		terrain.draw(view);
		return nullptr;
	}
} app;
