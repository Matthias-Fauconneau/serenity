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
	static constexpr uint G = 16; // Grid resolution (TODO: adapt with vertex count)
	vec3 gridMin = -1, gridMax = 1; // current grid bounds
	array<uint> grid[G*G*G];
	array<uint>& cell(vec3 p) {
		assert(p>=gridMin && p<=gridMax, gridMin, p, gridMax);
		int3 indices = min(int3(G), int3(float(G) * (p-gridMin) / (gridMax-gridMin))); // Assigns vertices on maximum limit to last cell
		size_t index = indices.z*G*G + indices.y*G + indices.x;
		assert_(index < G*G*G);
		return grid[index];
	}

	vec3 bbMin=0, bbMax=0; // Scene bounding box in world space
	vec3 center=0; float radius=0; // Scene bounding sphere in world space

	void clear() {
		vertices.clear(); indices.clear(); bbMin=0, bbMax=0, center=0, radius=0;
		gridMin=-1, gridMax=1; for(array<uint>& cell: grid) cell.clear(); // Clear grid
	}

	/// Creates a new face using existing vertices when possible
	/// \note Vertex normals are mean of adjacent face normals
	template<uint N> void face(const Vertex (&polygon)[N]) {
		static_assert(N>=3,"");
		uint indices[N];
		for(uint i: range(N)) { // Lookups each vertex
			const Vertex& o = polygon[i];

			// Computes scene bounds in world space to fit view
			bbMin = min(bbMin, o.position);
			bbMax = max(bbMax, o.position);
			// FIXME: evaluate only once needed
			center = (bbMin+bbMax)/2.f; radius = length(bbMax.xy()-bbMin.xy())/2.f; // FIXME: compute smallest enclosing sphere

			if(!(o.position>=gridMin && o.position<=gridMax)) {
				error(gridMin, o.position, gridMax);
				gridMin=min(gridMin,o.position/*-vec3(1)*/); gridMax=max(gridMax,o.position/*+vec3(1)*/); // Resizes grid
				for(array<uint>& cell: grid) cell.clear(); // Clears grid
				for(uint i: range(vertices.size)) cell(vertices[i].position).append( i ); // Sorts all vertices
			}

			float minDistance=0; uint minIndex = -1;
			for(uint index: cell(o.position)) {
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
				cell(o.position).append( index );
				indices[i] = index;
			}
		}
		// Appends polygon indices
		uint a = indices[0];
		uint b = indices[1];
		for(size_t i: range(2,N)) { // Tesselates convex polygons as fans // TODO: triangle strips with broad triangle optimization
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
	template<uint N> void face(const vec3 (&polygon)[N], vec3 color) {
		vec3 a = polygon[0];
		vec3 b = polygon[1];
		Vertex vertices[N]={{a,color,0},{b,color,0}};
		for(uint i: range(2,N)) { // Fan
			vec3 c = polygon[i];
			vec3 surface = cross(b-a, c-a); // Weights contribution to vertex normal by face area
			vertices[i-2].normal += surface;
			vertices[i-1].normal += surface;
			vertices[i] = {c,color,surface};
			b = c;
		}
		face(vertices);
	}
};

/// Generates a scene
struct Scene : virtual Surface {
	static constexpr float viewHeightM = 16; // m
	static constexpr float earthRadiusM = 6.4e6; // m
	const float horizonDistanceM = sqrt(2*earthRadiusM*viewHeightM);
	const float meter = 1/horizonDistanceM; // Defined by scene bounds [-1, 1]
	const float viewHeight = viewHeightM*meter;
	const float elevationDeviation = 50*meter;

	Scene() {
		const bgr3f groundColor = white;

		Random random;
		const int N = 256; // Terrain grid resolution (TODO: triangle mesh)
		float altitude[(N+1)*(N+1)];
		for(int y: range(N+1)) for(int x: range(N+1)) altitude[y*(N+1)+x] = elevationDeviation * (random()*2-1); // Uniform random altitude

		// Terrain surface
		for(int y: range(N)) for(int x: range(N)) {
			vec2 min = vec2(-1 + 2.f*x/N, -1 + 2.f*y/N);
			vec2 max = vec2(-1 + 2.f*(x+1)/N, -1 + 2.f*(y+1)/N);
			face((vec3[]){
					 vec3(min.x, min.y, altitude[(y+0)*(N+1)+(x+0)]),
					 vec3(max.x, min.y, altitude[(y+0)*(N+1)+(x+1)]),
					 vec3(max.x, max.y, altitude[(y+1)*(N+1)+(x+1)]),
					 vec3(min.x, max.y, altitude[(y+1)*(N+1)+(x+0)]) }, groundColor);
		}

		// Normalizes normals
		for(Vertex& vertex: vertices) vertex.normal = normalize(vertex.normal);
	}
};

/// Views a scene
struct View : Widget {
	// Creates a window and an associated GL context
	Window window {this, 512, []{ return "Editor"__; }, true, Image(), true};
	// ^ GL* constructors rely on a GL context being current ^
	struct Surface : virtual ::Surface {
		GLVertexBuffer vertexBuffer;
		GLIndexBuffer indexBuffer;
		Surface() {
			assert_(vertices && indices);
			// Submits geometry
			vertexBuffer.upload(vertices);
			indexBuffer.upload(indices);
		}
	};
	struct : Scene, Surface {} scene;
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

		// Computes projection transform
		mat4 projection = mat4()
				.perspective(PI/4, size, scene.meter, 1)
				// .scale(vec3(size.y/size.x, 1, -1))
				;
		// Computes view transform
		mat4 view = mat4()
				//.scale(1.f/scene.radius) // Fits scene (isometric approximation) radius=1
				//.translate(vec3(0,0,-1*scene.radius)) // Steps back
				.rotateX(rotation.y) // Pitch
				.rotateZ(rotation.x) // Yaw
				.translate(vec3(0,0, -scene.viewHeight)) // Altitude
				;
		// World-space lighting
		vec3 lightDirection = normalize(light.toWorld().normalMatrix()*vec3(0,0,-1));
		//vec3 skyLightDirection = vec3(0,0,1);

		diffuse["modelViewProjectionTransform"] = projection*view;
		//diffuse["shadowTransform"] = light.toShadow();
		diffuse["lightDirection"] = lightDirection;
		//diffuse["skyLightDirection"] = skyLightDirection;
		//diffuse["shadow"_] = 0; light.shadow.depthTexture.bind(0);
		diffuse.bind();
		scene.vertexBuffer.bindAttribute(diffuse, "aPosition"_, 3, offsetof(Vertex, position));
		scene.vertexBuffer.bindAttribute(diffuse, "aColor"_, 3, offsetof(Vertex, color));
		scene.vertexBuffer.bindAttribute(diffuse, "aNormal"_, 3, offsetof(Vertex, normal));
		scene.indexBuffer.draw();

		//TODO: fog
		sky["inverseViewProjectionMatrix"] = mat4(((mat3)view).transpose()) * projection.inverse();
		//sky["lightDirection"] = normalize(view.normalMatrix() * lightDirection);
		sky["skybox"] = 0; skybox.bind(0);
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
