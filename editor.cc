#include "process.h"
#include "interface.h"
#include "file.h"
#include "gl.h"
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"

/// Stream

/*struct Vertex { vec3 position; vec3 normal; Vertex(vec3 position, vec3 normal):position(position),normal(normal){}};
struct VertexStream {
	Vertex* data; int count=0;
	vec3 bbMin,bbMax;
	VertexStream(void* data) : data((Vertex*)data) {}
	VertexStream& operator <<(const Vertex& v) { if(count==0)bbMin=bbMax=v.position; bbMin=min(bbMin,v.position); bbMax=max(bbMax,v.position); data[count++]=v; return *this; }
};

struct IndexStream {
	uint* data; int count=0;
	IndexStream(void* data) : data((uint*)data) {}
	IndexStream& operator <<(const uint& v) { data[count++]=v; return *this; }
};*/

SHADER(light);
GLShader shader{lightShader,"lambert"};

struct Surface {
    //vec2 input; //debug
    array<vec2> curve;

    /*array<Vertex> vertices;
    array<uint> indices;
    GLBuffer buffer;*/

    /*vec3 bbMin, bbMax;
    vec3 center, extent;
    int planeIndex;*/

    Surface() {
        if(exists("surface")) curve = array<vec2>(mapFile("surface"));
	}
    ~Surface() { if(curve.capacity) write(createFile("surface"),curve.data,curve.size*sizeof(vec2)); }

    /*struct Line { int x1, x2, y, dy; };
    void quickfill(bool* bitmap, int w, int h, int x, int y) {
        array<Line> stack;
        stack << Line{x, x, y, 1}; //needed in some cases
        stack << Line{x, x, y+1, -1}; //seed segment
        while(stack) { Line l=stack.takeLast(); int x1=l.x1, x2=l.x2, y=l.y, dy=l.dy;
            if((x1%2)^(y%2) == dy?0:1) continue; //triangle grid connectivity
            y+=dy; if(y < 0 || y >= h) continue;
            for(x = x1;x >= 0 && !bitmap[y*w+x]; x--) bitmap[y*w+x]=1;
            int left = x+1;
            if(x >= x1) goto SKIP;
            if(left < x1) stack << Line{left, x1-1, y, -dy}; //leak on left?
            x = x1+1;
            do {
                for(;x<w && !bitmap[y*w+x]; x++) bitmap[y*w+x]=1;
                stack << Line{left, x-1, y, dy};
                if(x > x2+1) stack << Line{x2+1, x-1, y, -dy}; //leak on right?
                SKIP: for(x++; x <= x2 && bitmap[y*w+x]; x++) {}
                left = x;
            } while(x <= x2);
        }
    }*/

    void floodfill(bool* bitmap, int w, int h, int x, int y) { //naive recursive flood fill (3-way)
        if(x<0 || y<0 || x>=w || y>=h || bitmap[y*w+x]) return;
        bitmap[y*w+x]=1;
        floodfill(bitmap,w,h,x-1,y);
        floodfill(bitmap,w,h,x+1,y);
        floodfill(bitmap,w,h,x,y+((x%2)^(y%2)?1:-1));
    }

    void render(const mat4& view) {
        if(curve.size<2) return;
        shader.bind(); shader["view"]=view; shader["color"]=vec4(0,0,0,1);

        // Compute bounds
        vec2 m,M; m=M=curve.first();
        for(vec2 v: curve) m=min(m,v), M=max(M,v);

        // Pad bounds
        const float edge = 64, median = edge*sqrt(3.0/4);
        m -= vec2(edge,median), M += vec2(edge,median);
        vec2 origin=m, center = (m+M)/2, size=M-m;

        // Draw grid
        array<vec2> grid;
        int w=size.x/edge+2, h=size.y/median+2;
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            grid << origin+vec2(x-1+(y%2)/2.0,y)*vec2(edge,median);
        }

        // Rasterize curve in triangle grid
        bool faceGrid[(w*2)*h]; clear(faceGrid,(w*2)*h);
        array<vec2> triangle;
        for(vec2 input: curve) {
            vec2 pos=(input-origin)/vec2(edge/2,median);
            int x=pos.x, y=pos.y; float u=pos.x-x, v=pos.y-y;
            if((x&1)^(y&1)) { if(v+u<1) x--; } else { if(v-u>0) x--; }
            assert(x>=0 && y>=0 && x<w*2 && y<h,input,pos,x,y,w*2,h);
            if(faceGrid[y*(w*2)+x]) continue;
            faceGrid[y*(w*2)+x]=1;
        }

