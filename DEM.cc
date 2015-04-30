#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "algebra.h"

// -> vector.h
template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct quat {
    float s; vec3 v;
    quat conjugate() { return {s, -v}; }
};
quat operator*(quat p, quat q) { return {p.s*q.s - dot(p.v, q.v), p.s*q.v + q.s*p.v + cross(p.v, q.v)}; }
String str(quat q) { return "["+str(q.s, q.v)+"]"; }

// -> distance.h
void closest(vec3 a1, vec3 a2, vec3 b1, vec3 b2, vec3& A, vec3& B) {
    vec3 u = a2 - a1;
    vec3 v = b2 - b1;
    vec3 w = a1 - b1;
    float  a = dot(u,u);        // always >= 0
    float  b = dot(u,v);
    float  c = dot(v,v);        // always >= 0
    float  d = dot(u,w);
    float  e = dot(v,w);
    float  D = a*c - b*b;       // always >= 0
    float  sD = D;      // sc = sN / sD, default sD = D >= 0
    float  tD = D;      // tc = tN / tD, default tD = D >= 0

    // Compute the line parameters of the two closest points
    float sN, tN;
    if (D < __FLT_EPSILON__) { // the lines are almost parallel
        sN = 0;        // force using point P0 on segment S1
        sD = 1;        // to prevent possible division by 0.0 later
        tN = e;
        tD = c;
    }
    else {                // get the closest points on the infinite lines
        sN = (b*e - c*d);
        tN = (a*e - b*d);
        if (sN < 0) {       // sc < 0 => the s=0 edge is visible
            sN = 0;
            tN = e;
            tD = c;
        }
        else if (sN > sD) {  // sc > 1 => the s=1 edge is visible
            sN = sD;
            tN = e + b;
            tD = c;
        }
    }

    if (tN < 0) {           // tc < 0 => the t=0 edge is visible
        tN = 0;
        // recompute sc for this edge
        if (-d < 0) sN = 0;
        else if (-d > a) sN = sD;
        else { sN = -d; sD = a; }
    }
    else if (tN > tD) {      // tc > 1 => the t=1 edge is visible
        tN = tD;
        // recompute sc for this edge
        if ((-d + b) < 0) sN = 0;
        else if ((-d + b) > a) sN = sD;
        else { sN = (-d + b); sD = a; }
    }
    // finally do the division to get sc and tc
    float sc = (abs(sN) < __FLT_EPSILON__ ? 0 : sN / sD);
    float tc = (abs(tN) < __FLT_EPSILON__ ? 0 : tN / tD);
    A = a1 + (sc * u);
    B = b1 - (tc * v);
    // get the difference of the two closest points
    //vec3 dP = (a1 + (sc * u)) - (b1 + (tc * v));  // = S1(sc) - S2(tc)
    //return len( dP );   // return the closest distance
}


struct DEM : Widget, Poll {   
    const float smoothCoulomb = 0.1;
    const int skip = 1; //16;
    const float dt = 1./(60*16); //1./(60*skip);

    const float floorHeight = 1;
    const float floorYoung = 100000, floorPoisson = 0;
    const float floorNormalDamping = 10, floorTangentialDamping = 10;
    const float floorFriction = 1;

    struct Particle {
        float radius;
        float mass;
        vec3 position;
        vec3 velocity = 0;
        //vec3 acceleration; // Temporary
        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
        vec3 torque; // Temporary
        Particle(vec3 position, float radius)
            : radius(radius), mass(1/**4./3*PI*cb(radius)*/), position(position) {}
    };
    array<Particle> particles;
    const float particleRadius = 1./4;
    const float particleYoung = 2000, particlePoisson = 0;
    const float particleNormalDamping = 50, particleTangentialDamping = 1;
    const float particleFriction = 1;

    struct Node {
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary for explicit integration
        Node(vec3 position) : position(position) {}
    };
    const float nodeMass = 1;
    const float wireRadius = 1./16;
    const float wireYoung = 2000, wirePoisson = 0;
    const float wireNormalDamping = 50;
    const float wireFriction = 2;
    const float wireTangentialDamping = 10;
    const float wireTensionStiffness = 1000; //1000
    const float wireTensionDamping = 10; //10
    const float wireBendStiffness = 100;
    const float wireBendDamping = -100;

    float internodeLength = 1./4;

    array<Node> nodes;

    const float spoolerRate = 2./16;
    const float spoolerSpeed = 1./32;

    Random random;

