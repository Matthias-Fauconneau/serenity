/// \file fibermesh.cc Incomplete implementation of Fibermesh
#include "process.h"
#include "window.h"
#include "file.h"
#include "sparse.h"
#include "matrix.h"
#include "data.h"
#include "display.h"
#include "text.h"

/// Dense 2D grid with convenient indexing
template<class T> struct Grid {
    no_copy(Grid);
    Grid(Grid&& o) : data(o.data), m(o.m), n(o.n) { o.data=0; }
    Grid& operator=(Grid&& o) {assert(&o!=this); this->~Grid(); data=o.data; m=o.m; n=o.n; o.data=0; return *this; }
    Grid() {}
    /// Allocates a m-row, n-column Grid
    Grid(int m, int n) : data(allocate<T>(m*n)), m(m), n(n) {}
    ~Grid() { if(data) unallocate(data,m*n); }
    /// Initializes the grid
    void clear(T v=0) {assert(data && m>0 && n>0); ::clear((T*)data,m*n,v); }

    bool inside(int2 p) { return uint(p.x)<m && uint(p.y)<n; }

    /// Indexing operators
    const T& at(uint i, uint j) const { assert(data && i<m && j<n); return data[j*m+i]; }
    const T& operator()(uint i, uint j) const { return at(i,j); }
    T& operator()(uint i, uint j) { return (T&)at(i,j); }
    const T& operator()(int2 p) const { assert(p.x>=0 && p.y>=0); return at(p.x,p.y); }
    T& operator()(int2 p) { assert(p.x>=0 && p.y>=0); return (T&)at(p.x,p.y); }

    const T* data=0; /// elements stored in column-major order
    uint m=-1,n=-1; /// row and column count
};

/// Surface defined by a curve (TODO: constrained vertices)
struct Surface {
    array<int2> stroke;

    const float edge = 256/*->16*/, median = edge*sqrt(3.f/4); //triangle resolution
    const mat2 skew __(1/edge, 0, -1/(2*median), 1/median);
    const mat2 unskew __(edge,0,edge/2,median);

    array<int2> curve; //initial curve grid positions

    Grid<int2> grid; //map grid position -> vertex index (for both faces)
    const int2 empty=int2(-2,-2), full=int2(-1,-1); //empty=no vertex, full=waiting for index allocation

    //TODO: constrained vertices
    array<vec3> vertices; //vertex positions
    array<int3> triangles; //vertex indices for each triangle
    array<int3> indices; //map vertex index -> grid position (for both faces)
    map<vec3, string> debug;
    struct Line { vec3 a,b; byte4 color; };
    array<Line> lines;

    const int2 neighbours[6] ={int2(0,-1),int2(1,-1),int2(1,0),int2(0,1),int2(-1,1),int2(-1,0)};

    Surface(const ref<byte>& data) : curve(cast<int2>(data)) { generate(); }
    ref<byte> data() { return cast<byte,int2>(curve); }

    /// Creates new surface from input stroke
    void input(const ref<int2>& stroke) {
        if(stroke.size<2) return;
        this->stroke=array<int2>(stroke); //DEBUG
        array<int2> curve;

        int2 origin=0;//stroke[0];
        for(int2 input: stroke) { // Skew curve to triangle space (2 triangle / unit square)
            vec2 t = skew*vec2(input);
            vec2 a00 = unskew*vec2(int2(t)+int2(0,0));
            vec2 a01 = unskew*vec2(int2(t)+int2(0,1));
            vec2 a10 = unskew*vec2(int2(t)+int2(1,0));
            vec2 a11 = unskew*vec2(int2(t)+int2(0,0));
            vec2 p = vec2(input);
            int2 best = int2(t)+int2(0,0); float min=length(p-a00);
            {float d=length(p-a01); if(d<min) best=int2(t)+int2(0,1);}
            {float d=length(p-a10); if(d<min) best=int2(t)+int2(1,0);}
            {float d=length(p-a11); if(d<min) best=int2(t)+int2(1,1);}
            if(curve.size()>=1 && curve.last()==best) continue; // Removes duplicates
            else if(curve.size()>=2) { // Forces convex curves (TODO: convex hull)
                int2 A = curve[curve.size()-2], B=curve.last();
                if(cross(B-A, best-A)<=0) { curve.pop(); continue; } // previous point (last)
                if(cross(best-B, curve.first()-B)<=0) { continue; } // current point
                if(cross(curve.first()-best, curve[1]-best)<=0) { curve.take(0); continue; } // next point (first)
            }
            curve << best;
            origin=::min(origin, best);
        }
        for(int2& point: curve) point -= origin; // Normalize stroke
        if(curve!=this->curve) { this->curve=move(curve); generate(); }
    }

