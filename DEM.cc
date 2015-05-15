#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "algebra.h"
#include "png.h"
#include "time.h"
#include "parallel.h"
#include "gl.h"
FILE(shader)

// Contact

struct Contact { // 16 w
    size_t index; // Identifies contact to avoid duplicates and evaluate friction
    vec3 initialPosition; // Initial contact position
    int lastUpdate; // Time step of last update (to remove stale contacts)
    // Evaluate once to use both for normal and friction
    vec3 n; // Normal
    float fN; // Force
    vec3 v; // Relative velocity
    vec3 currentPosition; // Current contact position
    vec3 friction(const float friction) {
        //return 0; // DEBUG
        const float dynamicFriction = friction;
        vec3 fT = - dynamicFriction * fN * v / length(v);
        const float staticFrictionVelocity = 1;
        if(length(v) < staticFrictionVelocity) { // Static friction
            vec3 x = currentPosition - initialPosition;
            vec3 t = x-dot(n, x)*n;
            float l = length(t);
            /*if(l < 1./128)*/ { // FIXME
                const float staticFriction = friction;
                fT -= staticFriction * abs(fN) * t;
                vec3 u = t/l;
                constexpr float tangentialDamping = 1./8;
                if(l) fT -= tangentialDamping * dot(u, v) * u; // else static equilibrium
            }
        } else initialPosition = currentPosition; // Dynamic friction
        return fT;
    }
};
bool operator==(const Contact& a, const Contact& b) { return a.index == b.index; }
String str(const Contact& c) { return str(c.index); }

// Grid

struct Grid : buffer<uint16> {
    static constexpr size_t cellCapacity = 64; // 2 cell / line
    int3 size;
    Grid(int3 size) : buffer(size.z*size.y*size.x*cellCapacity), size(size) { clear(); }
    struct List : mref<uint16> {
        List(mref<uint16> o) : mref(o) {}
        void remove(uint16 index) {
            size_t i = 0;
            while(at(i)) { if(at(i)==index) break; i++; }
            assert(i<cellCapacity);
            while(i+1<cellCapacity) { at(i) = at(i+1); i++; }
            assert(i<cellCapacity);
            at(i) = 0;
        }
        void append(uint16 index) { // Sorted by decreasing index
            size_t i = 0;
            while(at(i) > index) { i++; assert(i<cellCapacity, (mref<uint16>)*this, index); }
            size_t j=i;
            while(index) { assert(j<cellCapacity, (mref<uint16>)*this, index); swap(index, at(j)); j++; }
            if(j<cellCapacity) at(j) = 0;
        }
    };
    List operator[](size_t i) { return slice(i*cellCapacity, cellCapacity); }
    size_t index(int x, int y, int z) {
        x = clamp(0, x, size.x-1);
        y = clamp(0, y, size.y-1);
        z = clamp(0, z, size.z-1);
        return (z*size[1]+y)*size[0]+x;
    }
    int3 index3(vec3 p) { // [-1..1, 0..2] -> [1..size-1]
        return int3(1,1,0) + int3(vec3(size-int3(2,2,1)) * (p+vec3(1,1,0))/2.f);
    }
    size_t index(vec3 p) { int3 i = index3(p); return index(i.x, i.y, i.z);}
};


struct DEM : Widget, Poll {
    static constexpr size_t wire = 0;
    static constexpr size_t wireCapacity = 2048;
    size_t wireCount = 0;
    static constexpr size_t grain = wire+wireCapacity;
    static constexpr size_t grainCapacity = 512;
    size_t grainCount = 0;
    static constexpr size_t elementCapacity = grain+grainCapacity;

    Matrix M { elementCapacity*3 };
    Vector b { elementCapacity*3 };

    // Grains + Wire
    buffer<vec3> position { elementCapacity };
    buffer<vec3> velocity { elementCapacity };
    buffer<array<Contact>> contacts { elementCapacity };
    // Grains only
    buffer<quat> rotation { grainCapacity };
    buffer<vec3> angularVelocity { grainCapacity };
    buffer<vec3> torque { grainCapacity };