    int timeStepCount = 0;
    float time = 0;

    void event() { step(); }
    void step() {
        /*{ // Generate falling particle (pouring)
            bool generate = true;
            for(auto& p: particles) {
                if(p.position.z - p.radius < -1) generate = false;
            }
            if(generate && particles.size<256) {
                for(;;) {
                    vec3 p(random()*2-1,random()*2-1,-1);
                    if(length(p.xy())>1) continue;
                    particles.append(p, particleRadius);
                    break;
                }
            }
        }*/
        /*{ // Generate wire (spooling)
            float spoolerAngle = 2*PI*spoolerRate*time;
            float spoolerHeight = floorHeight-spoolerSpeed*time;
            vec3 spoolerPosition (cos(spoolerAngle), sin(spoolerAngle), spoolerHeight);
            vec3 r = spoolerPosition-nodes.last().position;
            float l = length(r);
            if(l > internodeLength*2 // *2 to keep some tension
                    && nodes.size<256) nodes.append(nodes.last().position + internodeLength/l*r);
        }*/
        Matrix M (nodes.size*3, nodes.size*3); Vector b (nodes.size*3); // TODO: CG solve
        // Gravity, Wire - Floor contact, Bending resistance
        for(int i: range(0, nodes.size-0)) {
            vec3 force = 0;
            // Gravity (implicit)
            const vec3 g (0, 0, 1);
            force += nodeMass * g;
            for(int c: range(3)) {
                M(i*3+c, i*3+c) = nodeMass;
                b[i*3+c] = dt * force[c];
            }
            nodes[i].acceleration = 0;
            /*// Elastic Wire - Floor contact
            float d = (p.position.z + wireRadius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(wirePoisson))/wireYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/wireDamping)) ?
                vec3 n (0, 0, -1);
                vec3 v = p.velocity;
                float vN = dot(n, v);
                float fN = 4/3*E*sqrt(wireRadius)*d*sqrt(d) - kN * vN; //FIXME
                force += fN * n;
                vec3 vT = v - dot(n, v)*n;
                float friction = -floorFriction; // mu
                float kT = floorTangentialDamping; // 1/(1/floorDamping + 1/wireDamping)) ?
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                    force += fT;
                }
                assert(isNumber(force));
            }
            p.acceleration = force / nodeMass;*/
        }
        /*for(auto& p: particles) {
            vec3 force = 0;
            vec3 torque = 0; // Global
            // Gravity
            const vec3 g (0, 0, 1);
            force += p.mass * g;
            // Elastic sphere - Floor contact
            float d = (p.position.z + p.radius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(particlePoisson))/particleYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                vec3 n (0, 0, -1);
                vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n));
                float vN = dot(n, v);
                float fN = 4/3*E*sqrt(p.radius)*d*sqrt(d) - kN * vN;
                force += fN * n;
                vec3 vT = v - dot(n, v)*n;
                float friction = -floorFriction; // mu
                float kT = floorTangentialDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                    force += fT;
                    torque += cross(p.radius*(-n), fT);
                }
                assert(isNumber(force));
            }
            // Elastic sphere - sphere contact
            for(const auto& b: particles) {
                if(&b == &p) continue;
                vec3 r = p.position - b.position;
                float d = p.radius + b.radius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(particlePoisson))/particleYoung);
                    float kN = particleNormalDamping;
                    float R = 1/(1/p.radius+1/b.radius);
                    vec3 n = r/length(r);
                    vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n))
                           -(b.velocity + cross(b.angularVelocity, b.radius*(+n)));
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v);
                    force += fN * n;
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -particleFriction; // mu
                    float kT = particleTangentialDamping;
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                        force += fT;
                        torque += cross(p.radius*(-n), fT);
                    }
                    assert(isNumber(force));
                }
            }
            // Elastic sphere - cylinder contact
            for(int i: range(nodes.size-1)) {
                int a = i, b = i+1;
                vec3 A = nodes[a].position, B = nodes[b].position;
                float l = length(B-A);
                vec3 n = (B-A)/l;
                float t = dot(n, p.position-A);
                if(t < 0 || t > l) continue;
                vec3 P = A + t*n;
                vec3 r = p.position - P;
                float d = p.radius + wireRadius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(wirePoisson))/wireYoung);
                    float kN = wireNormalDamping; // 1/(1/wireNormalDamping + 1/particleDamping)) ?
                    float R = 1/(1/p.radius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n))
                           ;//FIXME -(b.velocity + cross(b.angularVelocity, b.radius*(+n)));
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v); // ?
                    force += fN * n;
                    nodes[a].acceleration -= (fN * n) / (2 * nodeMass);
                    nodes[b].acceleration -= (fN * n) / (2 * nodeMass);
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -wireFriction; // mu
                    float kT = wireTangentialDamping; // 1/(1/wireNormalDamping + 1/particleDamping)) ?
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                        force += fT;
                        nodes[a].acceleration -= fT / (2 * nodeMass);
                        nodes[b].acceleration -= fT / (2 * nodeMass);
                        torque += cross(p.radius*(-n), fT);
                    }
                    assert(isNumber(force));
                }
            }
            p.acceleration = force / p.mass;
            p.torque = torque;
        }*/
        // Wire tension
        for(int i: range(1,nodes.size-1)) {
            auto spring = [&](int i, int j) {
                vec3 x = nodes[i].position - nodes[j].position;
                float l = length(x);
                vec3 n = x/l;
                vec3 v = nodes[i].velocity - nodes[j].velocity;
                vec3 f = - wireTensionStiffness * (l - internodeLength) * n - wireTensionDamping * n * dot(n, v);
                mat3 o = outer(n, n);
                mat3 dxf = (1 - internodeLength/l)*(1 - o) + o;
                mat3 dvf = - wireTensionDamping * o;
                for(int c0: range(3)) {
                    for(int c1: range(3)) {
                        M(i*3+c0, i*3+c1) -= dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                        M(j*3+c0, j*3+c1) += dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                    }
                    b[i*3+c0] += dt*(  f[c0] + dt*dxf(c0, c0)*nodes[i].velocity[c0] );
                    b[j*3+c0] -= dt*( f[c0] + dt*dxf(c0, c0)*nodes[j].velocity[c0] );
                }
            };
            spring(i-1, i);  spring(i, i+1);
            /*vec3 x[] = {nodes[i-1].position, nodes[i].position, nodes[i+1].position};
            vec3 dx0 = x[1]-x[0], dx1 = x[2]-x[1];
            float l0 = length(dx0), l1=length(dx1);
            vec3 f = +k*(l/l0-1)*dx0 -k*(l/l1-1)*dx1;
            // dx[i] f
            mat3 dxf = k*(-1 + l/l0*(1-(1/dot(dx0,dx0))*outer(dx0,dx0)))
                           + k*(-1 + l/l1*(1-(1/dot(dx1,dx1))*outer(dx1,dx1)));
            for(int c0: range(3)) {
                for(int c1: range(3)) M(i*3+c0, i*3+c1) -= dt*dt*dxf(c0, c1);
                b[i*3+c0] += dt*( f[c0] + dt*dxf(c0, c0)*v[c0] );
            }
            // dx[i-1] f
            mat3 dxf0 = k*(+1 - l/l0*(1+(1/dot(dx0,dx0))*outer(dx0,dx0)));
            for(int c0: range(3)) for(int c1: range(3)) M(i*3+c0, (i-1)*3+c1) -= dt*dt*dxf0(c0, c1);
            // dx[i+1] f
            mat3 dxf1 = k*(+1 - l/l1*(1+(1/dot(dx1,dx1))*outer(dx1,dx1)));
            for(int c0: range(3)) for(int c1: range(3)) M(i*3+c0, (i+1)*3+c1) -= dt*dt*dxf1(c0, c1);*/
            //- wireTensionDamping * dot(n, nodes[b].velocity))
        }
        for(int i: range(1, nodes.size-1)) {
            // Torsion springs (Bending resistance)
            vec3 A = nodes[i-1].position, B = nodes[i].position, C = nodes[i+1].position;
            vec3 a = C-B, b = B-A;
            vec3 c = cross(a, b);
            float l = length(c);
            if(l) {
                float p = atan(l, dot(a, b));
                vec3 dap = cross(a, cross(a,b)) / (sq(a) * l);
                vec3 dbp = cross(b, cross(b,a)) / (sq(b) * l);
                // Explicit Euler
                nodes[i+1].acceleration += wireBendStiffness * (-p*dap) / nodeMass;
                nodes[i].acceleration += wireBendStiffness * (p*dap - p*dbp) / nodeMass;
                nodes[i-1].acceleration += wireBendStiffness * (p*dbp) / nodeMass;
                {//FIXME
                    vec3 A = nodes[i-1].velocity, B = nodes[i].velocity, C = nodes[i+1].velocity;
                    vec3 axis = cross(C-B, B-A);
                    if(axis) {
                        float angularVelocity = atan(length(axis), dot(C-B, B-A));
                        nodes[i].acceleration += (wireBendDamping * angularVelocity / 2.f * cross(axis/length(axis), C-A)) / nodeMass;
                    }
                }
            }
            /*// Elastic cylinder - cylinder contact
            auto contact = [this,&force](int i, int j) {
                vec3 A1 = nodes[i].position, A2 = nodes[i+1].position;
                vec3 B1 = nodes[j].position, B2 = nodes[j+1].position;
                vec3 A, B; closest(A1, A2, B1, B2, A, B);
                vec3 r = B-A;
                float d = wireRadius + wireRadius - length(r);
                if(d > 0) {
                    float E = 10; //1/((1-sq(wirePoisson))/wireYoung + (1-sq(wirePoisson))/wireYoung);
                    float kN = wireNormalDamping;
                    float R = 1/(1/wireRadius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = (nodes[i].velocity + nodes[i+1].velocity)/2.f
                            -(nodes[j].velocity + nodes[j+1].velocity)/2.f;
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v); // FIXME
                    force += fN * n;
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -particleFriction; // mu
                    float kT = particleTangentialDamping;
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                        force += fT;
                    }
                    assert(isNumber(force));
                }
            };
            for(int j: range(0, i-1)) contact(i, j);
            for(int j: range(i+2, nodes.size-1)) contact(i, j);
            assert(isNumber(force));
            nodes[i].acceleration += force / nodeMass;*/
        }
        /*// Particle dynamics
        for(Particle& p: particles) {
            // Leapfrog position integration
            p.velocity += dt * p.acceleration;
            p.position += dt * p.velocity;
            assert(isNumber(p.position));
            // PCDM rotation integration
            float I (2./3*p.mass*sq(p.radius)); // mat3
            vec3 w = (p.rotation.conjugate() * quat{0, p.angularVelocity} * p.rotation).v; // Local
            vec3 t = (p.rotation.conjugate() * quat{0, p.torque} * p.rotation).v; // Local
            vec3 dw = 1/I * (t - cross(w, I*w));
            vec3 w4 = w + dt/4*dw;
            float w4l = length(w4);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*w4/w4l} * p.rotation : p.rotation;
            vec3 w2 = w + dt/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Global
            float w2wl = length(w2w);
            // Correction (multiplicative update)
            p.rotation = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*w2w/length(w2w)} * p.rotation : p.rotation;
            p.angularVelocity = (p.rotation * quat{0, w + dt*dw} * p.rotation.conjugate()).v; // Global
        }*/
        // Anchors both ends (explicit)
        nodes.first().acceleration = 0;
        nodes.last().acceleration = 0;
        // Wire nodes dynamics
        for(Node& n: nodes) {
            // Leapfrog position integration
            n.velocity += dt * n.acceleration;
            n.position += dt * n.velocity;
            assert(isNumber(n.position), n.position, n.velocity, n.acceleration);
        }
        // Implicit Euler
        Vector dv = solve(move(M), b);
        // Anchors both ends (implicit)
        for(int c: range(3)) dv[0             *3+c] = 0;
        for(int c: range(3)) dv[(nodes.size-1)*3+c] = 0;
        for(int i: range(nodes.size)) {
            for(int c: range(3)) {
                assert(isNumber(dv[i*3+c]),"\n"_+str(M),"\n",b,"\n",dv);
                nodes[i].velocity[c] += dv[i*3+c];
                nodes[i].position[c] += dt*nodes[i].velocity[c];
            }
        }
        //error(M,"\n",b,"\n",dv);
        time += dt;
        timeStepCount++;
        if(timeStepCount%skip == 0) window->render();
        queue();
    }

