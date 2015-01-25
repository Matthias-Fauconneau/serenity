#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "time.h"
FILE(shader)

struct Vertex {
	vec3 position; // World-space position
	vec3 color; // BGR albedo (TODO: texture mapping)
	vec3 normal; // World-space vertex normals
};
String str(const Vertex& o) { return str(o.position); }

/// Polygons automatically sharing vertices with averaged normal attribute
struct Surface {
	array<Vertex> vertices;
	array<uint> indices;

	/// Space partionning for faster vertex index lookups
	const int3 gridSize = 0;
	buffer<array<uint>> grid {size_t(gridSize.z*gridSize.y*gridSize.x?:1)};
	vec3 gridMin = 0, gridMax = 0; // Grid bounds
	array<uint>& cell(vec3 p) {
		if(!gridSize) return grid[0];
		assert(p>=gridMin && p<=gridMax, gridMin, p, gridMax);
		int3 indices = int3(vec3(gridSize-int3(1)) * (p-gridMin) / (gridMax-gridMin)); // Assigns vertices on maximum limit to last cell
		size_t index = indices.z*gridSize.y*gridSize.x + indices.y*gridSize.x + indices.x;
		assert_(index < grid.size, p, gridMin, gridMax);
		return grid[index];
	}

	Surface() { grid.clear(); }
	Surface(int3 gridSize, vec3 gridMin, vec3 gridMax) : gridSize(gridSize), gridMin(gridMin), gridMax(gridMax) { grid.clear(); }

	/// Creates a new face using existing vertices when possible
	/// \note Vertex normals are mean of adjacent face normals
	void face(const ref<Vertex> polygon) {
		assert(polygon.size>=3);
		uint indices[polygon.size];
		for(uint i: range(polygon.size)) { // Lookups each vertex
			const Vertex& o = polygon[i];

			float minDistance=0; uint minIndex = -1;
			array<uint>& cell = this->cell(o.position);
			for(uint index: cell) {
				Vertex& v = vertices[index];

				if(v.color!=o.color) continue;
				float distance = sq(v.position - o.position);
				if(distance>minDistance) continue;
				if(dot(o.normal,v.normal)<0) continue;
				minDistance = distance;
				minIndex = index;
			}
			if(minIndex != uint(-1)) {
				Vertex& v = vertices[minIndex];
				v.normal += o.normal; // FIXME: weighted contribution
				indices[i] = minIndex;
			} else {
				//if(vertices.size()==vertices.capacity()) vertices.reserve(2*vertices.size()); // Amortized allocation is now already default dynamic array allocation policy
				uint index = vertices.size;
				vertices.append( o );
				cell.append( index );
				indices[i] = index;
			}
		}
		// Appends polygon indices
		uint a = indices[0];
		uint b = indices[1];
		for(size_t i: range(2, polygon.size)) { // Tesselates convex polygons as fans // TODO: triangle strips with broad triangle optimization
			//if(this->indices.size()==this->indices.capacity()) this->indices.reserve(2*this->indices.size()); // Amortized allocation is now already default dynamic array allocation policy
			uint c = indices[i];
			this->indices.append( a );
			this->indices.append( b );
			this->indices.append( c );
			b = c;
		}
	}

	/// Creates a new face using existing vertices when possible
	/// \note Vertex normals are average of adjacent face normals
	// (vec3[] positions, color) overload
	void face(ref<vec3> polygon, vec3 color) {
		vec3 a = polygon[0];
		vec3 b = polygon[1];
		buffer<Vertex> vertices (polygon.size);
		vertices[0]={a,color,0}, vertices[1]={b,color,0};
		for(uint i: range(2,polygon.size)) { // Fan
			vec3 c = polygon[i];
			vec3 surface = cross(b-a, c-a); // Weights contribution to vertex normal by face area
			vertices[i-2].normal += surface;
			vertices[i-1].normal += surface;
			vertices[i] = {c,color,surface};
			b = c;
		}
		face(vertices);
	}
	template<uint N> void face(const vec3 (&polygon)[N], vec3 color) { face(ref<vec3>(polygon), color); }
};

