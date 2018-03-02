#include "scene.h"
#include "matrix.h"
#include "gl.h"
#include "view-widget.h"
#include "window.h"
#include "png.h"
#include "ray.h"
#include "ggx.h"
#include "box.h"
#include "plot.h"
#include "algorithm.h"
FILE(shader_glsl)

struct Hit { float t; uint triangleIndex; float u,v; };
static inline Hit ray(const Scene& scene, const vec3 O, const vec3 D) {
    const v8sf Ox = float32x8(O.x);
    const v8sf Oy = float32x8(O.y);
    const v8sf Oz = float32x8(O.z);
    const v8sf Dx = float32x8(D.x);
    const v8sf Dy = float32x8(D.y);
    const v8sf Dz = float32x8(D.z);
    float minT = inff;
    float u, v;
    uint triangleIndex = ~0u;
    for(uint faceIndex=0; faceIndex<scene.X[0].size; faceIndex+=8) {
        const v8sf Ax = load8(scene.X[0], faceIndex);
        const v8sf Ay = load8(scene.Y[0], faceIndex);
        const v8sf Az = load8(scene.Z[0], faceIndex);
        const v8sf Bx = load8(scene.X[1], faceIndex);
        const v8sf By = load8(scene.Y[1], faceIndex);
        const v8sf Bz = load8(scene.Z[1], faceIndex);
        const v8sf Cx = load8(scene.X[2], faceIndex);
        const v8sf Cy = load8(scene.Y[2], faceIndex);
        const v8sf Cz = load8(scene.Z[2], faceIndex);
        v8sf det8, U, V; const v8sf t = ::intersect(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, Dx,Dy,Dz, det8,U,V);
        const v8sf hmin = ::hmin(t);
        const float hmin0 = hmin[0];
        if(hmin0 >= minT) continue;
        minT = hmin0;
        const uint k = ::indexOfEqual(t, hmin);
        triangleIndex = faceIndex + k;
        u = U[k] / det8[k];
        v = V[k] / det8[k];
    }
    return {minT, triangleIndex, u, v};
}

static inline Hit rayTwoSided(const Scene& scene, const vec3 O, const vec3 D) {
    const v8sf Ox = float32x8(O.x);
    const v8sf Oy = float32x8(O.y);
    const v8sf Oz = float32x8(O.z);
    const v8sf Dx = float32x8(D.x);
    const v8sf Dy = float32x8(D.y);
    const v8sf Dz = float32x8(D.z);
    float minT = inff;
    float det;
    float u, v;
    uint triangleIndex = -1;
    for(uint faceIndex=0; faceIndex<scene.X[0].size; faceIndex+=8) {
        const v8sf Ax = load8(scene.X[0], faceIndex);
        const v8sf Ay = load8(scene.Y[0], faceIndex);
        const v8sf Az = load8(scene.Z[0], faceIndex);
        const v8sf Bx = load8(scene.X[1], faceIndex);
        const v8sf By = load8(scene.Y[1], faceIndex);
        const v8sf Bz = load8(scene.Z[1], faceIndex);
        const v8sf Cx = load8(scene.X[2], faceIndex);
        const v8sf Cy = load8(scene.Y[2], faceIndex);
        const v8sf Cz = load8(scene.Z[2], faceIndex);
        v8sf det8, U, V; const v8sf t = ::intersectTwoSided(Ax,Ay,Az, Bx,By,Bz, Cx,Cy,Cz, Ox,Oy,Oz, Dx,Dy,Dz, det8,U,V);
        const v8sf hmin = ::hmin(t);
        const float hmin0 = hmin[0];
        if(hmin0 >= minT) continue;
        minT = hmin0;
        const uint k = ::indexOfEqual(t, hmin);
        triangleIndex = faceIndex + k;
        det = det8[k];
        u = U[k];
        v = V[k];
    }
    return {minT, det > 0 ? triangleIndex : uint(-1), u, v};
}

static inline uint localSampleIndex(const Scene& scene, const Hit& hit) {
    float quadU, quadV;
    {
        const float u=hit.u, v=hit.v;
        if(hit.triangleIndex%2 == 0) {
            quadU = u + v; // 0,1,1
            quadV = v; // 0,0,1
        } else {
            quadU = u; // 0,1,0
            quadV = u + v; // 0,1,1
        }
    }
    const uint quadIndex = hit.triangleIndex/2;
    const uint X = scene.nX[quadIndex];
    const uint Y = scene.nY[quadIndex];
    const uint sampleIndex = uint(quadV*Y)*X + uint(quadU*X);
    return /*scene.sampleBase[quadIndex]+*/sampleIndex;
}

// Light Area sampling
struct LightRay {
    bool hit;
    vec3 L;
    rgb3f incomingRadiance;
};
static inline LightRay lightRay(Random& random, const Scene& scene, const vec3 O, const vec3 N) {
    v8sf random8 = random();
    const vec2 uv (random8[0], random8[1]);
    Scene::QuadLight quadLight = scene.quadLights[0];
    const vec3 D = (quadLight.O + uv[0] * quadLight.size.x * quadLight.T + uv[1] * quadLight.size.y * quadLight.B) - O;

    const float dotAreaL = - dot(quadLight.N, D);
    if(dotAreaL <= 0) return {false,0,0}; // Light sample behind face
    const float dotNL = dot(D, N);
    if(dotNL <= 0) return {false,0,0};
    const v8sf Dx = D.x;
    const v8sf Dy = D.y;
    const v8sf Dz = D.z;
    const v8sf Ox = float32x8(O.x);
    const v8sf Oy = float32x8(O.y);
    const v8sf Oz = float32x8(O.z);
    for(size_t faceIndex=0; faceIndex<scene.X[0].size; faceIndex+=8) {
        const v8sf Ax = load8(scene.X[0], faceIndex);
        const v8sf Ay = load8(scene.Y[0], faceIndex);
        const v8sf Az = load8(scene.Z[0], faceIndex);
        const v8sf Bx = load8(scene.X[1], faceIndex);
        const v8sf By = load8(scene.Y[1], faceIndex);
        const v8sf Bz = load8(scene.Z[1], faceIndex);
        const v8sf Cx = load8(scene.X[2], faceIndex);
        const v8sf Cy = load8(scene.Y[2], faceIndex);
        const v8sf Cz = load8(scene.Z[2], faceIndex);

        v8sf det, u, v;
        // Front facing from light (Reverse winding (back facing to light))
        const v8sf t = ::intersect(Cx,Cy,Cz, Bx,By,Bz, Ax,Ay,Az, Ox,Oy,Oz, Dx,Dy,Dz, det,u,v);
        if(!allZero((t>v8sf(0.01f/*αd*/)) & (t<1))) return {false,0,0};
    }
    return {true,normalize(D), dotNL * dotAreaL / sq(sq(D)) * quadLight.emissiveFlux};
}