    const float scale = 256;
    unique<Window> window = ::window(this, 1024);
    DEM() {
        /*window->actions[Space] = [this]{
            writeFile(section(title,' '), encodePNG(render(512, graphics(512))), home());
        };*/
        //window->presentComplete = {this, &DEM::step};
        //notes.append(vec3(1,0,floorHeight));
        const int N = 32;
        internodeLength = 3./N;
        for(int i: range(N)) {
            float x = float(i)/(N-1);
            nodes.append(vec3(x*2-1,0,-x));
        }
        step();
    }
    vec2 sizeHint(vec2) { return 1024; }
    shared<Graphics> graphics(vec2 size) {
        Image target = Image( int2(size) );
        target.clear(0xFF);
        ImageF zBuffer = ImageF(int2(size));
        zBuffer.clear(-inf);

        // Computes view projection transform
        mat4 projection = mat4()
                .translate(vec3(size/2.f,0))
                .scale(scale);
                //.perspective(PI/4, size, 1, 2*horizonDistance + length(position));
        mat4 view = mat4()
                .rotateX(rotation.y) // Pitch
                .rotateZ(rotation.x) // Yaw
                ;//.translate(-position); // Position
        mat4 viewProjection = projection*view;

        line(target, (viewProjection*vec3(-1, 0, floorHeight)).xy(),
             (viewProjection*vec3(1, 0, floorHeight)).xy());
        line(target, (viewProjection*vec3(0, -1, floorHeight)).xy(),
             (viewProjection*vec3(0, 1, floorHeight)).xy());

        /*sort<Particle>([&viewProjection](const Particle& a, const Particle& b) -> bool {
            return (viewProjection*a.position).z < (viewProjection*b.position).z;
        }, particles);*/

        for(auto p: particles) {
            vec3 O = viewProjection*p.position;
            vec2 min = O.xy() - vec2(scale*p.radius); // Isometric
            vec2 max = O.xy() + vec2(scale*p.radius); // Isometric
            for(int y: range(::max(0.f, min.y), ::min(max.y, size.y))) {
                for(int x: range(::max(0.f, min.x+1), ::min(max.x+1, size.x))) {
                    vec2 R = vec2(x,y) - O.xy();
                    if(length(R)<scale*p.radius) {
                        vec2 r = R / (scale*p.radius);
                        vec3 N (r, 1-length(r)); // ?
                        float z = O.z + N.z;
                        if(z < zBuffer(x,y)) continue;
                        zBuffer(x,y) = z;
                        float I = /*::max(0.f,*/ dot(N, vec3(0,0,1));//);
                        assert_(I<1);
                        extern uint8 sRGB_forward[0x1000];
                        target(x,y) = byte4(byte3(sRGB_forward[int(0xFFF*I)]),0xFF);
                        //assert_(N.x >= 0 && N.x < 1 && N.y >= 0 && N.y < 1 && N.z >= 0 && N.z < 1, N);
                        /*target(x,y) = byte4( // TODO: local coordinates
                                sRGB_forward[int(0xFFF*N.x)],
                                sRGB_forward[int(0xFFF*N.y)],
                                sRGB_forward[int(0xFFF*N.z)], 0xFF);*/
                    }
                }
            }
        }

        for(int i: range(nodes.size-1)) {
            vec3 a = nodes[i].position, b = nodes[i+1].position;
            vec3 A = viewProjection*a, B = viewProjection*b;
            vec2 r = B.xy()-A.xy();
            float l = length(r);
            vec2 t = r/l;
            vec2 n (t.y, -t.x);
            float width = scale*wireRadius;
            vec2 P[4] {A.xy()-width*n, A.xy()+width*n, B.xy()-width*n, B.xy()+width*n};
            vec2 min = ::min(P), max = ::max(P);
            for(int y: range(::max(0.f, min.y), ::min(max.y, size.y))) {
                for(int x: range(::max(0.f, min.x+1), ::min(max.x+1, size.x))) {
                    vec2 p = vec2(x,y) - A.xy();
                    if(dot(p,t) < 0 || dot(p,t) > l) continue;
                    if(dot(p,n) < -width || dot(p,n) > width) continue;
                    float z = A.z + dot(p,t)/l*(B.z-A.z);
                    float dz = 1-sq(dot(p,n)/width); // ?
                    if(z+dz < zBuffer(x,y)) continue;
                    float I = dz;
                    extern uint8 sRGB_forward[0x1000];
                    target(x,y) = byte4(byte3(sRGB_forward[int(0xFFF*I)]),0xFF);
                }
            }
        }

        shared<Graphics> graphics;
        graphics->blits.append(vec2(0),vec2(target.size),move(target));
        return graphics;
    }

    // View
    vec2 lastPos; // Last cursor position to compute relative mouse movements
    vec2 rotation = vec2(0, -PI/2); // Current view angles (yaw,pitch)
    // Orbital ("turntable") view control
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        vec2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            rotation += float(2.f*PI) * delta / size; //TODO: warp
            rotation.y= clamp(float(-PI/*2*/), rotation.y, 0.f); // Keep pitch between [-PI, 0]
        }
        else return false;
        return true;
    }
} view;
