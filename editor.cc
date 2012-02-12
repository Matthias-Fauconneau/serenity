#include "process.h"
#include "window.h"
#include "file.h"
#include "gl.h"
#include "algebra.h"

SHADER(shader);

/// Dense 2D grid with convenient indexing
template<class T> struct Grid {
    no_copy(Grid)
    Grid(Grid&& o) : data(o.data), m(o.m), n(o.n) { o.data=0; }
    Grid& operator=(Grid&& o) {assert(&o!=this); this->~Grid(); data=o.data; m=o.m; n=o.n; o.data=0; return *this; }
    Grid() {}
    /// Allocates a m-row, n-column Grid
    Grid(int m, int n) : data(new T[m*n]), m(m), n(n) {}
    ~Grid() { if(data) delete data; }
    /// Initializes the grid
    void clear(T v=0) { ::set(data,m*n,v); }

    /// Indexing operators
    const T& at(int i, int j) const { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    const T& operator()(int i, int j) const { return at(i,j); }
    T& operator()(int i, int j) { return (T&)at(i,j); }
    const T& operator()(int2 p) const { return at(p.x,p.y); }
    T& operator()(int2 p) { return (T&)at(p.x,p.y); }

    T* data=0; /// elements stored in column-major order
    int m=-1,n=-1; /// row and column count
};

template<class Lambda> void line(int2 p0, int2 p1, Lambda set) {
    int2 d = abs(p1-p0);
    int2 s = sign(p1-p0);
    int e = d.x-d.y;
    for(;;) {
        set(p0);
        if(p0==p1) return;
        int e2 = 2*e;
        if(e2 > -d.y) e -= d.y, p0.x += s.x;
        if(e2 <  d.x) e += d.x, p0.y += s.y;
    }
}

/// Surface defined by a curve (TODO: constrained vertices),
struct Surface {
    array<int2> curve; //initial curve grid positions
    //TODO: constrained vertices
    array<vec3> vertices; //vertex positions
    array<int> triangles; //vertex indices for each triangle
    Grid<int2> grid; //map grid position -> vertex index (for both faces)
    array<int3> indices; //map vertex index -> grid position (for both faces)
    GLBuffer buffer{Triangle}; //handle to GPU memory

    const float edge = 64/*->16*/, median = edge*sqrt(3.0/4); //triangle resolution
    const int2 neighbours[6] ={int2(-1,-1),int2(0,-1),int2(1,0),int2(1,1),int2(0,1),int2(-1,0)};

    Surface(array<byte>&& data) : curve(array<int2>(move(data))) { generate(); }
    array<byte> data() { return array<byte>(curve.copy()); }

    /// Create new surface from input stroke
    void input(array<int2>&& stroke) {
        if(stroke.size<2) return;
        curve.clear(); // Clear any preexisting secondary data

        mat2 skew{{1/edge, 0, -1/(2*median), 1/median}};
        for(int2& input: stroke) input=int2(skew*vec2(input)); // Skew curve to triangle space (2 triangle / unit square)

        int2 origin{0,0}; for(int2 p: stroke) origin=min(origin,p);
        for(int2 input: stroke) { // Translate input stroke, remove duplicates
            int2 p = int2(input-origin);
            if(curve.size && curve.last()==p) continue;
            curve << p;
        }
        generate();
    }

    /// Generate new surface from stored initial curve (and TODO: constrained vertices)
    void generate() {
        if(curve.size<2) return;
        vertices.clear(); triangles.clear(); indices.clear(); // Clear any preexisting secondary data
        int2 size; for(int2 p: curve) size=max(size,p); int w=size.x,h=size.y; // Compute grid size
        static const int2 empty=int2(-2,-2), full=int2(-1,-1); //empty=no vertex, full=waiting for index allocation
        grid=Grid<int2>(w+2,h+2); grid.clear(empty);
        int2 seed{0,0}; int n=0;
        for(int i=0;i<curve.size;i++) line(curve[i],curve[(i+1)%curve.size], // Rasterize curve on grid
                                           [this,&seed,&n](int2 p){  seed+=p, n++;  grid(p)=full, grid(p+int2(1,0))=full, grid(p+int2(0,1))=full; });
        seed = seed/n;

        // Flood fill vertex grid (6-way) from curve
        int count=0;
        array<int2> stack;
        stack << seed; //FIXME: seed might be outside
        while(stack) {
            int2 p = stack.pop(); int x=p.x,y=p.y;
            if(x<0 || y<0 || x>w || y>h || grid(x,y)==full) continue;
            grid(x,y)=full;  //mark only. need a 2nd pass to allocate indices with boundary vertices common to both faces
            count++;
            stack << int2(x-1,y-1) << int2(x,y-1) << int2(x+1,y) << int2(x+1,y+1) << int2(x,y+1) << int2(x-1,y);
            assert(stack.size<=6*(w+1)*(h+1));
        }
        vertices.reserve(2*count); //preallocate arrays
        indices.reserve(2*count);
        triangles.reserve(2*count*3);

        //TODO: optimize surface boundary

        // Convert vertex grid to geometry (vertices + triangles)
        for(int y=0;y<=h;y++) for(int x=0;x<=w;x++) { //foreach vertex on grid
            int2& index = grid(x,y);
            if(index==empty) continue;
            bool inner=true;
            for(int2 d: neighbours) { int2 p=int2(x,y)+d; inner |= (p>=int2(0,0) && p<=int2(w,h) && grid(p)==empty); }
            for(int z=0;z<2;z++) { //for both side
                if(index[z]<0) { //add vertices as necessary
                    index[z]=vertices.size;
                    indices << int3(x,y,inner?z:-1);
                    mat2 unskew{{edge,0,edge/2,median}};
                    vec2 xy = unskew*vec2(int2(x,y)-size/2);
                    vertices << vec3(xy.x,xy.y,inner?(z?1:-1):0);
                    if(!inner) { index[1]=index[0]; break; } //merge boundary vertices
                }
            }
        }
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) { //foreach vertex on grid
            if(grid(x,y)==empty) continue;
            //triangle list for rendering (TODO: direct winding)
            if(x<w && y<h && grid(x+1,y)!=empty && grid(x,y+1)!=empty) for(int n=0;n<2;n++) triangles << grid(x,y)[n] << grid(x,y+1)[n] << grid(x+1,y)[n];
            if(x>0 && y>0 && grid(x-1,y)!=empty && grid(x,y-1)!=empty) for(int n=0;n<2;n++) triangles << grid(x,y)[n] << grid(x,y-1)[n] << grid(x-1,y)[n];
            //TODO: incident edges for edge optimization
        }
        buffer.upload(triangles);
        buffer.upload(vertices);
        //optimize();
    }

    /// foreach adjacent vertex
    template<class Lambda> void adjacent(int i, Lambda lambda) {
        int3 p = indices[i];
        if(p.z>=0) { // inner vertex -> iterate one face
            for(int2 d: neighbours) { int j=grid(p.xy()+d)[p.z]; if(j>=0) lambda(i,j); }
        } else { // boundary vertex -> iterate both side
            for(int2 d: neighbours) { int j=grid(p.xy()+d)[0]; if(j>=0) lambda(i,j); }
            for(int2 d: neighbours) { int j=grid(p.xy()+d)[1]; if(j>=0) lambda(i,j); }
        }
    }

    /// Optimize current surface to minimize curvature (TODO: constrained vertices)
    void optimize() {
        int N = vertices.size;
        //solve minimum curvature laplacian |(L+I)c-c'|²
        Matrix A(N,N);
        for(int i=0;i<N;i++) {
            for(int j=0;j<N;j++) A(i,j)=0; //TODO: sparse
            A(i,i)=1; //TODO: on first iteration, +1 only on curve
            adjacent(i,[&A](int i,int j){ A(i,j)=1; A(i,i)--; }); // Discrete Laplace operator on the mesh at c[i]
        }
        Vector c(N);
        for(int i=0;i<N;i++) c[i]=0; //TODO: initial curvature
        c = solve(A,c);
        log("c"_,c);
        //solve edge lengths reusing same matrix |(L+I)e-e'|²
        Vector e(N);
        for(int i=0;i<N;i++) e[i]=0; //TODO: initial edge length
        e = solve(A,e);
        log("e"_,e);
        //solve target vertex position | (L + curve?I + incident?I-J) x - (δ + curve?v' + incident?η[ij]) |²
        //TODO: vectorized solver
        Matrix B(3*N,3*N);
        for(int i=0;i<N;i++) {
            for(int k=0;k<3;k++) for(int j=0;j<3*N;j++) B(3*i+k,j)=0; //TODO: sparse
            for(int k=0;k<3;k++) B(3*i+k,3*i+k) = 1; //TODO: +1 only on curve, +1 on incident I, -1 on incident J
            adjacent(i,[&B](int i,int j){ for(int k=0;k<3;k++) B(i*3+k,j*3+k)=1, B(i*3+k,i*3+k)--; }); // Discrete Laplace operator
        }
        Vector v(3*N);
        for(int i=0;i<N;i++) {
            vec3 normal;
            adjacent(i, [&normal,this](int i,int j){ normal += cross(vertices[i],vertices[j]); }); //sum all cross products
            float area = length(normal);
            normal = normal/area;
            for(int k=0;k<3;k++) v[3*i+k]=area*normal[k]*c[3*i+k]; //TODO: target curvature
            //TODO: constrained vertices
            //TODO: target edge length
        }
        v = solve(B,v);
        copy((float*)vertices.data,v.data,3*N); //FIXME
        buffer.upload(vertices);
    }

    /// Render current surface
    void render(const mat4& view) {
        shader.bind(); shader["view"]=view; shader["color"]=vec4(0,0,0,1);

        /*{
            GLBuffer buffer{LineLoop};
            int2 size{0,0}; for(int2 p: curve) size=max(size,p);
            mat2 unskew{{edge,0,edge/2,median}};
            array<vec3> vertices; for(int2 p:curve) { vec2 xy = unskew*vec2(p-size/2); vertices<<vec3(xy.x,xy.y,0); }
            buffer.upload(vertices);
            buffer.bindAttribute(shader,"position",3,0);
            buffer.draw();
        }*/

        if(!buffer) return;
        glWireframe();
        buffer.bindAttribute(shader,"position",3,0);
        buffer.draw();
    }
};

