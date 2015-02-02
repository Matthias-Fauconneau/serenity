#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "tiff.h"
#include "time.h"
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
	Window window {this, 1024, []{ return "Editor"__; }};
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