    // Space partition
    Grid grainGrid {16}, wireGrid {128};

    // Parameters
    static constexpr float dt = 1./1024;

    static constexpr float grainRadius = 1./16; // 62 mm
    static constexpr float grainMass = 4./3*PI*cb(grainRadius);
    static constexpr float grainYoung = 16;
    static constexpr float grainNormalDamping = 1./8;
    static constexpr float grainFriction = 1;

    static constexpr float wireRadius = 1./128; // 8 mm
    static constexpr float internodeLength = 1./64; // 8, 4
    static constexpr float wireMass = 1./2048;
    static constexpr float wireYoung = 16;
    static constexpr float wireNormalDamping = 1./64;
    static constexpr float wireFriction = 1;

    static constexpr float wireTensionStiffness = 4;
    static constexpr float wireTensionDamping = 1./64;

    // Time
    int timeStep = 0;
    float time = 0;

    // Process
    static constexpr float moldRadius = 1./2;
    static constexpr float winchRadius = moldRadius - grainRadius - wireRadius;
    static constexpr float pourRadius = moldRadius - grainRadius;
    static constexpr float winchRate = 1024, winchSpeed = 1;
    float boundRadius = moldRadius;
    float winchAngle = 0, pourHeight = grainRadius;
    bool pour = true;
    Random random;

    // Performance
    int64 lastReport = realTime();
    Time totalTime {true}, solveTime, floorTime;
    Time grainTime, wireContactTime, wireFrictionTime, grainIntegrationTime, wireIntegrationTime;

    // Linear(FIXME) spring between two elements
    float spring(float ks, float kb, float x0, float xl, vec3 xn, int i, int j, vec3 v, const bool implicit=true) {
        assert_(i>=0 && j>=0);
        float fN = - ks * (xl - x0);
        vec3 f = fN * xn - kb * dot(xn, v) * xn;
        mat3 o = outer(xn, xn);
        assert_(xl);
        mat3 dxf = - ks * ((1 - x0/xl)*(1 - o) + o);
        mat3 dvf = - kb * o;
        for(int c0: range(3)) {
            if(implicit) for(int c1: range(3)) {
                const float a = dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                M(i*3+c0, i*3+c1) -= a;
                M(j*3+c0, j*3+c1) += a;
            }
            const float a = implicit*dt*dxf(c0, c0);
            b[i*3+c0] += dt*(  f[c0] + a*velocity[i][c0] );
            b[j*3+c0] -= dt*( f[c0] + a*velocity[j][c0] );
        }
        return fN;
    }
    // Linear(FIXME) spring to a fixed point
    float spring(float ks, float kb, float x0, float xl, vec3 xn, size_t i, vec3 v, const bool implicit=true) {
        float fN = - ks * (xl - x0);
        vec3 f = fN * xn - kb * dot(xn, v) * xn;
        mat3 o = outer(xn, xn);
        assert_(xl);
        mat3 dxf = - ks * ((1 - x0/xl)*(1 - o) + o);
        mat3 dvf = - kb * o;
        for(int c0: range(3)) {
            if(implicit) for(int c1: range(3)) {
                float a = dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                M(i*3+c0, i*3+c1) -= a;
            }
            const float a = implicit*dt*dxf(c0, c0);
            b[i*3+c0] += dt*( f[c0] + a*velocity[i][c0] );
        }
        return fN;
    }

    void contact(size_t a, size_t b, vec3 n, vec3 v, float fN, vec3 p) {
        auto& c = contacts[a].add(Contact{b,p,0,0,0,0,0});
        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition = p;
    }

    // FIXME: factorize contacts

