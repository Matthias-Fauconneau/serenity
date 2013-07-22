#include "thread.h"

#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"

template<Type T> struct Grid {
    Grid(uint x, uint y):size(x,y),nodes(x*y){}
    T& operator()(uint x, uint y) { return nodes[y*size.x+x]; }
    int2 size;
    buffer<T> nodes;
};

struct Node {
    float phi[3*3]={};
    float phi2[3*3]={};
};

struct Test : Widget {
    Grid<Node> grid{16,16};

    Window window{this, grid.size*64, "Test"_};
    Test() {
        initialize();
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(' ').connect([]{step();});
    }

    void initialize() {
        for(int y: range(grid.size.y)) for(int x: range(grid.size.x)) {
            const Node& node = grid(x,y);
            for(int dy: range(3)) for(int dx: range(3)) phi[dy*3+dx] = 1./9;
        }
    }

    void step() {
        // Collision
        for(int y: range(grid.size.y)) for(int x: range(grid.size.x)) {
            const Node& node = grid(x,y);
            for(int dy: range(3)) for(int dx: range(3)) {
                float rho = 1;
                float dotvu = dot(v,u);
                float phieq = rho * ( 1 + dotvu/2 + sq(dotvu)/(2*sq(e)) - sq(u)/(2*e));
                phi2[dy*3+dx] = phi[dy*3+dx] - dt/tau*(phi[dy*3+dx]-phieq); //+dt*d/e*dot(k,g);
            }
        }
    }

    void render(int2 position, int2 size) {
        int2 cellSize = size/grid.size;
        for(int y: range(grid.size.y)) for(int x: range(grid.size.x)) {
            const Node& node = grid(x,y);
            int2 p0 = position+int2(x,y)*cellSize;
            line(p0+int2(0,0),p0+int2(cellSize.x,0));
            line(p0+int2(cellSize.x,0),p0+int2(cellSize.x,cellSize.y));
            line(p0+int2(cellSize.x,cellSize.y),p0+int2(0,cellSize.y));
            line(p0+int2(0,cellSize.y),p0+int2(0,0));
            for(int dy: range(3)) for(int dx: range(3)) {
                int2 p = p0+int2(dx,dy)*cellSize/3;
                Text(str(node.phi[dy*3+dx]/*,node.phieq[dy*3+dx]*/)).render(p,cellSize/3);
            }
        }
    }
} test;
