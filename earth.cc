#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "zip.h"
#include "tiff.h"
#include "flic.h"
#include "time.h"
FILE(shader)

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
	int2 size = 0;
	ImageF(int2 size) : buffer((size_t)size.y*size.x), size(size) { assert(size>int2(0)); }
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

struct Terrain {
	GLShader shader {::shader(), {"terrain"}};

	struct Tile {
		int level; int2 index, size /*without +1 repeated row/column*/;
		struct Range { int min = 0, max= 0; } range;
		buffer<float> firstRowColumn; // last row/column of previous tile
		GLBuffer elevation;
		bool loaded = false;
		GLVertexArray vertexArray;
		GLTexture textureBuffer; // shader read access (texelFetch) to elevation buffer
		GLIndexBuffer indexBuffer;
	};
	array<unique<Tile>> tiles;

	Tile& tile(int level, int2 index) {
		index.x = index.x%(2*(1<<level)); // Wraps longitude
		assert_(0 <= index.x && index.x < 2*(1<<level));
		assert_(0 <= index.y && index.y < 1*(1<<level), index.y);
		for(Tile& tile: tiles) if(tile.level == level && tile.index==index) return tile; // TODO: correct mixed resolution stitch
		// Decodes requested tile
		string name = 0 ? "dem15"_ : "globe30"_;
		Map map(str(index.x, 2u,'0')+","+str(index.y,2u,'0'), Folder(name+"."+str(1<<level)+".eg2rle6"));
		FLIC source(map);
		ImageF elevation (source.size+int2(1));
		buffer<float> firstRowColumn (source.size.x+source.size.y);
		Tile::Range zRange;
		for(int y: range(source.size.y)) {
			int16 line[source.size.x];
			source.read(mref<int16>(line, source.size.x));
			for(int x: range(source.size.x)) {
				int z = line[x];
				elevation(x, y) = z;
				if(y==0) firstRowColumn[x] = elevation(x, 0);
				zRange.min = ::min(zRange.min, z);
				zRange.max = ::max(zRange.max, z);
			}
			firstRowColumn[source.size.x+y] = elevation(0, y);
		}
		log(index, zRange.min, zRange.max);
		return tiles.append(unique<Tile>(level, index, source.size, zRange, move(firstRowColumn), GLBuffer(elevation), false,
							GLVertexArray(), GLTexture(), GLIndexBuffer()));
	}

	void load(int level, int2 index) {
		Tile& tile = this->tile(level, int2(index.x, index.y));
		if(tile.loaded) return;
		// Completes last row/column with first row/column of next neighbours
		auto elevation = tile.elevation.map<float>();

		if(index.y+1 < (1<<level)) {
			Tile& nextY = this->tile(level, int2(index.x, index.y+1));
			for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = nextY.firstRowColumn[x];
		} else {
			log(index.x, index.y, tile.level, tile.index, tile.size);
			for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = 0; // Pole
		}

		Tile& nextX = this->tile(level, int2(index.x+1, index.y+0));
		for(size_t y: range(tile.size.y)) elevation[y*(tile.size.x+1)+tile.size.x] = nextX.firstRowColumn[tile.size.x+y];

		if(index.y+1 < (1<<level)) {
			Tile& nextXY = this->tile(level, int2(index.x+1, index.y+1));
			elevation[tile.size.y*(tile.size.x+1)+tile.size.x] = nextXY.firstRowColumn[0];
		} else {
			elevation[tile.size.y*(tile.size.x+1)+tile.size.x] = 0; // Pole
		}

		struct TriangleStrip triangleStrip (tile.size.x*tile.size.y*6);
		for(int y: range(tile.size.y)) for(int x: range(tile.size.x)) { // TODO: Z-order triangle strips for framebuffer locality ?
			int v00 = (y+0)*(tile.size.x+1)+(x+0), v10 = (y+0)*(tile.size.x+1)+(x+1);
			int v01 = (y+1)*(tile.size.x+1)+(x+0), v11 = (y+1)*(tile.size.x+1)+(x+1);
			const int invalid = tile.range.min;
			if(elevation[v00]!=invalid && elevation[v11]!=invalid) {
				if(elevation[v00]!=invalid) triangleStrip(v10, v00, v11);
				if(elevation[v01]!=invalid) triangleStrip(v11, v00, v01);
			}
		}
		if(triangleStrip) { // Not a void tile
			tile.indexBuffer = triangleStrip;
			tile.textureBuffer = GLTexture(tile.elevation, tile.size+int2(1));
			tile.vertexArray = GLVertexArray();
			tile.vertexArray.bindAttribute(shader.attribLocation("aElevation"_), 1, Float, tile.elevation);
		}
	}

