#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "tiff.h"
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
		assert_(index < bitSize);
		uint8 bit = uint8(uint8(data[index/8])<<(7-(index&7)))>>7;
		index++;
		return bit;
	}
	/// Reads \a size bits
	uint binary(uint size) {
		assert_(size > 0 && size < 32 && index+size <= bitSize);
		assert(size <= 32 && index < bitSize);
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
				assert_(count[0] == 0, count, lengths);
				{int sum = 0; for(size_t i: range(16)) { offsets[i] = sum; sum += count[i]; }}
				// Evaluates code -> symbol translation table
				for(size_t symbol: range(lengths.size)) if(lengths[symbol]) lookup[offsets[lengths[symbol]]++] = symbol;
			}
			uint decode(BitReader& s) {
				assert_(s.index < s.bitSize);
				int code=0; uint length=0, sum=0;
				do { // gets more bits while code value is above sum
					code <<= 1;
					code |= s.bit();
					length += 1;
					assert_(length < 16, length);
					sum += count[length];
					code -= count[length];
					assert_(sum + code >= 0 && sum + code < 288);
				} while (code >= 0);
				return lookup[sum + code];
			}
		};
		Huffman literal;
		Huffman distance;

		bool BFINAL = s.bit();
		int BTYPE = s.binary(2);
		log(ref<string>{"literal", "static", "dynamic", "reserved"}[BTYPE]);
		if(BTYPE==0) { // Literal
			s.index=align(8, s.index);
			uint16 length = raw<uint16>(s.slice(s.index/8, 2)); s.index+=16;
			uint16 unused nlength = raw<uint16>(s.slice(s.index/8, 2)); s.index+=16;
			assert_(length && ~length == nlength);
			target.append(s.slice(s.index/8, length));
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
						assert_(i>0);
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
				assert_(s.index < s.bitSize, s.index, s.bitSize);
				uint symbol = literal.decode(s);
				if(symbol < 256) { assert_(targetIndex < target.size, targetIndex, target.size); target[targetIndex++] = symbol; } // Literal
				else if(symbol == 256) break; // Block end
				else if(symbol < 286) { // Length-distance
					uint length = 0;
					if(symbol==285) length = 258;
					else {
						uint code[7] = {257, 265, 269, 273, 277, 281, 285};
						uint lengths[6] = {3, 11, 19, 35, 67, 131};
						for(int i: range(6)) if(symbol < code[i+1]) { length = ((symbol-code[i])<<i)+lengths[i]+(i?s.binary(i):0); break; }
						assert_(length);
					}
					uint distanceSymbol = distance.decode(s);
					uint code[15] = {0, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
					uint lengths[14] = {1, 5, 9, 17, 33, 65, 129, 257, 513, 1025, 2049, 4097, 8193, 16385};
					uint distance = 0; for(int i: range(14)) if(distanceSymbol < code[i+1]) { distance = ((distanceSymbol-code[i])<<i)+lengths[i]+(i?s.binary(i):0); break; } assert_(distance);
					assert_(targetIndex+length <= target.size && distance <= targetIndex, symbol, length, distanceSymbol, distance, targetIndex);
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
			for(size_t unused entryIndex: range(directory.nofEntries)) {
				const FileHeader& header =  raw<FileHeader>(zip.slice(offset, sizeof(FileHeader)));
				assert_(ref<byte>(header.signature, 4) == ref<byte>(FileHeader().signature, 4));
				string name = zip.slice(offset+sizeof(header), header.nameLength);
				if(name.last() != '/') {
					const LocalHeader& local = raw<LocalHeader>(zip.slice(header.offset, sizeof(LocalHeader)));
					assert_(ref<byte>(local.signature, 4) == ref<byte>(LocalHeader().signature, 4));
					ref<byte> compressed = zip.slice(header.offset+sizeof(local)+local.nameLength+local.extraLength, local.compressedSize);
					assert_(header.compression == 8);
					if(name == fileName) return inflate(compressed, buffer<byte>(local.size));
				}
				offset += sizeof(header)+header.nameLength+header.extraLength+header.commentLength;
			}
			error("No such file", fileName);
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
	static constexpr int W = 360/5/*01-72*/, H=24 /*01-24*/;
	struct { Map map; Image16 image; } tiles[W*H]; // 432000x144000 (120Gs), 13 GB, 872 images (60Gs)
	int2 tileSize = 6000;
	SRTM() {
		for(size_t y: range(H)) for(size_t x: range(W)) {
			String name = "srtm_"+str(y,2u)+"_"+str(x,2u);
			Image16 image;
			if(existsFile(name+".tif")) {
				tiles[y*W+x].map = Map(name+".tif");
				image = parseTIFF(unsafeRef(tiles[y*W+x].map)); // Weak reference to map
			} else if(existsFile(name+".zip")) {
				// TODO: progressive
				image = parseTIFF(extractZIPFile(Map(name+".zip"), name+".tif"));
			} else continue; // Ocean
			assert_(image.size == tileSize+int2(1)); // first/last points are identical
			tiles[y*W+x].image = move(image);
		}
	}
	int2 size() const { return int2(W,H)*tileSize; }
	int16 operator ()(size_t Xx, size_t Yy) const {
		size_t X = Xx / tileSize.x;
		size_t Y = Yy / tileSize.y;
		if(!tiles[Y*W+X].image) return -32768;
		size_t x = Xx % tileSize.x;
		size_t y = Yy % tileSize.y;
		return tiles[Y*W+X].image(x, (tileSize.y-1-1)-y);
	}
};

struct Terrain {
	GLShader shader {::shader(), {"terrain"}};
	GLBuffer elevation;
	GLVertexArray vertexArray;
	GLTexture textureBuffer;// {elevation}; // shader read access (texelFetch) to elevation buffer*/
	GLIndexBuffer indexBuffer;
	static constexpr int D = 128; // 3375x1125

	Terrain() {
		int maxZ = 0;
		SRTM srtm;
		ImageF elevation (srtm.size()/D); // int16 stutters //TODO: map //FIXME: share first/last line of each tile
		struct TriangleStrip triangleStrip (elevation.Ref::size*6);
		for(int y: range(elevation.size.y)) for(int x: range(elevation.size.x)) {
			int z = srtm(x*D, y*D);
			elevation(x, y) = z;
			maxZ = ::max(maxZ, z);
		}
		for(int y: range(elevation.size.y-1)) for(int x: range(elevation.size.x)) { // TODO: Z-order triangle strips for framebuffer locality ?
			int v00 = (y+0)*elevation.size.x+x, v10 = (y+0)*elevation.size.x+(x+1)%elevation.size.x;
			int v01 = (y+1)*elevation.size.x+x, v11 = (y+1)*elevation.size.x+(x+1)%elevation.size.x;
			if(elevation[v00]>-32768 && elevation[v11]>-32768) {
				if(elevation[v01]>-32768) triangleStrip(v01, v00, v11);
				if(elevation[v10]>-32768) triangleStrip(v11, v00, v10);
			}
		}
		indexBuffer = triangleStrip;
		this->elevation = GLBuffer(elevation);
		textureBuffer = GLTexture(this->elevation, elevation.size);
		vertexArray.bindAttribute(shader.attribLocation("aElevation"), 1, Float, this->elevation);
	}

	void draw(const mat4 viewProjection) {
		shader.bind();
		shader["W"_] = textureBuffer.size.x;
		shader["da"_] = float(2*PI/textureBuffer.size.x);
		static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
		shader["R"_] = R;
		shader["tElevation"_] = 0;
		textureBuffer.bind(0);
		shader["modelViewProjectionTransform"_] = mat4(viewProjection); //.scale(vec3(vec2(dx*D),1)); //.translate(vec3(vec2(N/D*tile.tileIndex),0));
		indexBuffer.draw(0);
	}
};

/// Views a scene
struct View : Widget {
	Window window {this, 896 /*%128==0*/, []{ return "Editor"__; }};
	Terrain terrain;

	// View
	vec2 lastPos; // Last cursor position to compute relative mouse movements
	vec2 rotation = vec2(0, 0); // Current view angles (longitude, latitude)

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) rotation += float(2.f*PI) * delta / size;
		 else return false;
		 return true;
	}

	vec2 sizeHint(vec2) override { return 0; }
	View() {
		glDepthTest(true);
		//glCullFace(true);
	}
	shared<Graphics> graphics(vec2 unused size) override {
		mat4 view = mat4()
				.translate(vec3(0,0,-1/**R*/)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		terrain.draw(view);
		return nullptr;
	}
} app;
