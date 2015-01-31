#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "tiff.h"
FILE(shader)

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
	struct { Map map; Image16 image; } tiles[W*H]; // 1728 images
	int2 tileSize = 6000;
	SRTM() {
		for(size_t y: range(H)) for(size_t x: range(W)) {
			String name = "srtm_"+str(y,2)+"_"+str(x,2)+".tif";
			if(!existsFile(name)) continue;
			tiles[y*W+x].map = Map(name);
			Image16 image = parseTIFF(tiles[y*W+x].map);
			assert_(image.size == tileSize+int2(1)); // first/last points are identical
			tiles[y*W+x].image = move(image);
		}
	}
	int2 size() const { return int2(W,H)*tileSize; } // 422 Ks x 140 Ks ~ 60 Gs ~ 20 GB
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
			static constexpr float R = 4E7/(2*PI); // 4·10⁷/2π  ~ 6.37
			elevation(x, y) = z / R;
			maxZ = ::max(maxZ, z);
		}
		for(int y: range(elevation.size.y-1)) for(int x: range(elevation.size.x-1)) { // TODO: Z-order triangle strips for framebuffer locality ?
			int v00 = (y+0)*elevation.size.x+x, v10 = (y+0)*elevation.size.x+(x+1);
			int v01 = (y+1)*elevation.size.x+x, v11 = (y+1)*elevation.size.x+(x+1);
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
		//shader["tElevation"_] = 0;
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
				.translate(vec3(0,0,-2/**R*/)) // Altitude
				.rotateX(rotation.y) // Latitude
				.rotateZ(rotation.x) // Longitude
				;
		terrain.draw(view);
		return nullptr;
	}
} app;