        // Quickfill
        floodfill(faceGrid, w*2, h, w, h/2);

        //TODO: optimize boundary

        // Convert triangle grid to geometry
        for(int y=0;y<h;y++) for(int x=0;x<w*2;x++) {
            if(!faceGrid[y*(w*2)+x]) continue;
            bool X=x&1,Y=y&1;
#define vertex(x,y) origin+vec2((2*(x)+((y)&1))*(edge/2),(y)*median)
            triangle << vertex(x/2+X,y+X) << vertex(x/2+!X,y+(X^Y)) << vertex(x/2+X,y+!X);
        }

        {
            //GLBuffer buffer{LineLoop};
            GLBuffer buffer{Point};
            buffer.upload(curve);
            buffer.bindAttribute(shader,"position",2,0);
            buffer.draw();
        }

        {
            glPolygonMode(GL_FRONT,GL_LINE);
            GLBuffer buffer{Triangle};
            buffer.upload(triangle);
            buffer.bindAttribute(shader,"position",2,0);
            buffer.draw();
        }
    }
};

struct Editor : Application, Widget {
    vec2 position,rotation; float scale=1;
    mat4 view;

    Surface surface;

    Window window = Window(*this,int2(1024,768),_("Editor"));
    /*void start(array<string>&&) override {
        //const int O[][6] = {{0,0,1,0,0,1},{1,0,1,1,0,1},{0,0,1,1,0,1},{1,0,1,1,0,0}};
        const int O[][6] =   {{0,0,1,0,0,1},{0,1,0,0,1,0},{0,0,1,1,0,1},{1,0,1,1,0,0}};
        for(int a=0;a<6;a++) for(int b=0;b<6;b++) {
            for(int c=0;c<6;c++) { for(int i=0;i<6;i++) log_(O[0][(0+i)%6]?".":" "); log_(" \t"); }
            log("");
            for(int c=0;c<6;c++) { for(int i=0;i<6;i++) log_(O[1][(a+i)%6]?".":" "); log_(" \t"); }
            log("");
            for(int c=0;c<6;c++) { for(int i=0;i<6;i++) log_(O[2][(b+i)%6]?".":" "); log_(" \t"); }
            log("");
            for(int c=0;c<6;c++) { for(int i=0;i<6;i++) log_(O[3][(c+i)%6]?".":" "); log_(" \t"); }
            log("");
            for(int c=0;c<6;c++) {
                for(int i=0;i<6;i++) log_(((O[0][i]&O[1][(a+i)%6]&O[2][(b+i)%6]&O[3][(c+i)%6])||!(O[0][i]|O[1][(a+i)%6]|O[2][(b+i)%6]|O[3][(c+i)%6]))?"|":" ");
                log_(" \t");
            }
            log("\n");
        }
        running=false;
    }*/
    void render(vec2, vec2) override {
        view = mat4();
        view.scale(vec3(scale/(size.x/2),scale/(size.y/2),-scale/size.y));
        view.rotateX(rotation.y);
        view.rotateZ(rotation.x);
        //view.translate(vec3(-position.x,-position.y,0));
        //log(view);
        surface.render(view);
	}

    //int2 lastPos;
    bool event(int2 input, Event event, State state) override {
        //int2 delta = pos-lastPos; lastPos=pos;
        vec2 viewPos = (2*vec2(input)/vec2(size.x,-size.y)-vec2(1,-1));
        vec2 pos = (view.inverse() * vec4(viewPos.x,viewPos.y,0,1)).xy();
        if(event==LeftButton && state==Pressed) {
            surface.curve.clear();
            surface.curve << vec2(pos);
        } else if(event==Motion && state==Pressed) {
            surface.curve << vec2(pos);
            //rotation += delta*PI/size;
        } else if(event==WheelDown && state==Pressed) {
            scale *= 17.0/16;
        } else if(event==WheelUp && state==Pressed) {
            scale *= 15.0/16;
        } else if(event==Motion) {
            //surface.input=pos;
            return true; //DEBUG
        } else return false;
        return true;
	}
	/*void View::timerEvent(QTimerEvent*) {
        if(window.cursor.x <= 0) pos -= view[0]/scale/16;
        if(window.cursor.x >= Screen::width-1 ) pos += view[0]/scale/16;
        if(window.cursor.y <= 0) pos -= view[1]/scale/16;
        if(window.cursor.y >= Screen::height-1 ) pos += view[1]/scale/16;
	}*/
} editor;