    // Grain - Bound contact
    void contactGrainBound(size_t a) {
        {// Floor
            float xl = position[a].z;
            float x0 = grainRadius;
            if(xl < x0) {
                vec3 xn (0,0,1);
                vec3 v = velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+xn));
                constexpr float E = grainYoung/2, R = grainRadius;
                spring(4/3*E*sqrt(R), grainNormalDamping, x0, xl, xn, a, v);
            }
        }
        {// Side
            vec3 x = vec3(position[a].xy(), 0);
            float xl = length(x);
            float x0 = boundRadius - grainRadius;
            if(xl > x0) {
                vec3 xn = x/xl;
                vec3 v = velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+xn));
                constexpr float E = grainYoung/2, R = grainRadius;
                spring(4/3*E*sqrt(R), grainNormalDamping, x0, xl, xn, a, v);
            }
        }
    }
    // Grain - Grain contact
    void contactGrainGrain(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        float xl = length(x);
        float x0 = grainRadius + grainRadius;
        if(xl < x0) {
            vec3 n = x/xl;
            vec3 v = (velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+n)))
                          - (velocity[b] + cross(angularVelocity[b-grain], grainRadius*(-n)));
            constexpr float E = grainYoung/2, R = grainRadius/2;
            float fN = spring(4/3*E*sqrt(R), grainNormalDamping, x0, xl, n, a, b, v);
            vec3 p = (position[a]+grainRadius*n + position[b]+grainRadius*(-n))/2.f;
            contact(a, b, n, v, fN, p);
        }
    }

    // Wire - Bound contact
    void contactWireBound(size_t a) {
        {// Floor
            float xl = position[a].z;
            float x0 = wireRadius;
            if(xl < x0) {
                vec3 n (0,0,1);
                vec3 v = velocity[a];
                constexpr float E = wireYoung/2, R = wireRadius;
                float fN = spring(4/3*E*sqrt(R), wireNormalDamping, x0, xl, n, a, v);
                vec3 p (position[a].xy(), (position[a].z-wireRadius)/2);
                contact(a, invalid, n, v, fN, p);
            }
        }
        {//Side
            vec3 x = vec3(position[a].xy(), 0);
            float xl = length(x);
            float x0 = boundRadius + wireRadius;
            if(xl > x0) {
                vec3 xn = x/xl;
                vec3 v = velocity[a];
                constexpr float E = wireYoung/2, R = wireRadius;
                spring(4/3*E*sqrt(R), wireNormalDamping, x0, xl, xn, a, v);
            }
        }
    }
    // Wire - Wire vertices contact
    void contactWireWire(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        float xl = length(x);
        float x0 = wireRadius + wireRadius;
        if(xl < x0) {
            vec3 xn = x/xl;
            vec3 v = velocity[a] - velocity[b];
            constexpr float E = wireYoung/2, R = wireRadius/2;
            spring(4/3*E*sqrt(R), grainNormalDamping, x0, xl, xn, a, b, v);
            // FIXME: Contact
        }
    }
    // Wire vertex - Grain contact
    void contactWireGrain(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        float l = length(x);
        float x0 = wireRadius + grainRadius;
        if(l < x0) {
            vec3 n = x/l;
            vec3 v = velocity[a] - (velocity[b] + cross(angularVelocity[b-grain], grainRadius*(-n)));
            constexpr float E = 1/(1/wireYoung + 1/grainYoung), R = 1/(1/wireRadius+1/grainRadius);
            float fN = spring(4/3*E*sqrt(R), 1/(1/wireNormalDamping+1/grainNormalDamping), x0, l, n, a, b, v);
            vec3 p = (position[a]+wireRadius*n + position[b]+grainRadius*(-n))/2.f;
            contact(a, b, n, v, fN, p);
        }
    }

    // Single implicit Euler time step
    void step() {
        // Process
        if(pour && (pourHeight>=2 || grainCount == grainCapacity || wireCount == wireCapacity)) {
            log("Release", wireCount, grainCount);
            pour = false;
            boundRadius *= 2;
        }
        if(pour) {
            // Generates falling grain (pour)
            for(;;) {
                vec3 p(random()*2-1,random()*2-1, pourHeight);
                if(length(p.xy())>1) continue;
                vec3 newPosition (pourRadius*p.xy(), p.z);
                for(vec3 p: position.slice(grain, grainCount)) if(length(p - newPosition) < 2*grainRadius) goto break2_;
                size_t i = grainCount; grainCount++;
                position[grain+i] = newPosition;
                velocity[grain+i] = 0;
                contacts.set(grain+i);
                grainGrid[grainGrid.index(position[i])].append(1+grain+i);
                rotation[i] = quat();
                angularVelocity[i] = 0;
                torque[i] = 0;
                break;
            }
            break2_:;
            // Generates wire (winch)
            winchAngle += winchRate * dt * winchRadius * internodeLength;
            pourHeight += winchSpeed * grainRadius * dt;
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle), pourHeight+grainRadius);
            size_t lastWire = wire+wireCount-1;
            vec3 r = winchPosition-position[lastWire];
            float l = length(r);
            if(1 && l > internodeLength*1.5) {
                size_t i = wireCount; wireCount++;
                position[wire+i] = position[lastWire] + internodeLength/l*r;
                velocity[wire+i] = 0;
                contacts.set(wire+i);
                wireGrid[wireGrid.index(position[i])].append(1+wire+i);
            }
        }

        // Initialization: gravity, bound, tension
        M.clear(); //b.clear();
        constexpr vec3 g {0, 0, -1};
        for(size_t i: range(grainCount)) {
            torque[i] = 0;
            vec3 force = grainMass * g;
            for(int c: range(3)) {
                M((grain+i)*3+c, (grain+i)*3+c) = grainMass;
                b[(grain+i)*3+c] = dt *  force[c];
            }
            contactGrainBound(grain+i);
        }
        for(size_t i: range(1, wireCount)) {
            vec3 force = wireMass * g;
            for(int c: range(3)) {
                M((wire+i)*3+c, (wire+i)*3+c) = wireMass;
                b[(wire+i)*3+c] = dt *  force[c];
            }
            contactWireBound(wire+i);

            // Wire tension
            vec3 x = position[wire+i] - position[wire+i-1]; // Outward from A to B
            float xl = length(x);
            float x0 = internodeLength;
            vec3 nx = x/xl;
            vec3 v = velocity[wire+i]  - velocity[wire+i-1]; // Velocity of A relative to B
            spring(wireTensionStiffness, wireTensionDamping, x0, xl, nx, wire+i, wire+i-1, v); // FIXME
        }

        // Grain - Grain contacts
        grainTime.start();
        parallel_for(grain, grain+grainCount, [this](uint, int a) {
            if(1) {
                int3 index = grainGrid.index3(position[a]);
                for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                    if(index.z+dz < 0) continue;
                    Grid::List list = grainGrid[grainGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                    for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                        int b = list[i]-1;
                        if(!(a < b)) break; // Single contact per pair, until 0
                        contactGrainGrain(a, b);
                    }
                }
            } else {
                for(int b: range(grain, grain+grainCount)) if(a<b) contactGrainGrain(a, b);
            }
        });
        grainTime.stop();

        // Contact frictions
        parallel_for(grain, grainCount, [this](uint, size_t a) {
            for(size_t i=0; i<contacts[a].size;) {
                Contact& c = contacts[a][i];
                if(c.lastUpdate != timeStep) { contacts[a].removeAt(i); continue; } else i++;
                size_t b = c.index;
                vec3 fT = c.friction(grainFriction);
                for(int c0: range(3)) { // FIXME: implicit
                    this->b[a*3+c0] += dt*fT[c0];
                    torque[a-grain] += cross(grainRadius*c.n, fT);
                    this->b[b*3+c0] -= dt*fT[c0];
                    torque[b-grain] -= cross(grainRadius*c.n, fT);
                }
            }
        });

        // Wire - Wire contacts
        wireContactTime.start();
        parallel_for(wire, wire+wireCount, [this](uint, int a) {
            {
                int3 index = wireGrid.index3(position[a]);
                for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                    if(index.z+dz < 0) continue;
                    Grid::List list = wireGrid[wireGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                    for(size_t i=0; i<wireGrid.cellCapacity; i++) {
                        int b = list[i]-1;
                        if(!(a+1 < b)) break; // No adjacent, single contact per pair, until 0
                        contactWireWire(a, b);
                    }
                }
            }
            {
                int3 index = grainGrid.index3(position[a]);
                for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                    if(index.z+dz < 0) continue;
                    Grid::List list = grainGrid[grainGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                    for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                        int b = list[i]-1;
                        if(b<0) break; // until 0
                        contactWireGrain(a, b);
                    }
                }
            }
        });
        wireContactTime.stop();

        // Contact frictions
        wireFrictionTime.start();
        parallel_for(wire+1, wire+wireCount, [this](uint, size_t a) {
            for(size_t i=0; i<contacts[a].size;) {
                Contact& c = contacts[a][i];
                if(c.lastUpdate != timeStep) { contacts[a].removeAt(i); continue; } else i++;
                size_t b = c.index;
                const float bFriction = b >= grain ? grainFriction : wireFriction;
                vec3 fT = c.friction(1/(1/wireFriction+1/bFriction));
                for(int c0: range(3)) { // FIXME: implicit
                    this->b[a*3+c0] += dt*fT[c0];
                    if(b != invalid) {
                        this->b[b*3+c0] -= dt*fT[c0];
                        if(b >= grain) torque[b-grain] -= cross(grainRadius*c.n, fT);
                    }
                }
            }
        });
        wireFrictionTime.stop();

        // Implicit Euler
        solveTime.start();
        Vector dv = UMFPACK(M).solve(b);
        solveTime.stop();

        // Anchors both wire ends
        for(int c: range(3)) dv[(wire+0                  )*3+c] = 0;
        //for(int c: range(3)) dv[(wire+wireCount-1)*3+c] = 0;

        auto update = [this, &dv](Grid& grid, size_t i) {
            for(int c: range(3)) velocity[i][c] += dv[i*3+c];
            size_t oldCell = grid.index(position[i]);
            for(int c: range(3)) position[i][c] += dt*velocity[i][c];
            size_t newCell = grid.index(position[i]);
            if(oldCell != newCell) {
                grid[oldCell].remove(1+i);
                grid[newCell].append(1+i);
            }
        };
        for(size_t i: range(grain, grain+grainCount)) update(grainGrid, i);
        for(size_t i: range(wire,wire+wireCount)) update(wireGrid, i);

        // PCDM rotation integration
        //grainIntegrationTime.start();
        parallel_for(0, grainCount, [this](uint, size_t i) {
            static constexpr float I (2./3*grainMass*sq(grainRadius)); // mat3
            vec3 w = (rotation[i].conjugate() * quat{0, angularVelocity[i]} * rotation[i]).v; // Local
            vec3 t = (rotation[i].conjugate() * quat{0, torque[i]} * rotation[i]).v; // Local
            vec3 dw = 1/I * (t - cross(w, I*w));
            vec3 w4 = w + dt/4*dw;
            float w4l = length(w4);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*w4/w4l} * rotation[i] : rotation[i];
            vec3 w2 = w + dt/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Global
            float w2wl = length(w2w);
            // Correction (multiplicative update)
            rotation[i] = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*w2w/length(w2w)} * rotation[i] : rotation[i];
            angularVelocity[i] = (rotation[i] * quat{0, w + dt*dw} * rotation[i].conjugate()).v; // Global
        });
        //grainIntegrationTime.stop();

        time += dt;
        timeStep++;
        window->render();
        if(realTime() > lastReport+2e9) {
            log("solve",str(solveTime, totalTime)/*, "grain",str(grainTime, solveTime), "wire",str(wireContactTime, solveTime),
                 "wire",str(wireFrictionTime, solveTime), "floor",str(floorTime, solveTime),
                "grain integration", str(grainIntegrationTime, solveTime), "wire integration", str(wireIntegrationTime, solveTime)*/);
            lastReport = realTime();
        }
        queue();
    }
    void event() { step(); }

    unique<Window> window = ::window(this, 1050);
    Thread solverThread;
    DEM() : Poll(0, POLLIN, solverThread) {
        b.clear();
        size_t i = wireCount; wireCount++;
        position[wire+i] = vec3(winchRadius,0, pourHeight);
        velocity[wire+i] = 0;
        contacts.set(wire+i);
        wireGrid[wireGrid.index(position[i])].append(1+wire+i);
        step();
        solverThread.spawn();

        glDepthTest(true);

        window->actions[Space] = [this]{ writeFile(str(time), encodePNG(render(1050, graphics(1050))), home()); };
        window->actions[Return] = [this]{
            if(pour) { pour = false; boundRadius *= 2; window->setTitle("2"); log("Release", wireCount, grainCount); }
        };
        window->actions[Escape] = [this]{ log(wireCount, grainCount); requestTermination(); };
    }
    vec2 sizeHint(vec2) { return 1050; }
    shared<Graphics> graphics(vec2) {
        mat4 viewProjection = mat4() .scale(vec3(1, 1, -1./2)) .rotateX(viewRotation.y) .rotateZ(viewRotation.x) .translate(vec3(0,0, -1./2));

        const size_t grainCount = this->grainCount;
        if(grainCount) {
            buffer<vec3> positions {grainCount*6};
            for(size_t i: range(grainCount)) {
                // FIXME: GPU quad projection
                vec3 O = viewProjection*position[grain+i];
                vec3 min = O - vec3(vec2(grainRadius), 0); // Isometric
                vec3 max = O + vec3(vec2(grainRadius), 0); // Isometric
                positions[i*6+0] = min;
                positions[i*6+1] = vec3(max.x, min.y, O.z);
                positions[i*6+2] = vec3(min.x, max.y, O.z);
                positions[i*6+3] = vec3(min.x, max.y, O.z);
                positions[i*6+4] = vec3(max.x, min.y, O.z);
                positions[i*6+5] = max;
            }

            static GLShader shader {::shader(), {"sphere"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Triangles, positions.size);
        }

        const size_t wireCount = this->wireCount;
        if(wireCount>1) {
            buffer<vec3> positions {(wireCount-1)*6};
            for(size_t i: range(wireCount-1)) {
                vec3 a = position[wire+i], b = position[wire+i+1];
                // FIXME: GPU quad projection
                vec3 A = viewProjection*a, B = viewProjection*b;
                vec2 r = B.xy()-A.xy();
                float l = length(r);
                vec2 t = r/l;
                vec3 n (t.y, -t.x, 0);
                float width = wireRadius;
                vec3 P[4] {A-width*n, A+width*n, B-width*n, B+width*n};
                positions[i*6+0] = P[0];
                positions[i*6+1] = P[1];
                positions[i*6+2] = P[2];
                positions[i*6+3] = P[2];
                positions[i*6+4] = P[1];
                positions[i*6+5] = P[3];
            }
            static GLShader shader {::shader(), {"cylinder"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Triangles, positions.size);
        }
        return shared<Graphics>();
    }

    // View
    vec2 lastPos; // Last cursor position to compute relative mouse movements
    vec2 viewRotation = vec2(0, -PI/2); // Current view angles (yaw,pitch)
    // Orbital ("turntable") view control
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        vec2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            viewRotation += float(2.f*PI) * delta / size; //TODO: warp
            viewRotation.y= clamp(float(-PI/*2*/), viewRotation.y, 0.f); // Keep pitch between [-PI, 0]
        }
        else return false;
        return true;
    }
} view;