/// Convenience class to create a single window Application with a custom interface
/// \note inherit Application as required by process.main()
/// \note inherit Widget to implement a custom GUI (by user class)
struct MainWindow : Application, Widget {
    Window window{*this,int2(1024,768),""_};
    MainWindow() { window.show(); }
    bool keyPress(Key key) { if(key==Escape) quit(); return false; }
};

/// Application to view and edit a \a Surface
struct Editor : MainWindow {
    Surface surface{exists("surface"_)?mapFile("surface"_):""_}; //load test file
    ~Editor(){ write(createFile("surface"_),surface.data()); }

    void render(int2) override {
        surface.render(view());
    }

    /// View
    vec2 position,rotation; float scale=1;
    mat4 view() {
        mat4 view;
        view.scale(vec3(scale/(size.x/2),-scale/(size.y/2),-scale/size.y));
        /*view.rotateX(rotation.y);
        view.rotateZ(rotation.x);*/
        return view;
    }

    array<int2> stroke; //current user input stroke

    /// Input
    //int2 lastPos;
    bool mouseEvent(int2 input, Event event, Button button) override {
        //int2 delta = pos-lastPos; lastPos=pos;
        //vec2 viewPos = (2*vec2(input)/vec2(size.x,-size.y)-vec2(1,-1));
        //vec2 pos = ( view().inverse() * vec4(viewPos.x,viewPos.y,0,1) ).xy();
        if(event==Press && button==LeftButton) {
            stroke.clear();
            stroke << input;
        } else if(event==Motion && button==LeftButton) {
            stroke << input;
            //rotation += delta*PI/size;
        } /*else if(event==Press && button==WheelDown) {
            scale *= 17.0/16;
        } else if(event==Press && button==WheelUp) {
            scale *= 15.0/16;
        }*/ else return false;
        surface.input(stroke.copy());
        return true;
    }
} editor;