    bool inner(int x, int y) {
        for(int2 d: neighbours) { int2 p=int2(x,y)+d; if(!grid.inside(p) || grid(p)==empty) return false; }
        return true;
    }

    /// Generates a new surface from stored initial curve (and TODO: constrained vertices)
    void generate() {
        if(curve.size()<2) return;
        int2 size=0; for(int2 p: curve) size=max(size,p);
        if(size.x<=0 || size.y<=0) return;
        vertices.clear(); triangles.clear(); indices.clear(); // Clear any preexisting secondary data
        int w=size.x,h=size.y; // Compute grid size
        grid=Grid<int2>(w+2,h+2); grid.clear(empty);

        // Rasterize convex polygon (TODO: concave)
        int count=0;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            int2 last = curve.last();
            for(int2 p: curve) {
                if(cross(p-last, int2(x,y)-last)<0) goto outside;
                last = p;
            }
            grid(x,y)=full; count++;
            outside:;
        }

        vertices.reserve(2*count); //preallocate arrays
        indices.reserve(2*count);
        triangles.reserve(2*count);

        //TODO: optimize surface boundary

        // Convert vertex grid to geometry (vertices + triangles)
        for(int y=0;y<=h;y++) for(int x=0;x<=w;x++) { //foreach vertex on grid
            int2& index = grid(x,y);
            if(index==empty) continue;
            bool inner=this->inner(x,y);
            for(int z=0;z<2;z++) { //for both side
                if(index[z]<0) { //add vertices as necessary
                    index[z]=vertices.size();
                    indices << int3(x,y,inner?z:-1);
                    vec2 xy = unskew*vec2(int2(x,y));
                    vertices << vec3(xy.x,xy.y,inner?(z?1:-1):0);
                    if(!inner) { index[1]=index[0]; break; } //merge boundary vertices
                }
            }
        }

        for(int y=0;y<h;y++) for(int x=0;x<w;x++) { //foreach vertex on grid
            if(grid(x,y)==empty) continue;
            //triangle list for rendering (TODO: direct winding)
            if(x<w && y<h && grid(x+1,y)!=empty && grid(x,y+1)!=empty) {
                for(int z=0;z<2;z++) triangles << int3(grid(x,y)[z], grid(x,y+1)[z], grid(x+1,y)[z]);
            }
            if(x>0 && y>0 && grid(x-1,y)!=empty && grid(x,y-1)!=empty) {
                for(int z=0;z<2;z++) triangles << int3(grid(x,y)[z], grid(x,y-1)[z], grid(x-1,y)[z]);
            }
            //TODO: incident edges for edge optimization
        }
        optimize();
    }