static float lerp(float a, float b, float t) { return (1-t)*a + t*b; }
static float bilinear(float* image, uint width, uint height, vec2 uv) {
	assert_(uv.x >= 0 && uv.x <= 1 && uv.y >=0 && uv.y <= 1, uv);
	uv *= vec2(width-1, height-1);
	int2 i = int2(uv);
	vec2 f = uv-floor(uv);
	return lerp(
				lerp( image[(i.y+0)*width+i.x], image[(i.y+0)*width+i.x+1], f.x),
				lerp( image[(i.y+1)*width+i.x], image[(i.y+1)*width+i.x+1], f.x), f.y);
};

vec2 randomNormals[256*256];
int modulo(int x, int y) { return (x  - x/y*y + y)%y; }
vec2 random2D(int2 i) {
	static unused bool init = ({Random random; for(vec2& v: randomNormals) v = normalize(vec2(random()*2-1,random()*2-1)); true; });
	return randomNormals[modulo(i.y,256)*256+modulo(i.x,256)];
}
float perlin2D(vec2 p) {
	int2 i = int2(p);
	vec2 f = p-floor(p);
	float v00 = dot(random2D(i+int2(0,0)), f - vec2(0,0));
	float v01 = dot(random2D(i+int2(0,1)), f - vec2(0,1));
	float v10 = dot(random2D(i+int2(1,0)), f - vec2(1,0));
	float v11 = dot(random2D(i+int2(1,1)), f - vec2(1,1));
	vec2 w = f*f*f*(f*(6.f*f-vec2(15.f))+vec2(10.f));
	return lerp( lerp( v00, v01, w.y ), lerp( v10, v11, w.y ), w.x );
}
float ridgedMultifractal(const vec2 p0) {
	const float H = 1;
	const float lacunarity = 3;
	const float gain = 2;
	float frequency = 1;
	vec2 p = p0;
	float offset = 3./4 + perlin2D(p)/2;
	float signal = sq(offset - abs(perlin2D(p)));
	float r = signal;
	for(int unused i : range(1,8)) {
		p *= lacunarity;
		signal = clip(0.f, signal*gain, 1.f) * sq(offset - abs(perlin2D(p)));
		r += signal * pow(frequency, -H);
		frequency *= lacunarity;
	}
	return r;
}
const float horizonDistance = 128;

struct Terrain {
	const float elevationDeviation = 10;
	static constexpr int N = 64; // Terrain grid resolution (TODO: triangle mesh)
	float altitudeSamples[(N+1)*(N+1)];
	Surface surface;

	Terrain() /*: surface(1, vec3(vec2(-horizonDistance), 0), vec3(vec2(horizonDistance), elevationDeviation))*/ {
		const bgr3f groundColor = vec3(14./16, 14./16., 13./16);

		for(int y: range(N+1)) for(int x: range(N+1)) altitudeSamples[y*(N+1)+x] = elevationDeviation * ridgedMultifractal(vec2((float)x/N,(float)y/N)*2.f); // Uniform random altitude

		// Terrain surface
		for(int y: range(N)) for(int x: range(N)) {
			vec2 min = vec2(-1 + 2.f*x/N, -1 + 2.f*y/N) * horizonDistance;
			vec2 max = vec2(-1 + 2.f*(x+1)/N, -1 + 2.f*(y+1)/N)  * horizonDistance;
			surface.face((vec3[]){
					 vec3(min.x, min.y, altitudeSamples[(y+0)*(N+1)+(x+0)]),
					 vec3(max.x, min.y, altitudeSamples[(y+0)*(N+1)+(x+1)]),
					 vec3(max.x, max.y, altitudeSamples[(y+1)*(N+1)+(x+1)]),
					 vec3(min.x, max.y, altitudeSamples[(y+1)*(N+1)+(x+0)]) }, groundColor);
		}
	}

	float altitude(vec2 world) { return bilinear(altitudeSamples, N+1, N+1, (world/horizonDistance+vec2(1.f))/2.f); };
};

struct Tree {
	const bgr3f trunkColor = vec3(14./16., 13./16, 12./16);
	const bgr3f branchColor = vec3(15./16, 16./16., 14./16); //vec3(1./2, 1./3, 0);
	Surface surface;