static inline rgb3f reflectance(const rgb3f diffuseReflectance, const rgba4f specularReflectance, const vec3 N, const vec3 L, const vec3 V) {
    return diffuseReflectance + (specularReflectance.a < 1 ? specularReflectance.rgb() * reflectanceGGX(specularReflectance.a, N, L, V) : 0);
}

struct GLScene {
    GLBuffer x;
    GLBuffer y;
    GLBuffer z;

    buffer<GLTexture> radiosityTextures;
    buffer<uint64> radiosityTextureHandles;
    GLBuffer radiosityTextureHandleBuffer;
    GLBuffer nXω;
    GLBuffer nYω;

#define RAYTRACE 1
#if RAYTRACE
    GLBuffer N[3];
    GLBuffer X[3];
    GLBuffer Y[3];
    GLBuffer Z[3];

    const GLShader shaders[1] {{::shader_glsl(), {"ptex raytrace"}}};
    const GLShader* shader = &shaders[0];
#else
    //const GLShader shaders[2] {{::shader_glsl(), {"ptex cubic"}}, {::shader_glsl(), {"ptex cubic bilateral"}}};
    //const GLShader shaders[1] {{::shader_glsl(), {"ptex nearest"}}};
    const GLShader shaders[1] {{::shader_glsl(), {"uv"}}};
    const GLShader* shader = &shaders[0];
#endif

    GLVertexArray vertexArray;
    GLScene(const Scene& scene) :
        x(scene.x), y(scene.y), z(scene.z),
        radiosityTextures(scene.quadCount),
        radiosityTextureHandles(scene.quadCount),
        nXω(scene.nXω), nYω(scene.nYω) {
        for(uint quadIndex: range(scene.quadCount)) {
            const uint4 size (scene.nX[quadIndex], scene.nY[quadIndex], scene.nXω[quadIndex], scene.nYω[quadIndex]);
            if(!size) continue;
            GLTexture& radiosityTexture = radiosityTextures[quadIndex];
            assert_(size[2]*size[3] < 1600, size);
            new (&radiosityTexture) GLTexture(uint3(size.xy(),size[2]*size[3]), RGB32F);
            radiosityTextureHandles[quadIndex] = radiosityTexture.handle();
        }
        radiosityTextureHandleBuffer.upload(radiosityTextureHandles);

#if RAYTRACE
        for(size_t i: range(3)) {
            X[i] = GLBuffer(scene.X[i]);
            Y[i] = GLBuffer(scene.Y[i]);
            Z[i] = GLBuffer(scene.Z[i]);
        }
        for(size_t c: range(3)) {
            buffer<float> N(scene.N.size);
            for(size_t i: range(N.size)) N[i] = scene.N[i][c];
            this->N[c] = GLBuffer(N);
        }
        const uint N = 128;
        float radicalInverse[N];
        for(uint i: range(N)) {
            const uint t0 = (i << 16u) | (i >> 16u);
            const uint t1 = ((t0 & 0x55555555u) << 1u) | ((t0 & 0xAAAAAAAAu) >> 1u);
            const uint t2 = ((t1 & 0x33333333u) << 2u) | ((t1 & 0xCCCCCCCCu) >> 2u);
            const uint t3 = ((t2 & 0x0F0F0F0Fu) << 4u) | ((t2 & 0xF0F0F0F0u) >> 4u);
            const uint t4 = ((t3 & 0x00FF00FFu) << 8u) | ((t3 & 0xFF00FF00u) >> 8u);
            radicalInverse[i] = float(t4) / float(0x100000000);
        }
        shader->uniform("radicalInverse") = ref<float>(radicalInverse);
#endif
    }

    void render() const {
        shader->bind();
        vertexArray.bindAttribute(shader->attribLocation("X"_), 1, Float, x);
        vertexArray.bindAttribute(shader->attribLocation("Y"_), 1, Float, y);
        vertexArray.bindAttribute(shader->attribLocation("Z"_), 1, Float, z);
#if PTEX // 0
        shader->bind("radiosityTextureHandleBuffer", radiosityTextureHandleBuffer, 0);
        shader->bind("nXwBuffer", nXω, 1);
        shader->bind("nYwBuffer", nYω, 2);
#endif
#if RAYTRACE
        shader->bind("NXBuffer", this->N[0], 3);
        shader->bind("NYBuffer", this->N[1], 4);
        shader->bind("NZBuffer", this->N[2], 5);
        shader->bind("X0Buffer", X[0], 6);
        shader->bind("X1Buffer", X[1], 7);
        shader->bind("X2Buffer", X[2], 8);
        shader->bind("Y0Buffer", Y[0], 9);
        shader->bind("Y1Buffer", Y[1], 10);
        shader->bind("Y2Buffer", Y[2], 11);
        shader->bind("Z0Buffer", Z[0], 12);
        shader->bind("Z1Buffer", Z[1], 13);
        shader->bind("Z2Buffer", Z[2], 14);
#endif
        glDepthTest(true);
        glCullFace(true);
        //glCullFace(false);
        //glLine(true);
        vertexArray.draw(Triangles, x.elementCount);
    }
};

