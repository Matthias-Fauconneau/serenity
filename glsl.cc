#include "string.h"
#include "file.h"
#include <X11/Xlib.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

int main(int argc, const char** argv) {
    Display* x = XOpenDisplay(0);
    Window id = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,1,1,0,0,0);
    XVisualInfo* vis = glXChooseVisual(x,DefaultScreen(x),(int[]){GLX_RGBA,0});
    GLXContext ctx = glXCreateContext(x,vis,0,1);
    glXMakeCurrent(x, id, ctx);

    array<string> args; for(int i=1;i<argc;i++) args << strz(argv[i]);
    if(args.size<2) { log("Usage: glsl binary.gpu shader.glsl tags"_); return 1; }
    assert(exists(args[1]));
    string source = mapFile(args[1]);
    array<string> tags = slice(args,2);

    uint program = glCreateProgram();
    for(int type: {GL_VERTEX_SHADER,GL_FRAGMENT_SHADER}) {
        array<string> tags_ = { (type==GL_VERTEX_SHADER?"vertex"_:"fragment"_) };
        tags_ << tags;
        string global, main;
        const char* s = &source, *e=&source+source.size; //TODO: TextStream
        array<int> scope;
        for(int nest=0;s<e;) { //for each line
            const char* l=s;
            { //[a-z]+ {
                const char* t=s; while(*t==' '||*t=='\t') t++; const char* b=t; while(*t>='a'&&*t<='z') t++;
                const string tag(b,t);
                if(t>b && *t++==' ' && *t++=='{' && *t++=='\n') { //scope
                    bool skip=true;
                    for(const auto& e : tags_) if(tag==e) { skip=false; break; }
                    s=t;
                    if(skip) {
                        for(int nest=1;nest;s++) { assert(*s,"Unmatched {"_); if(*s=='{') nest++; if(*s=='}') nest--; }
                        assert(*s=='\n'); s++;
                    } else { scope<<nest; nest++; } //remember to remove scope end bracket
                    continue;
                }
            }
            bool declaration=false;
            { //(uniform|attribute|varying|in|out )|(float|vec[1234]|mat[234]) [a-zA-Z0-9]+\(
                const char* t=s; while(*t==' '||*t=='\t') t++; const char* b=t; while(*t>='a'&&*t<='z') t++;
                const string qualifier(b,t);
                if(t>b && *t++==' ') {
                    for(const auto& e : {"uniform"_,"attribute"_,"varying"_,"in"_,"out"_}) if(qualifier==e) { declaration=true; break; }
                }
            }
            for(;s<e && *s!='\n';s++) { if(*s=='{') nest++; if(*s=='}') nest--; } s++;
            if(scope.size && nest==scope.last()) { scope.removeLast(); continue; }
            (declaration ? global : main) << string(l,s);
        }
        string glsl = "#version 120\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_;
        uint shader = glCreateShader(type);
        glShaderSource(shader,1,(const char**)& &glsl,&glsl.size);
        glCompileShader(shader);
        glAttachShader(program,shader);
        int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
        if(!status) {
            log(glsl);
            log(args[1]+":1:1: error: Failed to compile"_);
            int l=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&l);
            if(l) { string msg(l); glGetProgramInfoLog(program,l,&msg.size,(char*)&msg); log(msg); }
            return 1;
        }
    }
    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(program);
    int success=0; glGetProgramiv(program, GL_LINK_STATUS, &success);
    assert(success);

    int binaryLength;
    glUseProgram(program);
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    assert(binaryLength);
    array<byte> binary(binaryLength);
    GLenum binaryFormat;
    glGetProgramBinary(program, binaryLength, 0, &binaryFormat, (void*)&binary);
    binary.size = binaryLength;
    assert(!exists(args[0]));
    int fd = createFile(args[0]);
    write(fd,binary);
    close(fd);
    return 0;
}
