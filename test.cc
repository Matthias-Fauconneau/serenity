#include "thread.h"

#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"

template<Type T> struct Grid {
    Grid(int2 size):size(size),nodes(size.x*size.y){}
    T& operator()(uint x, uint y) { assert(x<size.x && y<size.y, x,y); return nodes[y*size.x+x]; }
    int2 size;
    buffer<T> nodes;
};

struct Node {
    float phi[3*3]={};
    float operator()(uint dx, uint dy) const { assert(dx<3 && dy<3,int(dx),int(dy)); return phi[dy*3+dx]; }
    float& operator()(uint dx, uint dy) { assert(dx<3 && dy<3,int(dx),int(dy)); return phi[dy*3+dx]; }
};

struct Test : Widget {
    int2 gridSize{16,16};
    Grid<Node> source{gridSize};
    Grid<Node> target{gridSize};

    Window window{this, gridSize*64, "Test"_};
    Test() {
        initialize();
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect([this]{step();});
    }

    void initialize() {
        for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Node& node = source(x,y);
            for(int dy: range(3)) for(int dx: range(3)) node(dx,dy) = 1;
            //for(int dy: range(3)) for(int dx: range(3)) node(dx,dy) = 0; node(1,1) = 1;
        }
    }

    void step() {
        // Collision
        const float dx = 1/3.7; // Spatial step [m]
        const float dt = 1./60; // Time step [s]
        const float c = dx/dt; // Lattice velocity δx/δt [m/s]
        const float e = sq(c)/3; // Energy density c = √(3e) [m²/s²]
        const vec2 g = vec2(0,/*9.8*/0); // Body Force: Earth gravity pull [m/s²]
        const float rho = 1/*0e3*/; // Mass density: ρ[water] [kg/m³]
        const float nu = 1/*0e-6*/; // Kinematic viscosity: ν[water] [m²/s]
        const float tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
        for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            const Node& node = source(x,y);
            vec2 u = dt/2*g;
            for(int dy: range(3)) for(int dx: range(3)) { // Compute node mean velocity
                const float W[] = {1./6, 2./3, 1./6};
                const float w = W[dx]*W[dy];
                const vec2 v = c * vec2(dx-1,dy-1);
                u += w * v * node(dx,dy) / rho;
            }
            for(int dy: range(3)) for(int dx: range(3)) { // BWK relaxation (+body force)
                const vec2 v = c * vec2(dx-1,dy-1);
                const float dotvu = dot(v,u);
                const float phieq = rho * ( 1 + dotvu/2 + sq(dotvu)/(2*sq(e)) - sq(u)/(2*e));
                const vec2 k = rho * ( 1/sqrt(e) * (v-u) + dotvu/sqrt(cb(e)) * v );
                target(x,y)(dx,dy) = node(dx,dy) - dt/tau*(node(dx,dy)-phieq) + dt/e*dot(k,g);
            }
        }
        // Streaming
        for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            for(int dy: range(3)) for(int dx: range(3)) {
                int sx=x-(dx-1), sy=y-(dy-1);
                if(sy<0) sy=gridSize.y-1; // Periodic horizontal boundary
                if(sy>=gridSize.y) sy=0; // Periodic horizontal boundary
                int tx=dx-1, ty=dy-1;
                if(sx<0 || sx>=gridSize.x) { sx=x, tx=-tx, ty=-ty; } // No slip vertical boundary
                source(x,y)(dx,dy) = target(sx,sy)(tx+1,ty+1);
            }
        }
        window.render();
    }

    void render(int2 position, int2 size) {
        int2 cellSize = size/gridSize;
        for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            const Node& node = source(x,y);
            int2 p0 = position+int2(x,y)*cellSize;
            line(p0+int2(0,0),p0+int2(cellSize.x,0));
            line(p0+int2(cellSize.x,0),p0+int2(cellSize.x,cellSize.y));
            line(p0+int2(cellSize.x,cellSize.y),p0+int2(0,cellSize.y));
            line(p0+int2(0,cellSize.y),p0+int2(0,0));
            for(int dy: range(3)) for(int dx: range(3)) {
                int2 p = p0+int2(dx,dy)*cellSize/3;
                Text(ftoa(node.phi[dy*3+dx],0)).render(p,cellSize/3);
            }
        }
    }
} test;