	void internode(vec3 a, vec3 b, float rA, float rB, vec3 zA, vec3 zB, vec3 color) {
		vec3 xA = cross(zA, vec3(0,1,0)); xA = normalize(length(xA)?xA:cross(zA,vec3(0,0,1))); vec3 yA = normalize(cross(zA, xA));
		vec3 xB = cross(zB, vec3(0,1,0)); xB = normalize(length(xB)?xB:cross(zB,vec3(0,0,1))); vec3 yB = normalize(cross(zB, xB));
		const int angleSteps = clip(2, int(8*ceil(max(rA, rB))), 16);
		for(int i: range(angleSteps)) {
			float a0 = 2*PI*i/angleSteps, a1=2*PI*(i+1)/angleSteps;
			surface.face((vec3[]){
					 a + rA*cos(a0)*xA + rA*sin(a0)*yA,
					 a + rA*cos(a1)*xA + rA*sin(a1)*yA,
					 b + rB*cos(a1)*xB + rB*sin(a1)*yB,
					 b + rB*cos(a0)*xB + rB*sin(a0)*yB
				 }, color);
		}
	}

	struct Node {
		vec3 axis;
		float length;
		float baseRadius, endRadius;
		int order;
		float axisLength;
		bool bud;
		vec3 nextAxis;
		Node(vec3 axis, float length, float baseRadius, float endRadius, int order, float axisLength=0) : axis(axis), length(length), baseRadius(baseRadius), endRadius(endRadius), order(order), axisLength(axisLength), bud(true), nextAxis(axis) {}
		array<unique<Node>> branches;

		void grow(Random& random, int year, vec3 origin=vec3(0)) {
			// Tropism
			axis = normalize(axis+vec3(0,0,1./(8*(order+1))/*+length/meter*/)); // Weight
			// Grows
			const float growthRate = 1+1./(16*(year+1)*(order+1));
			length *= growthRate;
			baseRadius *= growthRate;
			vec3 end = origin+length*axis;
			for(auto& branch: branches) branch->grow(random, year, end);
			if(order==2 || (order>=1 && axisLength>1)) bud = false;
			if(bud) { // Shoots new branches
				// Coordinate system
				vec3 z = axis;
				vec3 x = cross(z, vec3(0,1,0)); x = normalize(::length(x)?x:cross(z,vec3(0,0,1)));
				vec3 y = normalize(cross(z, x));
				{// Main axis
					const float gnarl = order==0 ? 1./64 : 1./32;
					float xAngle = 2*PI*(random()*2-1) * gnarl;
					float yAngle = 2*PI*(random()*2-1) * gnarl;
					vec3 main = normalize(sin(xAngle)*x + sin(yAngle)*y + cos(xAngle)*cos(yAngle)*z);
					branches.append(main, length*(1-1./4), endRadius, endRadius*(1-1./4), order, axisLength+length);
				}
				if(order<=0 && year<6) {// -- Whorl
					const int branchCount = order==0 ? 7+random%(11-7) :  2+random%(5-2);
					float phase = 2*PI*random();
					for(int i: range(branchCount)) {
						float angle = phase + 2*PI*i/branchCount + (2*PI*random() / branchCount);
						float zAngle = PI / 4 + (2*PI*random() / 16);
						float length = this->length/2;
						vec3 shootAxis = sin(zAngle)*(cos(angle)*x + sin(angle)*y) + cos(zAngle)*z;
						shootAxis = normalize(shootAxis-vec3(0,0,1+length)); // Weight
						branches.append(shootAxis, endRadius+length, endRadius/2 + min(baseRadius, 1.f/10), (endRadius*(1-1./4))/2 + min(baseRadius, 1.f/10), order+1);
					}
				}
			}
			bud = false;
			nextAxis = branches ? branches[0]->axis : axis; // First is main axis
			endRadius = branches ?  branches[0]->baseRadius : min(baseRadius/2, 1.f/10) ;
		};
	};

	void geometry(const Node& node, vec3 origin=vec3(0)) {
		vec3 end = origin+node.length*node.axis;
		internode(origin, end, node.baseRadius, node.endRadius, node.axis, node.nextAxis, node.order ? branchColor : trunkColor);
		if(node.branches) for(auto& branch: node.branches) geometry(branch, end);
		else { // Cap
			vec3 b = end;
			vec3 zB = node.axis;
			vec3 xB = cross(zB, vec3(0,1,0)); xB = normalize(length(xB)?xB:cross(zB,vec3(0,0,1))); vec3 yB = normalize(cross(zB, xB));
			float rA = node.baseRadius, rB = node.endRadius;
			const int angleSteps = clip(2, int(8*ceil(max(rA, rB))), 16);
			array<vec3> face;
			for(int i: range(angleSteps)) {
				float a = 2*PI*i/angleSteps;
				face.append( b + rB*cos(a)*xB + rB*sin(a)*yB );
			}
			surface.face(face, node.order ? branchColor : trunkColor);
		}
	};