struct Renderer : ViewWidget {
    Lock lock;
    const Scene& scene;
    buffer<float> scatterRadiosity[3];
    buffer<float> radiosity[3];
    buffer<float> nextRadiosity[3];
    buffer<float>  tmpRadiosity[3];

    const GLScene* gl;
    buffer<GLBuffer> radiosityBuffers {gl ? scene.quadCount : 0};
    buffer<Map> radiosityMaps {gl ? 0 : scene.quadCount};
    buffer<buffer<rgb3f>> totalRadiosities {scene.quadCount}; // FIXME: Map to move from heap to memory file without double storage (no copy)

    const rgb3f gather3(const buffer<float> components[3], uint i) { return rgb3f(components[0][i], components[1][i], components[2][i]); }
    void set3(const buffer<float> components[3], uint i, vec3 v) { components[0][i] = v[0]; components[1][i] = v[1]; components[2][i] = v[2]; }
    void add3(const buffer<float> components[3], uint i, vec3 v) { components[0][i] += v[0]; components[1][i] += v[1]; components[2][i] += v[2]; }

    //static constexpr enum Method { Radiosity } method = Radiosity;
    static constexpr bool showScatterRadiosity = true;

    bool radiosityChanged = true;

    Time totalTime {true};
    Time stepTime {false};
    Time scatterTime {false}, gatherTime {false}, storeTime {false};
    uint steps = 0; // gatherRadiositySampleCount / N (In theory the samples should be multiplied by 1/p=1/(1/N))
    uint scatterRadiositySampleCount = 0;
    rgb3f energy;

    // Lazy evaluation
    float s = 0, t = 0;
    buffer<uint16> sampleCount;

    Renderer(const Scene& scene, const GLScene* gl, const Folder& folder=Folder(), const string suffix=""_) : scene(scene), gl(gl) {
        for(int c: range(3)) {
            scatterRadiosity[c] = buffer<float>(scene.sampleRadianceCount);
            radiosity[c] = buffer<float>(scene.sampleRadianceCount);
            nextRadiosity[c] = buffer<float>(scene.sampleRadianceCount);
            tmpRadiosity[c] = buffer<float>(scene.sampleRadianceCount);
        }
        for(uint quadIndex: range(scene.quadCount)) {
            const Scene& _ = scene;
            const uint X = _.nX[quadIndex], Y = _.nY[quadIndex];
            const uint Xω = _.nXω[quadIndex], Yω = _.nYω[quadIndex];
            //if(!Yω && !Xω && !Y && !X) continue;
            if(gl) {
                new (&radiosityBuffers[quadIndex]) GLBuffer();
                new (&totalRadiosities[quadIndex]) buffer<rgb3f>(unsafeRef(radiosityBuffers[quadIndex].map<rgb3f>(Yω*Xω*Y*X)));
            } else if(folder) {
                const String name = str(quadIndex)+"."+strx(uint2(X,Y))+"."+strx(uint2(Xω,Yω));
                File file(name+suffix, folder, Flags(ReadWrite|Create|Truncate));
                file.resize(Yω*Xω*Y*X*sizeof(rgb3f));
                new (&radiosityMaps[quadIndex]) Map(file, Map::Prot(Map::Read|Map::Write));
                new (&totalRadiosities[quadIndex]) buffer<rgb3f>(unsafeRef(cast<rgb3f>(radiosityMaps[quadIndex])));
            } else {
                new (&totalRadiosities[quadIndex]) buffer<rgb3f>(Yω*Xω*Y*X);
            }
        }
        sampleCount = buffer<uint16>(scene.sampleRadianceCount);
        sampleCount.clear(0); reset(); // Just to be Sure™
    }
    void reset() {
        Locker lock(this->lock);
        scatterRadiositySampleCount = 0; steps = 0;
        //gatherConvolveTime.reset(); stepTime.reset(); totalTime.reset();
        for(int c: range(3)) {
            scatterRadiosity[c].clear(0);
            radiosity[c].clear(0);
            nextRadiosity[c].clear(0); // Just to be Sure™
            tmpRadiosity[c].clear(0); // Just to be Sure™
        }
    }
    //string name() const { return (string[]){"path","radiosity"_}[int(method)]; }
    String title() const override {
        //const float s = angles.x/(π/3), t = angles.y/(π/3);
        //return /*name()+" "+*/str(s,t);
        //str(sum(energy)/(1024*1024));
        return str(steps);
    }

