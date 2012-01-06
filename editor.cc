#include "process.h"
#include "interface.h"
#include "gl.h"
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"

/// Random

inline uint64 rdtsc() { uint hi, lo; asm volatile("rdtsc":"=a"(lo),"=d"(hi)); return uint64(lo)|(uint64(hi)<<32); }
struct Random {
	uint64 sz,sw;
	uint64 z,w;
	Random() { seed(); reset(); }
	void seed() { sz=rdtsc(); sw=rdtsc(); }
	void reset() { z=sz; w=sw; }
	uint64 next() {
		z = 36969 * (z & 65535) + (z >> 16);
		w = 18000 * (w & 65535) + (w >> 16);
		return (z << 16) + w;
	}
} random_generator;
const uint uint_max = (1l<<(sizeof(uint)*8))-1;
#define random ({ float(uint(random_generator.next()))/uint_max; })

/// Stream

struct Vertex { vec3 position; vec3 normal; Vertex(vec3 position, vec3 normal):position(position),normal(normal){}};
struct VertexStream {
	Vertex* data; int count=0;
	vec3 bbMin,bbMax;
	VertexStream(void* data) : data((Vertex*)data) {}
	VertexStream& operator <<(const Vertex& v) { if(count==0)bbMin=bbMax=v.position; bbMin=min(bbMin,v.position); bbMax=max(bbMax,v.position); data[count++]=v; return *this; }
};

struct IndexStream {
	uint* data; int count=0;
	IndexStream(void* data) : data((uint*)data) {}
	IndexStream& operator <<(const uint& v) { data[count++]=v; return *this; }
};

/// Object

struct Object {
	GLBuffer buffer;
	GLShader shader{"lambert"};
	vec3 bbMin,bbMax;
	void render(const mat4& view) {
		shader.bind(); shader["view"]=view; shader["color"]=vec4(1,1,1,1);
		buffer.bindAttribute(shader,"position",3,offsetof(Vertex,position));
		buffer.bindAttribute(shader,"normal",3,offsetof(Vertex,normal));
		buffer.draw();
	}
};

struct Plane : Object {
	Plane() {
		buffer.allocate(4, 4, sizeof(Vertex));
		VertexStream vertices = buffer.mapVertexBuffer();
		vertices << Vertex(vec3(-1,-1,0), vec3(0,0,1))
				 << Vertex(vec3(-1, 1,0), vec3(0,0,1))
				 << Vertex(vec3( 1, 1,0), vec3(0,0,1))
				 << Vertex(vec3( 1,-1,0), vec3(0,0,1));
		bbMin=vertices.bbMin, bbMax=vertices.bbMax;
		buffer.unmapVertexBuffer();
		buffer.primitiveType = 5;
	}
};

struct Editor : Application, Widget {
	Window window = Window(int2(1024,768),*this);

	vec2 pos;
	float rotateX=PI/4,rotateZ=-PI/4;
	float zoom=4;

	array<Object> world;

	void start(array<string>&&) {
		window.render();
		//window.render(); //?
	}
	void render(vec2, vec2) {
		mat4 view;
		view.scale(vec3(zoom/(size.x/2),zoom/(size.y/2),-zoom/size.y));
		view.rotateX(rotateZ);
		view.rotateZ(rotateX);
		view.translate(vec3(-pos.x,-pos.y,0));
		for(Object object: world) object.render(view);
	}

	int2 lastPos;
	bool event(int2 pos, int event, int state) {
		int2 delta = pos-lastPos; lastPos=pos;
		if(event==Motion && state==Pressed) { rotateX += delta.x*PI/size.x; return true; }
		if(event==WheelDown && state==Pressed) { zoom *= 17.0/16; update(); return true; }
		if(event==WheelUp && state==Pressed) { zoom *= 15.0/16;return true; }
		return false;
	}
	/*void View::timerEvent(QTimerEvent*) {
		if(window.cursor.x <= 0) pos -= view[0]/zoom/16;
		if(window.cursor.x >= Screen::width-1 ) pos += view[0]/zoom/16;
		if(window.cursor.y <= 0) pos -= view[1]/zoom/16;
		if(window.cursor.y >= Screen::height-1 ) pos += view[1]/zoom/16;
	}*/
} editor;