	Tree(Random& random) /*: surface(vec3(-2,-2,-1), vec3(2,2,16))*/ {
		Node root (vec3(0,0,1), 1, 2.f/10, 2.f/10*(1-1./4), 0, 0); // Trunk axis root
		const int age = 8; // 3 - 20 y
		for(int year: range(age)) root.grow(random, year);
		geometry(root); // TODO: independent object
	}
};

/// Views a scene
struct View : Widget {
	// Creates a window and an associated GL context
	Window window {this, 512, []{ return "Editor"__; }, true, Image(), true};
	// ^ GL* constructors rely on a GL context being current ^
	struct Surface {
		GLVertexBuffer vertexBuffer;
		GLIndexBuffer indexBuffer;
		Surface(::Surface&& surface) {
			assert_(surface.vertices && surface.indices);
			// Normalizes normals
			for(Vertex& vertex: surface.vertices) vertex.normal = normalize(vertex.normal);
			// Submits geometry
			vertexBuffer.upload(surface.vertices);
			indexBuffer.upload(surface.indices);
		}
	};
	array<unique<Surface>> surfaces;
	struct Instance {
		Surface& surface;
		mat4 transform;
	};
	array<Instance> instances;
	GLShader diffuse {shader(), {"transform normal color diffuse light"}};
	// Light
	struct Light {
		float pitch = 3*PI/4;
		bool enable = true; // Whether shadows are rendered
		vec3 lightMin=0, lightMax=0; // Scene bounding box in sun light space
		/*GLFrameBuffer shadow {GLTexture(4096,4096, Depth|Shadow|Bilinear|Clamp)};
		GLShader transform {shader(), {"transform"}};
		Light(const Surface& surface) {
			{// Compute scene bounds in light space to fit shadow
				lightMin=0, lightMax=0;
				mat4 worldToLight = mat4()
						.rotateX( pitch );
				for(const Surface::Vertex& vertex: surface.vertices) {
					vec3 P = worldToLight * vertex.position;
					lightMin = min(lightMin, P);
					lightMax = max(lightMax, P);
				}
			}
			// Renders shadow map
			if(!shadow) {
				// World to [-1,1]³
				mat4 worldToLight = mat4()
						.translate(-1) // [-1,1]
						.scale(2.f/(lightMax-lightMin)) // [0,2]
						.translate(-lightMin) // [0,max-min]
						.rotateX( pitch );

				//glDepthTest(true);
				//glCullFace(true);
				shadow.bind(ClearDepth);
				transform.bind();
				// Draws the single scene object
				transform["modelViewProjectionTransform"] = worldToLight;
				surface.vertexBuffer.bindAttribute(transform, "aPosition", 3, offsetof(Surface::Vertex,position));
				surface.indexBuffer.draw();
			}
		}*/
		// Light to world
		mat4 toWorld() {
			return mat4()
					.rotateX( -pitch );
		}
		/*// World to shadow sample coordinates [0,1]³
		mat4 toShadow() {
			return mat4()
					.scale(vec3(1.f/(lightMax-lightMin)))
					.translate(-lightMin)
					.rotateX( pitch );
		}*/
	} light;// {surface};
	// Sky
	GLShader sky {shader(), {"sky"}};
	GLTexture skybox {decodeImage(readFile("skybox.png"_)), SRGB|Bilinear|Cube};