	Terrain() { for(uint Y: range(1)) for(uint X: range(2)) load(0, int2(X, Y)); } // Loads mipmap level 0 (2x1 tiles)

	void draw(const mat4 viewProjection, vec2 size) {
		shader.bind();
		int splitCount = 0;
		for(size_t tileIndex=0; tileIndex<tiles.size;) { // Not for(tiles) as 'tiles' is edited within
			Tile& tile = tiles[tileIndex];
			if(!tile.indexBuffer) { tileIndex++; continue; } // Void tile

			float angularSize = float(PI/(1<<tile.level));
			float angularResolution = angularSize/tile.size.x;
			vec2 originAngles = float(PI/(1<<tile.level))*vec2(tile.index);
			{ // Estimates maximum cell size
				vec2 centerAngles = originAngles+vec2(angularSize/2);
				auto sphere = [](vec2 angles) { return vec3(sin(angles.y)*cos(angles.x), sin(angles.y)*sin(angles.x), cos(angles.y)); };
				// Center cell side lengths
				float dx = length(size*(viewProjection * sphere(centerAngles+vec2(angularResolution/2, 0)) - viewProjection * sphere(centerAngles-vec2(angularResolution/2, 0))).xy());
				float dy = length(size*(viewProjection * sphere(centerAngles+vec2(0, angularResolution/2)) - viewProjection * sphere(centerAngles-vec2(0, angularResolution/2))).xy());
				float d = max(dx, dy);
				log(d);
				if(d > 4 && level<5 && splitCount==0) { // Splits up to one tile per frame (TODO: async)
					int level = tile.level;
					int2 index = tile.index;
					tiles.removeAt(tileIndex); // Removes parent tile
					// Loads next mipmap level
					for(int Y: range(2)) for(int X: range(2)) load(level+1, index*2+int2(X,Y));
					splitCount++;
					continue;
				}
			}
			shader["W"_] = tile.size.x+1;
			assert_(tile.size.x == tile.size.y);
			shader["angularResolution"_] = angularResolution;
			shader["originAngles"_] = originAngles;
			static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
			shader["R"_] = R;
			shader["tElevation"_] = 0;
			tile.textureBuffer.bind(0);
			shader["modelViewProjectionTransform"_] = mat4(viewProjection); //.scale(vec3(vec2(dx*D),1)); //.translate(vec3(vec2(N/D*tile.tileIndex),0));
			tile.vertexArray.bind();
			tile.indexBuffer.draw();
			tileIndex++;
		}
	}
};

/// Views a scene
struct View : Widget {
	Window window {this, 1024, []{ return "Editor"__; }};
	Terrain terrain;

	// View
	vec2 lastPos; // Last cursor position to compute relative mouse movements
	vec2 rotation = vec2(0, 0); // Current view angles (longitude, latitude)
	float altitude = 1;

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) {
			 rotation += delta / size / float(asin(1/(1+altitude)));
			 rotation.y = clip<float>(-PI, rotation.y, 0);
		 }
		 else if(event==Press && button==WheelUp) altitude = max(1./256, altitude / pow(2, 1./8));
		 else if(event==Press && button==WheelDown) altitude = min(1., altitude * pow(2, 1./8));
		 else return false;
		 return true;
	}

	vec2 sizeHint(vec2) override { return 0; }
	View() {
		glDepthTest(true);
		glCullFace(true);
	}
	shared<Graphics> graphics(vec2 unused size) override {
		mat4 projection = mat4().perspective(2*asin(1/(1+altitude)), size, altitude-1./512 /*maximum elevation*/, altitude+1);
		mat4 view = mat4()
				.translate(vec3(0,0,-altitude-1)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		mat4 viewProjection = projection*view;
		terrain.draw(viewProjection, size);
		return nullptr;
	}
} app;
