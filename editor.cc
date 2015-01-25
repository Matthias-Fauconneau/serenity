#include "window.h"
#include "matrix.h"
#include "gl.h"
#include "time.h"
#include "tiff.h"
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
};

struct AutoSurface : Surface {
	/// Creates a new face using existing vertices when possible
	/// \note Vertex normals are mean of adjacent face normals
	void face(const ref<Vertex> polygon) {
		assert(polygon.size>=3);
		uint indices[polygon.size];
		for(uint i: range(polygon.size)) { // Lookups each vertex
			const Vertex& o = polygon[i];

			float minDistance=0; uint minIndex = -1;
			for(uint index: range(vertices.size)) {
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

static float triangle(ref<Vertex> vertices, int width, int height, const vec2 v) {
	static float dx = 30.f;
	vec2 uv = (v/dx)+vec2(width/2, height/2);
	int2 i = int2(uv);
	assert_(i.x >= 0 && i.x+1 < width && i.y >=0 && i.y+1 < height, v, uv, i, width, height, v/dx);
	vec2 f = uv-floor(uv);
	if(f.x < f.y) {
		vec3 v0 = vertices[(i.y+0)*width+i.x].position;
		vec3 v1 = vertices[(i.y+0)*width+i.x+1].position;
		vec3 v2 = vertices[(i.y+1)*width+i.x+1].position;
		return v0.z + dot(v-v0.xy(), v1.xy()-v0.xy())/sq(v1.xy()-v0.xy())*(v1.z-v0.z) + dot(v-v0.xy(), v2.xy()-v0.xy())/sq(v2.xy()-v0.xy())*(v2.z-v0.z);
	} else {
		vec3 v0 = vertices[(i.y+0)*width+i.x].position;
		vec3 v1 = vertices[(i.y+1)*width+i.x+0].position;
		vec3 v2 = vertices[(i.y+1)*width+i.x+1].position;
		return v0.z + dot(v-v0.xy(), v1.xy()-v0.xy())/sq(v1.xy()-v0.xy())*(v1.z-v0.z) + dot(v-v0.xy(), v2.xy()-v0.xy())/sq(v2.xy()-v0.xy())*(v2.z-v0.z);
	}
};

static constexpr int N = 256; // Terrain grid resolution (TODO: irregular triangle mesh)
const float horizonDistance = (N/2-1) * 30;

struct Terrain {
	static constexpr int D = 1; // Downsample
	buffer<Vertex> vertices {N*N};
	buffer<uint> indices {((N-1)/D)*((N-1)/D)*6};
	vec3 max = 0;

	Terrain() {
		const bgr3f groundColor = vec3(3./16, 3./16., 0./16);

		Map map("srtm_12_03.tif");
		Image16 elevation = parseTIFF(map);
		//assert_(elevation.width == elevation.stride && elevation.width == N+1 && elevation.height == N+1, elevation.width, elevation.height, elevation.stride);
		const float dx = 30;
		for(int y: range(N)) for(int x: range(N)) {
			int16 v = elevation.data[y*elevation.stride+x];
			vertices[y*N+x] = {vec3(vec2(x - N/2, y - N/2)*dx, (v>0?v:0)), groundColor, vec3(0)};
			if(y<N-1 && x<N-1 && vertices[y*N+x].position.z > max.z) max = vertices[y*N+x].position;
		}
		for(int y: range(1,N-1)) for(int x: range(1,N-1)) { // Computes normal from 4 neighbours
			float dxZ = vertices[y*N+x+1].position.z - vertices[y*N+x-1].position.z;
			float dyZ = vertices[(y+1)*N+x].position.z - vertices[(y-1)*N+x].position.z;
			vertices[y*N+x].normal = normalize(vec3(-dxZ, -dyZ, dx));
		}

		// Terrain surface
		/*for(int y: range(N-1)) for(int x: range(N-1)) {
			indices[y*(N-1)*6+x*6+0] = y*N+x;
			indices[y*(N-1)*6+x*6+1] = y*N+x+1;
			indices[y*(N-1)*6+x*6+2] = (y+1)*N+x+1;
			indices[y*(N-1)*6+x*6+3] = (y+1)*N+x+1;
			indices[y*(N-1)*6+x*6+4] = (y+1)*N+x;
			indices[y*(N-1)*6+x*6+5] = y*N+x;
		}*/
		for(int y: range((N-1)/D)) for(int x: range((N-1)/D)) {
			indices[y*((N-1)/D)*6+x*6+0] = (y*D)*N+x*D;
			indices[y*((N-1)/D)*6+x*6+1] = (y*D)*N+(x+1)*D;
			indices[y*((N-1)/D)*6+x*6+2] = ((y+1)*D)*N+(x+1)*D;
			indices[y*((N-1)/D)*6+x*6+3] = ((y+1)*D)*N+(x+1)*D;
			indices[y*((N-1)/D)*6+x*6+4] = ((y+1)*D)*N+x*D;
			indices[y*((N-1)/D)*6+x*6+5] = (y*D)*N+x*D;
		}
	}

	float elevation(vec2 world) { return triangle(vertices, N, N, world); };
};

struct Tree {
	const bgr3f trunkColor = vec3(6./16., 4./16, 0./16);
	const bgr3f branchColor = vec3(2./16, 6./16., 0./16); //vec3(1./2, 1./3, 0);
	AutoSurface surface;

	void internode(vec3 a, vec3 b, float rA, float rB, vec3 zA, vec3 zB, vec3 color) {
		vec3 xA = cross(zA, vec3(0,1,0)); xA = normalize(length(xA)?xA:cross(zA,vec3(0,0,1))); vec3 yA = normalize(cross(zA, xA));
		vec3 xB = cross(zB, vec3(0,1,0)); xB = normalize(length(xB)?xB:cross(zB,vec3(0,0,1))); vec3 yB = normalize(cross(zB, xB));
		const int angleSteps = 2; //clip(2, int(8*ceil(max(rA, rB))), 16);
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
		/*else { // Cap
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
		}*/
	};

	Tree(Random& random) {
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
		ref<Vertex> vertices;
		GLVertexBuffer vertexBuffer;
		GLIndexBuffer indexBuffer;
		Surface(ref<Vertex> vertices, ref<uint> indices) : vertices(vertices) {
			assert_(vertices && indices);
			// Submits geometry
			vertexBuffer.upload(vertices);
			indexBuffer.upload(indices);
		}
	};
	array<unique<Surface>> surfaces;
	GLShader diffuse {shader(), {"transform normal color diffuse light shadow"}};


	Random random;
	Terrain terrain;
	buffer<Tree> treeModels = apply(1, [this](int) { return Tree(random); });

	const float viewDistance = 30;
	const vec3 origin = vec3(terrain.max.xy(), 0);
	vec3 position = vec3(origin.xy(), terrain.elevation(origin.xy()) + 5);

	struct Instance {
		Surface& surface;
		mat4 transform;
	};
	array<Instance> evaluateInstances() {
		array<Instance> instances;
		auto treeSurfaces = apply(treeModels, [](const Tree& tree) { return unique<Surface>(tree.surface.vertices, tree.surface.indices); });
		const int treeCount = 64*64;
		uint instanced=0; while(instanced < treeCount) {
			const float radius = 1000;
			vec2 p = origin.xy() + vec2(random()*2-1, random()*2-1)*radius;
			float dx = 30;
			if(p.x<=-(N-1)/2*dx || p.y<=-(N-1)/2*dx || p.x >= (N-1)/2*dx || p.y >= (N-1)/2*dx) continue;
			float angle = 2*PI*random();
			instances.append({treeSurfaces[0], mat4().translate(vec3(p, terrain.elevation(p))).rotateZ(angle)});
			instanced++;
		}
		surfaces.append(move(treeSurfaces)); // Holds tree models
		instances.append({surfaces.append(unique<Surface>(terrain.vertices, terrain.indices)), mat4()});
		return instances;
	}
	array<Instance> instances = evaluateInstances();

	// Light
	struct Light {
		float pitch = 3*PI/4;
		bool enable = true; // Whether shadows are rendered
		vec3 lightMin=0, lightMax=0; // Scene bounding box in sun light space
		GLFrameBuffer shadow {GLTexture(4096,4096, Depth|Shadow|Bilinear|Clamp)};
		GLShader transform {shader(), {"transform"}};
		Light(ref<Instance> instances) {
			{// Compute scene bounds in light space to fit shadow
				lightMin=0, lightMax=0;
				mat4 worldToLight = mat4()
						.rotateX( pitch );
				for(const Instance& instance: instances) {
					for(const Vertex& vertex: instance.surface.vertices) {
						vec3 P = worldToLight * instance.transform * vertex.position;
						lightMin = min(lightMin, P);
						lightMax = max(lightMax, P);
					}
				}
			}
			// -- Renders shadow map
			// World to [-1,1]³
			mat4 worldToLight = mat4()
					.translate(-1) // [-1,1]
					.scale(2.f/(lightMax-lightMin)) // [0,2]
					.translate(-lightMin) // [0,max-min]
					.rotateX( pitch );

			glDepthTest(true);
			glCullFace(true);
			shadow.bind(ClearDepth);
			transform.bind();
			// Draws all shadow casting instances
			for(const Instance& instance: instances) {
				transform["modelViewProjectionTransform"] = worldToLight * instance.transform;
				instance.surface.vertexBuffer.bindAttribute(transform, "aPosition", 3, offsetof(Vertex, position));
				instance.surface.indexBuffer.draw();
			}
		}
		// Light to world
		mat4 toWorld() {
			return mat4()
					.rotateX( -pitch );
		}
		// World to shadow sample coordinates [0,1]³
		mat4 toShadow() {
			return mat4()
					.scale(vec3(1.f/(lightMax-lightMin)))
					.translate(-lightMin)
					.rotateX( pitch );
		}
	} light {instances};
	// Sky
	GLShader sky {shader(), {"sky"}};
	//GLTexture skybox {decodeImage(readFile("skybox.png"_)), SRGB|Bilinear|Cube};

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

	vec3 force = 0; // View coordinate system
	bool keyPress(Key key, Modifiers) override {
		if(key==Key('q')) force = 0, speed = 0;
		else if(key==Key('a')) force.x--;
		else if(key==Key('w')) force.z--;
		else if(key==Key('s')) force.z++;
		else if(key==Key('d')) force.x++;
		else if(key==Space) force.y++;
		else if(key==ControlKey) force.y--;
		else return false;
		return true;
	}
	bool keyRelease(Key key, Modifiers) override {
		// FIXME: Missed release
		if(key==Key('a')) force.x=0;
		else if(key==Key('w')) force.z=0;
		else if(key==Key('s')) force.z=0;
		else if(key==Key('d')) force.x=0;
		else if(key==Space) force.y=0;
		else if(key==ControlKey) force.y=0;
		else return false;
		return true;
	}

	vec3 speed = 0;
	bool step() { // Assumes 60 Hz (FIXME: handle dropped frames)
		speed *= 1-1./16;
		speed += mat4().rotateX(rotation.y).rotateZ(rotation.x).inverse() * force;
		position += speed;
		return force || length(speed) > 1./60;
	}

	vec2 sizeHint(vec2) override { return 0; }
	shared<Graphics> graphics(vec2 unused size) override {
		//if(frameBuffer.size != int2(size)) frameBuffer = GLFrameBuffer(int2(size));
		//frameBuffer.bind(ClearDepth);
		glDepthTest(true);
		glCullFace(true);

		// Computes view projection transform
		mat4 projection = mat4().perspective(PI/4, size, 1, 2*horizonDistance + length(position));
		mat4 view = mat4()
				.rotateX(rotation.y) // Pitch
				.rotateZ(rotation.x) // Yaw
				.translate(-position); // Position
		mat4 viewProjection = projection*view;

		// Object-space lighting
		vec3 lightDirection = normalize(light.toWorld().normalMatrix()*vec3(0,0,-1));

		diffuse["shadow"_] = 0; light.shadow.depthTexture.bind(0);
		diffuse.bind();
		for(Instance instance: instances) {
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aPosition"_, 3, offsetof(Vertex, position));
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aColor"_, 3, offsetof(Vertex, color));
			instance.surface.vertexBuffer.bindAttribute(diffuse, "aNormal"_, 3, offsetof(Vertex, normal));
			diffuse["modelViewProjectionTransform"] = viewProjection * instance.transform;
			diffuse["shadowTransform"] = light.toShadow() * instance.transform;
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
		if(step()) window.render();
		return nullptr;
	}
} app;