#if 1
    /// Adjacent vertices iterator
    template<class Lambda> void adjacent(int i, Lambda lambda) {
        int3 p = indices[i]; int2 o=p.xy(); int z=p.z;
        if(z>=0) { // inner vertex -> iterate one face
            for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[z]; if(j>=0) lambda(i,j); }
        } else { // boundary vertex -> iterate both side
            for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[0]; if(j>=0) lambda(i,j); }
            for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[1]; if(j>=0) lambda(i,j); }
        }
    }

    /// Adjacent triangles iterator
    template<class Lambda> void adjacentFaces(int i, Lambda lambda) {
        int3 p = indices[i]; int2 o=p.xy(); int z=p.z;
        if(z==0) { // inner vertex -> iterate one face
            int last=-1;
            for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[z]; if(j>=0) { if(last>=0) { lambda(i,j,last); } last=j; } else last=-1; }
            {int2 d=neighbours[0]; if(grid.inside(o+d)){ int j=grid(o+d)[z]; if(j>=0) { if(last>=0) { lambda(i,j,last); } last=j; } else last=-1; }}
        } else if(z==1) { // inner vertex -> iterate one face in reverse winding
            int last=-1;
            for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[z]; if(j>=0) { if(last>=0) { lambda(i,last,j); } last=j; } else last=-1; }
            {int2 d=neighbours[0]; if(grid.inside(o+d)){ int j=grid(o+d)[z]; if(j>=0) { if(last>=0) { lambda(i,last,j); } last=j; } else last=-1; }}
        } else { // boundary vertex -> iterate both side
            {
                int last=-1;
                for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[0]; if(j>=0) { if(last>=0) { lambda(i,j,last); } last=j; } else last=-1; }
                {int2 d=neighbours[0]; if(grid.inside(o+d)){ int j=grid(o+d)[0]; if(j>=0) { if(last>=0) { lambda(i,j,last); } last=j; } else last=-1; }}
            }
            {
                int last=-1;
                for(int2 d: neighbours) if(grid.inside(o+d)){ int j=grid(o+d)[1]; if(j>=0) { if(last>=0) { lambda(i,last,j); } last=j; } else last=-1; }
                {int2 d=neighbours[0]; if(grid.inside(o+d)){ int j=grid(o+d)[1]; if(j>=0) { if(last>=0) { lambda(i,last,j); } last=j; } else last=-1; }}
            }
        }
    }

    /// Optimizes current surface to minimize curvature (TODO: constrained vertices)
    void optimize() {
        debug.clear(); lines.clear();
        int N = vertices.size();
        // Solves minimum curvature laplacian |(L+I)c-c'|²
        Matrix A(2*N,N); //FIXME: N+C on first iteration
        A.clear(0);
        for(int i=0;i<N;i++) {
            int n=0; adjacent(i,[&n](int,int){ n++; });
            assert(n);
            A(i,i)=-1;
            adjacent(i,[n,&A](int i,int j){ A(i,j)=1.f/n; }); // Minimizes Laplacian of curvature
            if(indices[i].z==-1) A(N+i,i)=1; //on first iteration, only add curvature changes minimization to boundary vertices
        }
        log(A);
        Vector c(2*N); c.clear(0);
        for(int i=0;i<N;i++) if(indices[i].z==-1) {
            vec3 L=0; int n=0;
            adjacent(i,[this,&L,&n](int i,int j){
                vec3 d = vertices[j]-vertices[i];
                //debug[vec3(int3((vertices[j]+vertices[i])/2.f))] << str(i);
                L+=d;
                n++;
            });
            L/=n;
            c[N+i]=length(L); //TODO: compute initial curvature on first iteration
        }
        //for(int i=0;i<N;i++) debug[vertices[i]]=str(c[N+i]);
        c = solve(A,c);
        log("c"_,c);
        //for(int i=0;i<N;i++) debug[vertices[i]]=str(c[i]);
        /*// Solves average edge lengths reusing same matrix |(L+I)e-e'|²
        Vector e(N);
        for(int i=0;i<N;i++) { // Current average length of all edges incident on vertex i
            float sum=0; int N=0;
            adjacent(i,[this,&sum,&N](int i,int j){ sum+=length(vertices[i]-vertices[j]); N++; });
            e[i]=sum/N;
        }
        e = solve(A,e);
        log("e"_,e);*/
        // Solves target vertex position | (L + curve?I + incident?I-J) x - (δ + curve?v' + incident?η[ij]) |²
        //TODO: vectorized solver
        Matrix B(6*N, 3*N); // Minimizes difference with target laplacian vector |L(v) - A·c·n| and curve distance |c - c'| //FIXME: 3N+3C
        B.clear(0);
        for(int i=0;i<N;i++) { //for each vertex
            int n=0; adjacent(i,[&n](int,int){ n++; });
            for(int k=0;k<3;k++) B(3*i+k,3*i+k)=-1;
            adjacent(i,[n,&B](int i,int j){ for(int k=0;k<3;k++) B(3*i+k,3*j+k)=1.f/n; }); // Discrete Laplace operator
            for(int j=0;j<3*N;j++) if(indices[i].z==-1) for(int k=0;k<3;k++) B(3*N+3*i+k,j)=1; // Minimizes curve points squared distance to initial curve
            // ([ TODO: +1 only on curve, +1 on incident I, -1 on incident J ]) ?
        }
        Vector v(6*N); v.clear(0);
        for(int i=0;i<N;i++) {
            vec3 area=0;
            adjacentFaces(i, [&area,this](int i, int j, int k){ //sum all adjacent triangle areas
                vec3 a = cross(vertices[j]-vertices[i],vertices[k]-vertices[i]);
                area += a;
                vec3 O = (vertices[i]+vertices[j]+vertices[k])/3.f;
                lines << Line __(O,O+a/4.f,red);
            });
            log(area);
            debug[vertices[i]]=str(area);
            lines << Line __(vertices[i],vertices[i]+area/4.f,green);
            for(int k=0;k<3;k++) {
                v[3*i+k] = area[k]*c[i]; // target curvature
            }
            //TODO: target edge length
        }
        for(int i=0;i<N;i++) if(indices[i].z==-1) { //C
            for(int k=0;k<3;k++) v[3*N+3*i+k] =vertices[i][k]; // constrained curve vertices
        }
        v = solve(B,v);
        log("v",v);
        //copy((float*)vertices.data(),v.data,3*N); //FIXME
        //buffer.upload(vertices);
    }
