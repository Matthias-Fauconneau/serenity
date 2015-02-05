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
		GLVertexArray vertexArray;
		GLTexture textureBuffer; // shader read access (texelFetch) to elevation buffer
		GLIndexBuffer indexBuffer;
	};
	array<unique<Tile>> tiles;

	Tile& tile(int level, int2 index) {
		index.x = index.x%(2*(1<<level)); // Wraps longitude
		assert_(0 <= index.x && index.x < 2*(1<<level));
		assert_(0 <= index.y && index.y < 1*(1<<level), index.y);
		for(Tile& tile: tiles) {
			if(tile.index==index) {
				assert_(tile.level==level); // TODO: mixed resolution
				return tile;
			}
		}
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
		return tiles.append(unique<Tile>(level, index, source.size, zRange, move(firstRowColumn), GLBuffer(elevation),
							GLVertexArray(), GLTexture(), GLIndexBuffer()));
	}

	// Loads single uniform mipmap level
	Terrain() {
		uint level = 0;
		for(uint Y: range(1*(1<<level))) {
			for(uint X: range(2*(1<<level))) {
				Tile& tile = this->tile(level, int2(X, Y));
				// Completes last row/column with first row/column of next neighbours
				auto elevation = tile.elevation.map<float>();

				if(Y+1 < (1<<level)) {
					Tile& nextY = this->tile(level, int2(X, Y+1));
					for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = nextY.firstRowColumn[x];
				} else {
					log(X, Y, tile.level, tile.index, tile.size);
					for(size_t x: range(tile.size.x)) elevation[(size_t)tile.size.y*(tile.size.x+1)+x] = 0; // Pole
				}

				Tile& nextX = this->tile(level, int2(X+1, Y+0));
				for(size_t y: range(tile.size.y)) elevation[y*(tile.size.x+1)+tile.size.x] = nextX.firstRowColumn[tile.size.x+y];

				if(Y+1 < (1<<level)) {
					Tile& nextXY = this->tile(level, int2(X+1, Y+1));
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
		}
	}

	void draw(const mat4 viewProjection) {
		shader.bind();
		for(Tile& tile: tiles) {
			if(!tile.indexBuffer) continue; // Void tile
			shader["W"_] = tile.size.x+1;
			assert_(tile.size.x == tile.size.y);
			shader["da"_] = float(PI/tile.size.x/(1<<tile.level));
			shader["origin"_] = float(PI/(1<<tile.level))*vec2(tile.index);
			static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
			shader["R"_] = R;
			shader["tElevation"_] = 0;
			tile.textureBuffer.bind(0);
			shader["modelViewProjectionTransform"_] = mat4(viewProjection); //.scale(vec3(vec2(dx*D),1)); //.translate(vec3(vec2(N/D*tile.tileIndex),0));
			tile.vertexArray.bind();
			tile.indexBuffer.draw();
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
	//float scale = 1;
	float altitude = 1;

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) {
			 rotation += float(2.f*PI) * delta / size;// / scale;
			 rotation.y = clip<float>(-PI, rotation.y, 0);
		 }
		 else if(event==Press && button==WheelUp) altitude = /*max(1./8,*/ altitude / pow(2,1./4); //);
		 else if(event==Press && button==WheelDown) altitude = min(1., altitude * pow(2,1./4));
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
				//.scale(scale)
				.translate(vec3(0,0,-altitude-1)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		mat4 viewProjection = projection*view;
		terrain.draw(viewProjection);
		return nullptr;
	}
} app;