    void step() {
        Locker lock(this->lock);
        stepTime.start();
        if(0/*1*/) { // S+ only (forward)
            const uint updateCount = scene.samplePositions.size;
            struct Update { uint quadIndex, localSampleIndex; vec3 O, L; rgb3f incomingRadiance; };
            buffer<Update> updates (updateCount); // Defers updates for atomic-free operation
            //scatterTime.start();
            uint64 updateCounter = 0;
            parallel([this, updateCounter, updateCount, &updates](const uint) {
                const Scene& _ = scene;
                Random random;
                for(;;) {
                    const uint updateIndex = __sync_fetch_and_add(&updateCounter, 1);
                    if(updateIndex >= updateCount) break;
                    if(!scene.quadLights) break; // FIXME
                    Scene::QuadLight quadLight = scene.quadLights[0];
                    const vec3 T = quadLight.T, B = quadLight.B, N = quadLight.N;
                    v8sf random8 = random(); const vec2 uv (random8[0], random8[1]);
                    vec3 O = quadLight.O + uv[0] * quadLight.size.x * T + uv[1] * quadLight.size.y * B;
                    vec3 V = cosineDistribution(random, T, B, N);
                    Hit hit = ray(scene, O, V);
                    if(hit.triangleIndex == uint(-1)) { updates[updateIndex].quadIndex = -1; continue; }
                    uint quadIndex = hit.triangleIndex/2;
                    rgb3f throughput = 1;
                    for(/*uint bounce = 0*/;;) { // S+
                        const rgba4f specularReflectance = _.specularReflectance[quadIndex];
                        if(specularReflectance.a == 1) { updates[updateIndex].quadIndex = -1; break; } // Diffuse
                        O += hit.t*V;
                        if(specularReflectance.a == 0) { // Pure specular
                            const vec3 N = _.N[quadIndex];
                            V = normalize(V - 2*dot(V, N)*N); // Specular reflection
                            throughput *= specularReflectance.rgb();
                        } else { // GGX
                            const vec3 L = normalize(-V);
                            const vec3 T = _.T[quadIndex], B = _.B[quadIndex], N = _.N[quadIndex];
                            const vec3 M = normalDistributionGGX(random, specularReflectance.a, T, B, N);
                            V = normalize(V - 2*dot(V, M)*M); // Specular reflection
                            /*FIXME: normalization?*/
                            throughput *= specularReflectance.rgb() * reflectanceGGX_D(specularReflectance.a, N, L, V) * 0.f; // only S
                        }
                        if(!throughput) { updates[updateIndex].quadIndex = -1; break; } // FIXME: ^ Fix GGX
                        assert_(throughput);
                        //assert_(bounce == 0); bounce++;
                        Hit hit = ray(scene, O, V);
                        if(hit.triangleIndex == uint(-1)) { updates[updateIndex].incomingRadiance = 0; break; }
                        quadIndex = hit.triangleIndex/2;
                        const uint localSampleIndex = ::localSampleIndex(scene, hit);
                        if(_.diffuseReflectance[quadIndex]) {
                            updates[updateIndex] = {quadIndex, localSampleIndex, O+hit.t*V, -V,
                                                    throughput * quadLight.emissiveFlux * 2.f / _.sampleArea[quadIndex]};
                            //assert_(!sample->specularReflectance); // Only one scatter per update
                            break;
                        }
                    }
                }
            });
            for(Update update: updates) {
                const uint quadIndex = update.quadIndex;
                if(quadIndex != uint(-1)) {
                    const uint Xω = scene.nXω[quadIndex], Yω = scene.nYω[quadIndex];
                    for(uint xω: range(Xω)) for(uint yω: range(Yω)) {
                        const v8sf random8 = random();
                        const vec3 V = normalize(vec3(float(xω+random8[0])/Xω*2-1,float(yω+random8[1])/Yω*2-1,0)-update.O);
                        add3(scatterRadiosity, scene.sampleRadianceBase[quadIndex]+update.localSampleIndex+yω*Xω+xω,
                             reflectance(scene.diffuseReflectance[quadIndex], scene.specularReflectance[quadIndex], scene.N[quadIndex],
                                         update.L, V) * update.incomingRadiance);
                    }
                }
            }
            //scatterTime.stop();
            scatterRadiositySampleCount += updateCount;
        }
        // D* only (not S+) (backward)
        for(const uint quadIndex: range(scene.quadCount)) {
            const Scene& _ = scene;
            const uint X = _.nX[quadIndex], Y = _.nY[quadIndex];
            const uint Xω = _.nXω[quadIndex], Yω = _.nYω[quadIndex];
            mref<rgb3f> quadRadiosity = totalRadiosities[quadIndex];
            uint64 sampleCounter = 0;
            gatherTime.start();
            parallel([this, quadIndex, sampleCounter, &quadRadiosity](const uint) {
                const Scene& _ = scene;
                const uint X = _.nX[quadIndex], Y = _.nY[quadIndex];
                const uint Xω = _.nXω[quadIndex], Yω = _.nYω[quadIndex];
                const uint sampleBase = _.sampleBase[quadIndex];
                const uint targetSampleRadianceBase = _.sampleRadianceBase[quadIndex];
                const rgb3f diffuseReflectance = _.diffuseReflectance[quadIndex];
                const vec3 halfSizeT= _.halfSizeT[quadIndex];
                const vec3 halfSizeB= _.halfSizeB[quadIndex];
                const rgba4f specularReflectance = _.specularReflectance[quadIndex];
                const vec3 T = _.T[quadIndex], B = _.B[quadIndex], N = _.N[quadIndex];
                Random random;
                for(;;) {
                    const uint localSampleIndex = __sync_fetch_and_add(&sampleCounter, 1);
                    if(localSampleIndex >= Y*X) break;
                    //gatherTSC.start();
                    const uint targetLocalSampleRadianceIndexBase = localSampleIndex*Yω*Xω;
                    const uint targetSampleRadianceIndexBase = targetSampleRadianceBase+targetLocalSampleRadianceIndexBase;
                    vec3 O;
                    const v8sf random8 = random();
                    O = _.samplePositions[sampleBase+localSampleIndex] + (random8[0]*2-1)*halfSizeT + (random8[1]*2-1)*halfSizeB;
                    // If not pure specular: Accumulates to all radiance samples (or a single diffuse sample)
                    if(/*specularReflectance.a > 0*/specularReflectance.a == 1) {
                        rgb3f throughput[Xω*Yω];
                        vec3 L;
                        float D0;
                        /*if(specularReflectance.a == 1)*/ { // Diffuse (FIXME: GGX should generalize to cosine for α=1)
                            L = cosineDistribution(random, T, B, N);
                            D0 = 1/π; // PDF
                        } /*else {
                                const vec3 M = normalDistributionGGX(random, specularReflectance.a, T, B, N);
                                // Chooses a random sample to path trace (but contributes to all samples (for rough surfaces))
                                const uint i = random.next()[0];
                                const uint xω = i%Xω, yω = (i/Xω)%Yω;
                                const vec3 V = normalize(vec3(float(xω)/(Xω-1)*2-1,float(yω)/(Yω-1)*2-1,0)-O);
                                L = normalize(2*dot(V, M)*M - V); // Specular reflection
                                D0 = normalDensityGGX(specularReflectance.a, N, M); // NDF PDF Normalization for principal ray
                            }*/
                        LightRay lightRay = scene.quadLights ? ::lightRay(random, scene, O, N) : LightRay{false, 0, 0}; // FIXME
                        for(uint yω: range(Yω)) for(uint xω: range(Xω)) { // First bounce has view dependent outgoing vector
                            const uint iω = yω*Xω+xω;
                            const uint targetSampleRadianceIndex = targetSampleRadianceIndexBase+iω;
#if 1 // No share
                            set3(nextRadiosity, targetSampleRadianceIndex,
                                 gather3(radiosity, targetSampleRadianceIndex) +
                                 (lightRay.hit ? diffuseReflectance * lightRay.incomingRadiance: 0));

                            throughput[iω] = diffuseReflectance / D0; // fᵣ(L, V) (*PI)
#else
                            const vec3 V = normalize(vec3(float(xω+random)/(Xω-1)*2-1,float(yω+random)/(Yω-1)*2-1,0)-O);
                            set3(nextRadiosity, targetSampleRadianceIndex,
                                 gather3(radiosity, targetSampleRadianceIndex) +
                                 (lightRay.hit ? reflectance(diffuseReflectance, specularReflectance, N, lightRay.L, V) * lightRay.incomingRadiance : 0));

                            throughput[iω] = reflectance(diffuseReflectance, specularReflectance, N, L, V) / D0; // fᵣ(L, V)
#endif
                        }
                        if(steps) {
                            const Hit hit = rayTwoSided(scene, O + 0.001f*L, L);
                            if(hit.triangleIndex != uint(-1)) {
                                /*if(method==Radiosity)*/ { // First bounce indirect lighting (Radiosity)
                                    rgb3f incomingRadiance = 0;
                                    { // Gathers radiosity
                                        const uint quadIndex = hit.triangleIndex/2;
                                        const uint localSampleIndex = ::localSampleIndex(scene, hit);
                                        if(_.nXω[quadIndex] == 1 && _.nYω[quadIndex] == 1) {
                                            const uint sampleRadianceIndex = _.sampleRadianceBase[quadIndex] + localSampleIndex;
                                            // D Specular+
                                            if(scatterRadiositySampleCount)
                                                incomingRadiance += gather3(scatterRadiosity, sampleRadianceIndex) / float(scatterRadiositySampleCount);
                                            if(steps > 0) incomingRadiance += gather3(radiosity, sampleRadianceIndex) / float(steps); // Diffuse*
                                        }
                                        // else No outgoing radiance sampled for arbitrary directions (only stored for view directions)
                                        // FIXME: Path trace or store
                                    }
                                    for(uint yω: range(Yω)) for(uint xω: range(Xω)) {
                                        const uint iω = yω*Xω+xω;
                                        const uint targetSampleRadianceIndex = targetSampleRadianceIndexBase+iω;
                                        add3(nextRadiosity, targetSampleRadianceIndex, throughput[iω] * incomingRadiance);
                                    }
                                }
                            } else if(!scene.quadLights) { // FIXME?
                                rgb3f incomingRadiance = 0.3;
                                for(uint yω: range(Yω)) for(uint xω: range(Xω)) {
                                    const uint iω = yω*Xω+xω;
                                    const uint targetSampleRadianceIndex = targetSampleRadianceIndexBase+iω;
                                    add3(nextRadiosity, targetSampleRadianceIndex, throughput[iω] * incomingRadiance);
                                }
                            }
                        }
                    } else {
#ifdef JSON
                        error("glossy");
#endif
#define RANDOM_SAMPLE 0
#if RANDOM_SAMPLE
                        for(uint yω: range(Yω)) for(uint xω: range(Xω)) { // set nextRadiosity = radiosity for all other samples
                            const uint iω = yω*Xω+xω;
                            const uint targetSampleRadianceIndex = targetSampleRadianceIndexBase+iω*Y*X;
                            set3(nextRadiosity, targetSampleRadianceIndex, gather3(radiosity, targetSampleRadianceIndex));
                        }
                        const uint i = random.next()[0];
                        const uint xω = i%Xω, yω = (i/Xω)%Yω; // Chooses a random sample to path trace (but contributes to all samples (for rough surfaces))
                        {
#else
#if 1
                        if(steps) // Only if there is any radiosity to reflect (do not do an emit/scatter only step) (/!\ FIXME Bias)
#endif
                        for(uint yω: range(Yω)) for(uint xω: range(Xω)) {
#endif
                            const uint iω = yω*Xω+xω;
                            const uint targetSampleRadianceIndex = targetSampleRadianceIndexBase+iω;
#define RENDER 1
#ifdef RENDER
                            const int2 i (int(s*Xω), int(t*Yω));
                            if(!((i.x-1 <= int(xω) && int(xω) <= i.x+2) && (i.y-1 <= int(yω) && int(yω) <= i.y+2))) continue;
#endif

                            uint16& sampleCount = this->sampleCount[targetSampleRadianceIndex];
                            //if(sampleCount==0xFF) sampleCount=0; // Resets
                            sampleCount++;

                            const v8sf random8 = random();
                            const vec3 V = normalize(vec3(float(xω+random8[0])/Xω*2-1,float(yω+random8[1])/Yω*2-1,0)-O);
                            vec3 L;
                            rgb3f throughput;
                            if(specularReflectance.a == 0) {
                                L = normalize(2*dot(V, N)*N - V); // Specular reflection
                                throughput = specularReflectance.rgb();
                            } else {
                                assert_(specularReflectance.a < 1, specularReflectance.a);
                                const vec3 M = normalDistributionGGX(random, specularReflectance.a, T, B, N);
                                L = normalize(2*dot(V, M)*M - V); // Specular reflection
                                // fᵣ(L, V)
                                //throughput = reflectanceGGX_D(specularReflectance, N, L, V) * (4.f /** dot(N, L) * dot(M, V) / (dot(M, N) * dot(V, N)));
                                throughput = specularReflectance.rgb() * reflectanceGGX_D(specularReflectance.a, N, L, V) * (4.f * dot(N, L) * dot(M, V));
                                //throughput = dot(M, N);
                            }
                            const Hit hit = rayTwoSided(scene, O + 0.001f*L, L);
                            if(hit.triangleIndex != uint(-1)) {
                                /*if(method==Radiosity)*/ { // First bounce indirect lighting (Radiosity)
                                    rgb3f incomingRadiance = 0;
                                    { // Gathers radiosity
                                        const uint quadIndex = hit.triangleIndex/2;
                                        const uint localSampleIndex = ::localSampleIndex(scene, hit);
                                        if(_.nXω[quadIndex] == 1 && _.nYω[quadIndex] == 1) {
                                            const uint sampleRadianceIndex = _.sampleRadianceBase[quadIndex] + localSampleIndex;
                                            // D Specular+
                                            incomingRadiance += gather3(scatterRadiosity, sampleRadianceIndex) / float(scatterRadiositySampleCount);
                                            if(steps > 0) incomingRadiance += gather3(radiosity, sampleRadianceIndex) / float(steps); // Diffuse*
                                        }
                                        // else No outgoing radiance sampled for arbitrary directions (only stored for view directions)
                                        // FIXME: Path trace or store
                                    }
                                    set3(nextRadiosity, targetSampleRadianceIndex,
                                         gather3(radiosity, targetSampleRadianceIndex) +
     #if RANDOM_SAMPLE
                                         float(Xω*Yω) *
     #endif
                                         throughput * incomingRadiance);
                                }
                            }
#if !RANDOM_SAMPLE
                            else set3(nextRadiosity, targetSampleRadianceIndex, gather3(radiosity, targetSampleRadianceIndex));
#endif
                        }
                    }
#if 0
                    else
                    incomingRadiance = 0;
                    for(/*Scene::Sample const* sample*/;;) {
                        // Bias to prevent self shadowing (when using two-sided intersection)
                        // (using two-sided in case there are lights outside single sided walls)
                        const Hit hit = rayTwoSided(scene, O + 0.001f*D, D);
                        if(hit.triangleIndex == uint(-1)) break;
                        const uint sampleIndex = ::sampleIndex(scene, hit);
                        rgb3f incomingRadiance = 0;
                        if(method==Radiosity) {
                            if(steps > 0) incomingRadiance += (radiosity[sampleIndex] / float(steps)); // Diffuse*
                            incomingRadiance += ((rgb3f&)scatterRadiosity[sampleIndex] / float(scatterRadiositySampleCount)); // D Specular+
                            for(uint xω: range(Xω)) for(uint yω: range(Yω)) {
                                const uint iω = yω*Xω+xω;
                                outgoingRadiance[iω] += throughput[iω] * π * incomingRadiance;
                            }
                            break;
                        } // else Follows BRDF sampled bounces and accumulates light contributing through the path
                        /*for(uint xω: range(Xω)) for(uint yω: range(Yω)) {
                            const uint iω = yω*Xω+xω;
                            outgoingRadiance[iω] += throughput[iω] * π * incomingRadiance;
                            throughput[iω] *= π * reflectance(sample, D, V[iω]); // fᵣ(D, V)
                        }
                        O += hit.t*D;
                        sample = &_.samples[::sampleIndex(scene, hit)];
                        const float p = max(cast<float>(ref<vec3>(throughput,Yω*Xω)));
                        if(random()[0] >= p) break;
                        for(uint iω: Yω*Xω) throughput[iω] *= 1 / p;*/
                    }
#endif
                }
            });
            gatherTime.stop();
            storeTime.start();
            //const rgb3f emissiveRadiance = _.emissiveRadiance[quadIndex].rgb();
            const float scatterWeight = 1 / float(scatterRadiositySampleCount);
            const uint sampleRadianceBase = _.sampleRadianceBase[quadIndex];
#if 1
            if(Xω==1 && Yω==1) { // TODO: SIMD
                const float gatherWeight = 1 / float(steps+1);
                if(scatterRadiositySampleCount) {
                    for(uint i: range(X*Y)) for(uint c: range(3))
                        quadRadiosity[i][c] = /*scatterWeight * scatterRadiosity[c][sampleRadianceBase+i] +*/
                                gatherWeight * nextRadiosity[c][sampleRadianceBase+i]
                                ;//+ emissiveRadiance[c]; // FIXME
                } else {
                    for(uint i: range(X*Y)) for(uint c: range(3))
                        quadRadiosity[i][c] = gatherWeight * nextRadiosity[c][sampleRadianceBase+i];//+ emissiveRadiance[c]; // FIXME
                }
            } else
#endif
#if 1
            if(steps) // No data
#endif
            {
                error("Glossy");
                const float gatherWeight = 1 / float(steps);
#if 0
                        for(uint c: range(3)) {
                            const float *const scatterRadiosity = this->scatterRadiosity[c].begin()+targetSampleRadianceIndexBase;
                            const float *const  gatherRadiosity = this->   nextRadiosity[c].begin()+targetSampleRadianceIndexBase;
                            /**/  float *const     tmpRadiosity = this->    tmpRadiosity[c].begin()+targetSampleRadianceIndexBase;
                            for(uint i=0; i<Yω*Xω; i+=8)
                                store(tmpRadiosity, i, scatterWeight * load8(scatterRadiosity, i) + gatherWeight * load8(gatherRadiosity, i));
                            /**/  float *const              tmp = this->        radiosity[c].begin()+targetSampleRadianceIndexBase;
                            for(uint unused _: range(2)) box<1>(tmpRadiosity, tmp, tmpRadiosity, Xω, Yω);
                        }
                        // CUVST -> STUVC
                        for(uint iω: range(Yω*Xω)) quadRadiosity[iω*Y*X+localSampleIndex] =
                                rgb3f(tmpRadiosity[0][targetSampleRadianceIndexBase+iω],
                                tmpRadiosity[1][targetSampleRadianceIndexBase+iω],
                                tmpRadiosity[2][targetSampleRadianceIndexBase+iω]);
#if 0 // AVERAGE
                        float sum = 0;
                        for(int iω: range(Yω*Xω)) sum += totalRadiosity[iω];
                        const float w = 1.f/(Yω*Xω);
                        const float v = w * sum;
                        for(uint yω0: range(Yω)) for(uint xω0: range(Xω)) glRadiosity[yω0*Xω+xω0] = v;
#endif
#else
                // CUVST -> STUVC
                uint64 sampleCounter = 0;
                parallel([&sampleCounter, X,Y,Yω,Xω, scatterWeight,gatherWeight, this,sampleRadianceBase,quadRadiosity](uint) {
                    const float *const scatterRadiosity0 = this->scatterRadiosity[0].begin()+sampleRadianceBase;
                    const float *const  gatherRadiosity0 = this->   nextRadiosity[0].begin()+sampleRadianceBase;
                    const float *const scatterRadiosity1 = this->scatterRadiosity[1].begin()+sampleRadianceBase;
                    const float *const  gatherRadiosity1 = this->   nextRadiosity[1].begin()+sampleRadianceBase;
                    const float *const scatterRadiosity2 = this->scatterRadiosity[2].begin()+sampleRadianceBase;
                    const float *const  gatherRadiosity2 = this->   nextRadiosity[2].begin()+sampleRadianceBase;
                    const uint YX = Y*X, YωXω = Yω*Xω;
                    const v8sf unused scatterWeight8 = scatterWeight;
                    const v8sf unused gatherWeight8 = gatherWeight;
                    for(;;) {
                        const uint i = __sync_fetch_and_add(&sampleCounter, 1);
                        if(i >= YX) break;
                        const float *const scatterRadiosityUV0 = scatterRadiosity0+i*YωXω;
                        const float *const  gatherRadiosityUV0 =  gatherRadiosity0+i*YωXω;
                        const float *const scatterRadiosityUV1 = scatterRadiosity1+i*YωXω;
                        const float *const  gatherRadiosityUV1 =  gatherRadiosity1+i*YωXω;
                        const float *const scatterRadiosityUV2 = scatterRadiosity2+i*YωXω;
                        const float *const  gatherRadiosityUV2 =  gatherRadiosity2+i*YωXω;
                        for(uint iω=0; iω<YωXω; iω+=8) {
#if 1
                            uint16* const sampleCount = this->sampleCount.begin()+sampleRadianceBase+i*YωXω+iω;
                            v8sf v0, v1, v2;
                            for(uint k: range(8)) {
                                v0[k] = scatterWeight*scatterRadiosityUV0[iω+k] + gatherRadiosityUV0[iω+k]/sampleCount[k];
                                v1[k] = scatterWeight*scatterRadiosityUV1[iω+k] + gatherRadiosityUV1[iω+k]/sampleCount[k];
                                v2[k] = scatterWeight*scatterRadiosityUV2[iω+k] + gatherRadiosityUV2[iω+k]/sampleCount[k];
                            }
#else
                            v8sf v0 = scatterWeight8 * load8(scatterRadiosityUV0, iω) + gatherWeight8 * load8(gatherRadiosityUV0, iω);
                            v8sf v1 = scatterWeight8 * load8(scatterRadiosityUV1, iω) + gatherWeight8 * load8(gatherRadiosityUV1, iω);
                            v8sf v2 = scatterWeight8 * load8(scatterRadiosityUV2, iω) + gatherWeight8 * load8(gatherRadiosityUV2, iω);
#endif
                            rgb3f* const target = quadRadiosity.begin()+i+iω*YX;
                            for(uint k: range(8)) target[k*YX] = rgb3f(v0[k], v1[k], v2[k]);
                        }
                    }
                });
#endif
            }
            storeTime.stop();
            if(gl && radiosityBuffers[quadIndex]) {
                radiosityBuffers[quadIndex].unmap();
                gl->radiosityTextures[quadIndex].upload(uint3(X,Y,Yω*Xω), radiosityBuffers[quadIndex], 0, Yω*Xω*Y*X);
                totalRadiosities[quadIndex] = unsafeRef(radiosityBuffers[quadIndex].map<rgb3f>());
            }
        }
        for(int c: range(3)) swap(radiosity[c], nextRadiosity[c]);

        stepTime.stop();
        steps++;
        if(1) log(//scene.samplePositions.size, scene.sampleRadianceCount,
                  steps,
                  stepTime.milliseconds() /steps,
                  strD(stepTime, totalTime),
                  //strD(scatterTime, stepTime),
                  strD(gatherTime, stepTime),
                  strD(storeTime, stepTime)
                  );
        //stepTime.reset(); gatherTime.reset(); storeTime.reset();
        /*uint64 totalTime = gatherTime+storeTime;
        log(strD(gatherTime,totalTime),strD(storeTime,totalTime));*/
        radiosityChanged = true;
    }

    /*void upload() {
        if(needUpload) {
            bufferLock[readIndex].lock();
            const Scene& _ = scene;
            totalRadiosity[readIndex] = {};
            radiosityBuffer[readIndex].unmap();
            for(uint quadIndex: range(_.quadCount)) {
                const uint X = _.nX[quadIndex], Y = _.nY[quadIndex];
                const uint Xω = _.nXω[quadIndex], Yω = _.nYω[quadIndex];
                gl.radiosityTextures[quadIndex].upload(uint3(X,Y,Yω*Xω), radiosityBuffer[readIndex], _.sampleRadianceBase[quadIndex], Yω*Xω*Y*X);
            }
            readIndex = (readIndex+1)%2;
            if(!totalRadiosity[readIndex]) {
                totalRadiosity[readIndex] = radiosityBuffer[readIndex].map<rgb3f>();
                bufferLock[readIndex].unlock();
            }
            needUpload = false;
        }
    }*/

    void render(const GLFrameBuffer& framebuffer) {
        const float s = angles.x/(π/3), t = angles.y/(π/3);
        this->s = s; this->t = t; // Lazy evaluation
        const uint2 size = framebuffer.size;
        const mat4 view1 = shearedPerspective(s, t, scene.near, scene.far) .scale(vec3(1, size.x/size.y, 1));

        framebuffer.bind(ClearColor|ClearDepth, rgba4f(rgb3f(0),1));
        gl->shader->uniform("view1") = view1;
#if RAYTRACE
        gl->shader->uniform("viewOrigin") = vec3(s,t,0);
#endif
#if 0 // PTEX
        (*gl->shader)["st"] = ::min(vec2((1+s)/2,(1+t)/2), vec2(1-__FLT_EPSILON__));
#endif
        gl->render();
    }

    Image4f image(const GLFrameBuffer& framebuffer) {
        render(framebuffer);
        GLFrameBuffer target(framebuffer.size, 1);
        framebuffer.blit(target.id);
        Image4f imageTarget (target.size);
        target.readback(imageTarget);
        return imageTarget;
    }
    void render(RenderTarget2D&, vec2, vec2) override { error(""); }
    void render(GLFrameBuffer& framebuffer, vec2 unused offset, vec2 unused size) {
        render(framebuffer);
        framebuffer.blit(::window());
    }
};

static struct App
#if RENDER
        : Widget, Poll {
    Thread rendererThread {19};
    Lock rendererLock;
#else
{
#endif
    //Folder folder{"box", environmentVariable("XDG_RUNTIME_DIR")};

    const float sizes[2] {0.01f,0.1f};
    uint sizeIndex = 0;
    float size = sizes[sizeIndex];

    //uint rendererIndex = -1;

    uint shaderIndex = 0;

    unique<Scene> scene = nullptr;
    unique<Renderer> renderers[1] = {nullptr};
    const Renderer& renderer() const { return renderers[0/*rendererIndex*/]; }
    Renderer& renderer() { return renderers[0/*rendererIndex*/]; }

#if RENDER
    unique<Window> window = ::window(this, 2048);
    unique<GLScene> gl = nullptr;
    static constexpr uint samples = 1; //32; // 8-32
    GLFrameBuffer framebuffer{window->size, samples};
#endif

    App()
#if RENDER
        : Poll(0, POLLIN, rendererThread)
#endif
    {
#if RENDER
        window->actions[Key::Return] = [this]{
            renderer().angles = 0;
            window->render();
        };
        window->actions[Key('r')] = [this]{
            //renderer().method=Renderer::Method((int(renderer().method)+1)%2);
            renderer().reset();
        };
        //window->actions[Key('e')] = [this]{ renderer().showScatterRadiosity = !renderer().showScatterRadiosity; };
        window->actions[Key('s')] = [this]{
            Locker lock(rendererLock);
            if(0) {
                //writeFile(renderer().name()+/*"."+str(renderer().steps)+*/".png", encodePNG(sRGB(renderer().image(framebuffer))), home(), true);
            } else {
#if 0
                const Scene& _ = scene;
                for(uint quadIndex: range(_.quadCount)) {
                    const uint X = _.nX[quadIndex], Y = _.nY[quadIndex];
                    const uint Xω = _.nXω[quadIndex], Yω = _.nYω[quadIndex];
                    mref<rgb3f> quadRadiosity = renderer().totalRadiosities[quadIndex];
                    for(uint iω: range(Yω*Xω))
                        writeFile(str(quadIndex)+"."+str(iω)/*+".png"*/,
                                  encodePNG(sRGB(Image3f(unsafeRef(quadRadiosity.slice(iω*Y*X,Y*X)),uint2(X,Y)))), folder, true);
                }
#endif
            }
        };
        window->actions[Key('w')] = [this]{
            sizeIndex = (sizeIndex+1)%ref<float>(sizes).size;
            size = sizes[sizeIndex];
            log(size);
            load();
        };
        window->actions[Key('c')] = [this]{
            shaderIndex = (shaderIndex+1)%ref<GLShader>(gl->shaders).size;
            gl->shader = &gl->shaders[shaderIndex];
            log(shaderIndex);
            load();
        };
        //window->actions[Space] = [this](){ renderer().step(); window->render(); };
        /*window->actions[RightArrow] = [this]{ rendererIndex=(rendererIndex+1)%ref<unique<Renderer>>(renderers).size; };
        window->actions[LeftArrow] =
                [this]{ rendererIndex=(rendererIndex-1+ref<unique<Renderer>>(renderers).size)%ref<unique<Renderer>>(renderers).size; };
        for(uint i: range(ref<unique<Renderer>>(renderers).size)) window->actions[Key('1'+i)] = [this, i]{ rendererIndex=i; window->render(); };*/
        load();
        queue();
        rendererThread.spawn();
        window->show();
#else
        for(string file: folder.list(Files)) remove(file, folder);
        Scene scene = ::loadScene(arguments()?arguments()[0]:"box", ref<entry<string,float>>{{"Lx"_,size},{"Ly"_,size}});
        for(bool ref: range(2)) {
            Renderer renderer(scene, nullptr, !ref, folder, ref?".ref"_:""); // Oversharp: Approximate gloss by filtering
            for(uint unused step: range(2)) renderer.step();
        }
#endif
    }
#if RENDER
    ~App() {
        //rendererThread.wait();
        exit_group(0);
    }
    void load() {
        Locker lock(rendererLock);
        scene = unique<Scene>(::loadScene(arguments()?arguments()[0]:"box", ref<entry<string,float>>{{"Lx"_,size},{"Ly"_,size}}));
        gl = unique<GLScene>(scene);
        renderers[0] = unique<Renderer>(scene, gl.pointer);
        renderer().step();
    }
    void event() override {
        static XWindow::GLXContext unused glContext = reinterpret_cast<XWindow*>(window.pointer)->initializeThreadGLContext();
        Locker lock(rendererLock);
        renderer().step();
        queue();
        window->render(); // Asynchronous post to rendering thread
    }
    virtual String title() const override { return /*rendererIndex!=uint(-1) ? renderer().title() : String();*/ renderers[0] ? renderer().title() : {}; }
    void render(RenderTarget2D&, vec2 offset, vec2 size) override { return renderer().render(framebuffer, offset, size); }
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
        return renderer().mouseEvent(cursor, size, event, button, focus);
    }
#endif
} app;