#endif
    /// Render current surface
    void render(const mat4& view) {
        /*if(stroke) {
            int2 last=stroke.last();
            for(int2 p: stroke) { line(last,p,1,green); last = p; }
        }
        if(curve) {
            vec2 xy = unskew*vec2(curve.last());
            vec2 last = (view*vec3(xy.x,xy.y,0)).xyz().xy();
            for(int2 p: curve) {
                vec2 xy = unskew*vec2(p);
                xy = (view*vec3(xy.x,xy.y,0)).xyz().xy();
                line(last,xy,1,blue);
                last = xy;
            }
        }*/
        for(int3 p: triangles) {
            vec2 A = (view*vertices[p.x]).xyz().xy();
            vec2 B = (view*vertices[p.y]).xyz().xy();
            vec2 C = (view*vertices[p.z]).xyz().xy();
            line(A,B,1,black); line(B,C,1,black); line(C,A,1,black);
        }
        for(int x : range(grid.m)) for(int y : range(grid.n)) {
            vec2 xy = unskew*vec2(int2(x,y));
            xy = (view*vec3(xy.x,xy.y,0)).xyz().xy();
            if(grid(x,y)!=empty) fill(int2(xy)+Rect(4),inner(x,y)?red:blue);
        }
        for(pair<vec3,string> label: debug) {
            vec2 p = (view*label.key).xyz().xy();
            Text(copy(label.value),12).render(int2(p));
        }
        for(Line line: lines) {
            vec2 A = (view*line.a).xyz().xy();
            vec2 B = (view*line.b).xyz().xy();
            ::line(A,B,1,line.color);
        }
    }
};

/// Application to view and edit a \a Surface
struct Editor : Widget {
    Window window __(this,int2(1024,1024*sqrt(3.f/4)),"Editor"_);
    Editor() { window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF; }

    Surface surface{existsFile("surface"_)?readFile("surface"_):""_}; //load test file
    ~Editor(){ writeFile("surface"_,surface.data()); }

    void render(int2 position, int2 size) override {
        surface.render(view(position,size));
    }

    /// View
    vec2 position,rotation; float scale=1;
    mat4 view(int2 unused position, int2 unused size) {
        mat4 view;
        //view.translate(position);
        view.scale(scale);
        /*view.rotateX(rotation.y);
        view.rotateZ(rotation.x);*/
        return view;
    }

    array<int2> stroke; //current user input stroke

    /// Input
    //int2 lastPos;
    bool mouseEvent(int2 input, int2 unused size, Event event, Button button) override {
        //int2 delta = pos-lastPos; lastPos=pos;
        //vec2 viewPos = (2*vec2(input)/vec2(size.x,-size.y)-vec2(1,-1));
        //vec2 pos = ( view(int2(0,0),size).inverse() * vec4(viewPos.x,viewPos.y,0,1) ).xy();
        if(event==Press && button==LeftButton) {
            stroke.clear();
            stroke << input;
            surface.input(stroke);
        } else if(event==Motion && button==LeftButton) {
            stroke << input;
            surface.input(stroke);
            //rotation += delta*PI/size;
        } /*else if(event==Press && button==WheelDown) {
            scale *= 17.0/16;
        } else if(event==Press && button==WheelUp) {
            scale *= 15.0/16;
        }*/ else return false;
        return true;
    }
} editor;