	// View
	vec2 lastPos; // Last cursor position to compute relative mouse movements
	vec2 rotation = vec2(0, -PI/2); // Current view angles (yaw,pitch)
	// Render
	struct Render {
		GLVertexBuffer vertexBuffer;
		Render() { vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)}); }
		void draw(GLShader& shader) {
			vertexBuffer.bindAttribute(shader,"aPosition"_, 2);
			shader.bind();
			vertexBuffer.draw(TriangleStrip);
		}
	} render;
	//GLFrameBuffer frameBuffer;
	//GLShader present {shader(), {"screen present"_}};
	// Profile
	//int64 lastFrameEnd = realTime(), frameInterval = 20000000/*ns*/;

	const float viewDistance = 30;
	float viewHeight;

	View() {
		Random random; random.seed();
		Terrain terrain;
		instances.append({surfaces.append(unique<Surface>(move(terrain.surface))), mat4()});
		viewHeight = terrain.altitude(0) + 5;
		auto treeModels = apply(1, [&random](int) { return unique<Surface>(move(Tree(random).surface)); });
		const int treeCount = 32*32;
		for(int unused i: range(treeCount)) {
			const float radius = horizonDistance / 2;
			vec2 p = vec2(random()*2-1, random()*2-1)*radius;
			float angle = 2*PI*random();
			instances.append({treeModels[0], mat4().translate(vec3(p, terrain.altitude(p))).rotateZ(angle)});
		}
		surfaces.append(move(treeModels)); // Holds tree models
	}

	// Orbital ("turntable") view control
	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
		 vec2 delta = cursor-lastPos; lastPos=cursor;
		 if(event==Motion && button==LeftButton) {
			 rotation += float(2.f*PI) * delta / size; //TODO: warp
			 rotation.y= clip(float(-PI/*2*/), rotation.y, 0.f); // Keep pitch between [-PI, 0]
		 }
		 else return false;
		 return true;
	 }

	vec2 sizeHint(vec2) override { return 0; }
	shared<Graphics> graphics(vec2 unused size) override {
		//if(frameBuffer.size != int2(size)) frameBuffer = GLFrameBuffer(int2(size));
		//frameBuffer.bind(ClearDepth);
		glDepthTest(true);
		glCullFace(true);

		// Computes projection transform
		mat4 projection = mat4()
				.perspective(PI/4, size, 1, 2*horizonDistance)
				// .scale(vec3(size.y/size.x, 1, -1))
				;
		// Computes view transform
		mat4 view;
		const bool orbital = true; // To view objects
		if(orbital) {
			view
					//.scale(1.f/scene.radius) // Fits scene (isometric approximation) radius=1
					.translate(vec3(0,0,-viewDistance)); // Steps back
		}
		view
				.rotateX(rotation.y) // Pitch
				.rotateZ(rotation.x) // Yaw
				.translate(vec3(0,0, -viewHeight)) // Altitude
				;
		// Object-space lighting
		vec3 lightDirection = normalize(light.toWorld().normalMatrix()*vec3(0,0,-1));
		//vec3 skyLightDirection = vec3(0,0,1);

		mat4 viewProjection = projection*view;
		//diffuse["shadowTransform"] = light.toShadow();
		//diffuse["skyLightDirection"] = skyLightDirection;
		//diffuse["shadow"_] = 0; light.shadow.depthTexture.bind(0);
		diffuse.bind();
		for(Instance instance: instances) {
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aPosition"_, 3, offsetof(Vertex, position));
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aColor"_, 3, offsetof(Vertex, color));
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aNormal"_, 3, offsetof(Vertex, normal));
			diffuse["modelViewProjectionTransform"] = viewProjection * instance.transform;
			diffuse["lightDirection"] = normalize(instance.transform.inverse().normalMatrix() * lightDirection);
			instance.surface.indexBuffer.draw();
		}

		//TODO: fog
		sky["inverseViewProjectionMatrix"] = mat4(((mat3)view).transpose()) * projection.inverse();
		//sky["lightDirection"] = normalize(view.normalMatrix() * lightDirection);
		//sky["skybox"] = 0; skybox.bind(0);
		render.draw(sky);

		/*GLTexture color(width,height,GLTexture::RGB16F);
		frameBuffer.blit(color);

        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size);
        glDepthTest(false);
        glCullFace(false);

		resolve.bindSamplers("frameBuffer"); GLTexture::bindSamplers(color);
        glDrawRectangle(resolve,vec2(-1,-1),vec2(1,1));

		GLFrameBuffer::bindWindow(0, window.size);*/

		/*uint frameEnd = realTime();
		frameInterval = (1 * (frameEnd-lastFrameEnd) + (16-1) * frameInterval) / 16;
		lastFrameEnd=frameEnd;*/

		// Overlays errors / profile information (TODO: restore GL blit)
		/*Text(system.parseErrors ? copy(userErrors) :
                                  userErrors ? move(userErrors) :
                                               ftoa(1e6f/frameTime,1)+"fps "_+str(frameTime/1000)+"ms "_+str(indices.size()/3)+" faces\n"_)
				.render(int2(position+int2(16)));*/
		//window.render(); //keep updating to get maximum performance profile
		return nullptr;
	}
} app;
