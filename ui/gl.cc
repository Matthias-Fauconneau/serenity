#include "gl.h"
#include "thread.h"
#include "data.h"

#define Time XTime
#define Cursor XCursor
#define Depth XXDepth
#define Window XWindow
#define Screen XScreen
#define XEvent XXEvent
#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h> //X11
#include <GL/gl.h> //GL
#undef Time
#undef Cursor
#undef Depth
#undef Window
#undef Screen
#undef XEvent
#undef None

static Display* display;
static GLXContext glContext;

void __attribute((constructor(1002))) setup_gl() {
    display = XOpenDisplay(strz(getenv("COMPUTE"_,":0"_))); assert(display);
    //display = XOpenDisplay(":1"); assert(display);
    const int fbAttribs[] = {0};
    int fbCount=0; GLXFBConfig* fbConfigs = glXChooseFBConfig(display, 0, fbAttribs, &fbCount); assert(fbConfigs && fbCount);
    glContext = ((PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB"))
            (display, fbConfigs[0], 0, true, (int[]){ GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 0, 0});
    assert(glContext);
    GLXPbuffer id = glXCreatePbuffer( display, fbConfigs[0], (int[]){GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, 0} );
    XFree(fbConfigs);
    glXMakeContextCurrent(display, id, id, glContext);
}

/// Shader

void GLUniform::operator=(int v) { assert_(location>=0); glUseProgram(program); glUniform1i(location,v); }
void GLUniform::operator=(float v) { assert_(location>=0); glUseProgram(program); glUniform1f(location,v); }
void GLUniform::operator=(vec2 v) { assert_(location>=0); glUseProgram(program); glUniform2f(location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { assert_(location>=0); glUseProgram(program); glUniform3f(location,v.x,v.y,v.z); }
void GLUniform::operator=(vec4 v) { assert_(location>=0); glUseProgram(program); glUniform4f(location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat3 m) { assert_(location>=0); glUseProgram(program); glUniformMatrix3fv(location,1,0,m.data); }

GLUniform GLShader::operator[](const string& name) {
    int location = uniformLocations.value(String(name),-1);
    if(location<0) {
        location=glGetUniformLocation(id, strz(name));
        if(location<0) error("Unknown uniform"_, name, source);
        if(location<0) return GLUniform(id,location);
        uniformLocations.insert(String(name),location);
    }
    return GLUniform(id,location);
}

GLShader::GLShader(const string& source, const ref<string>& stages) {
    id = glCreateProgram();
    array<string> knownTags;
    for(uint type: (uint[]){GL_VERTEX_SHADER,GL_FRAGMENT_SHADER}) {
        String global, main;
        for(uint i: range(stages.size)) { string tags = stages[i];
            String stageGlobal, stageMain;
            array<string> tags_;
            tags_ << split(tags,' ') << (type==GL_VERTEX_SHADER?"vertex"_:"fragment"_);
            TextData s (source);
            array<uint> scope;
            for(uint nest=0;s;) { //for each line (FIXME: line independent)
                uint start = s.index;
                s.whileAny(" \t"_);
                string identifier = s.identifier("_!"_);
                s.whileAny(" \t"_);
                if(identifier && identifier!="else"_ && s.match("{"_)) { //scope: "[a-z]+ {"
                    bool condition=true;
                    if(startsWith(identifier,"!"_)) condition=false, identifier=identifier.slice(1);
                    if(tags_.contains(identifier)==condition) {
                        knownTags += identifier;
                        scope<<nest; nest++; // Remember nesting level to remove matching scope closing bracket
                    } else { // Skip scope
                        for(uint nest=1; nest;) {
                            if(!s) error(source.slice(start), "Unmatched {"_);
                            if(s.match('{')) nest++;
                            else if(s.match('}')) nest--;
                            else s.advance(1);
                        }
                        //if(!s.match('\n')) error("Expecting newline after scope");
                    }
                    continue;
                    //start = s.index;
                }
                bool function = false;
                static array<string> types = split("void float vec2 vec3 vec4"_);
                static array<string> qualifiers = split("struct const uniform attribute varying in out"_);
                if(types.contains(identifier) && s.identifier("_"_) && s.match('(')) {
                    function = true;
                    s.until('{');
                    for(uint n=nest+1;s && n>nest;) {
                        if(s.match('{')) n++;
                        else if(s.match('}')) n--;
                        else s.advance(1);
                    }
                }
                if(identifier=="uniform"_ && s.match("sampler2D "_)) s.whileAny(" \t"_), sampler2D << String(s.identifier("_"_));
                while(s && !s.match('\n')) {
                    if(s.match('{')) nest++;
                    else if(s.match('}')) { nest--;
                        if(scope && nest==scope.last()) { //Remove matching scope closing bracket
                            scope.pop(); stageMain<<s.slice(start, s.index-1-start); start=s.index;
                        }
                    }
                    else s.advance(1);
                }
                string line = s.slice(start, s.index-start);
                if(trim(line)) ((function || qualifiers.contains(identifier) || startsWith(line,"#"_)) ? stageGlobal : stageMain) << line;
            }
            global << replace(stageGlobal,"$"_,str(i-1));
            main << replace(stageMain,"$"_,str(i-1));
        }
        String glsl = "#version 130\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_;
        this->source << copy(glsl);
        uint shader = glCreateShader(type);
        const char* data = glsl.data; int size = glsl.size;
        glShaderSource(shader, 1, &data,&size);
        glCompileShader(shader);

        int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
        int length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        if(!status || length>1) {
            char buffer[length];
            glGetShaderInfoLog(shader, length, 0, buffer);
            error(string(buffer,length-1));
        }
        glAttachShader(id, shader);
    }
    glLinkProgram(id);

    int status=0; glGetProgramiv(id, GL_LINK_STATUS, &status);
    int length=0; glGetProgramiv(id , GL_INFO_LOG_LENGTH , &length);
    if(!status || length>1) {
        ::buffer<byte> buffer(length);
        glGetProgramInfoLog(id, length, 0, buffer.begin());
        error(this->source, stages, "Program failed\n", buffer.slice(0,buffer.size-1));
    }
    for(string tags: stages) for(string& tag: split(tags,' ')) if(!knownTags.contains(tag)) error("Unknown tag",tag, tags, stages);
}
void GLShader::bind() { glUseProgram(id); }
void GLShader::bindSamplers(const ref<string> &textures) { for(int i: range(textures.size)) { GLUniform tex = operator[](textures[i]); if(tex) tex = i; } }
//void GLShader::bindFragments(const ref<string> &fragments) { for(uint i: range(fragments.size)) glBindFragDataLocation(id, i, fragments[i]); }
uint GLShader::attribLocation(const string& name) {
    int location = attribLocations.value(String(name),-1);
    if(location<0) {
        location=glGetAttribLocation(id,strz(name));
        if(location>=0) attribLocations.insert(String(name), location);
    }
    //if(location<0) error("Unknown attribute"_,name);
    return (uint)location;
}

/// Uniform buffer

GLUniformBuffer::~GLUniformBuffer() { if(id) glDeleteBuffers(1,&id); }
void GLUniformBuffer::upload(const ref<byte>& data) {
    assert(data);
    if(!id) glGenBuffers(1, &id);
    glBindBuffer(GL_UNIFORM_BUFFER, id);
    glBufferData(GL_UNIFORM_BUFFER, data.size, data.data, GL_STATIC_DRAW);
    size=data.size;
}
void GLUniformBuffer::bind(GLShader& program, const ref<byte>& name) const {
    assert(id);
    int location = glGetUniformBlockIndex(program.id, strz(name));
    assert(location>=0, name);
    int size=0; glGetActiveUniformBlockiv(program.id, location, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
    assert(size == this->size, size, this->size);
    glUniformBlockBinding(program.id, location, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, id);
}

/// Vertex buffer

GLVertexBuffer::~GLVertexBuffer() { if(id) glDeleteBuffers(1,&id); }
void GLVertexBuffer::allocate(int vertexCount, int vertexSize) {
    this->vertexCount = vertexCount;
    this->vertexSize = vertexSize;
    if(!id) glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, vertexCount*vertexSize, 0, GL_STATIC_DRAW);
}
void* GLVertexBuffer::mapVertexBuffer() {
    glBindBuffer(GL_ARRAY_BUFFER, id);
    return glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY );
}
void GLVertexBuffer::unmapVertexBuffer() { glUnmapBuffer(GL_ARRAY_BUFFER); }
void GLVertexBuffer::upload(const ref<byte>& vertices) {
    if(!id) glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, vertices.size, vertices.data, GL_STATIC_DRAW);
    vertexCount = vertices.size/vertexSize;
}
void GLVertexBuffer::bindAttribute(GLShader& program, const string& name, int elementSize, uint64 offset) const {
    assert(id!=0); assert(elementSize<=4);
    int index = program.attribLocation(name);
    assert(index>=0); //if(index<0) return;
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glVertexAttribPointer(index, elementSize, GL_FLOAT, 0, vertexSize, (void*)offset);
    //assert(!glGetError()); //First call to glVertexAttribPointer logs an user error to MESA_DEBUG for some reason :/
    glEnableVertexAttribArray(index);
}
void GLVertexBuffer::draw(PrimitiveType primitiveType) const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    if(primitiveType==Lines) {
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glEnable(GL_LINE_SMOOTH);
        glLineWidth(2);
    }
    glDrawArrays(primitiveType, 0, vertexCount);
}

/// Texture
GLTexture::GLTexture(int2 size) : size(size,0) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size.x, size.y, 0, GL_RED, GL_FLOAT, 0);
}
GLTexture::GLTexture(const VolumeF& volume) : size(volume.sampleCount) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_3D, id);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, size.x, size.y, size.z, 0, GL_RED, GL_FLOAT, volume.data.data);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLTexture::~GLTexture() { if(id) glDeleteTextures(1,&id); id=0; }
void GLTexture::bind(uint sampler) const {
    assert(id);
    glActiveTexture(GL_TEXTURE0+sampler);
    glBindTexture(size.z?GL_TEXTURE_3D:GL_TEXTURE_2D, id);
}

void GLTexture::read(const ImageF& target) const {
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexImage(	GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, (float*)target.data.data);
}

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(GLTexture&& texture) : texture(move(texture)) {
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->texture.id, 0);
    assert_(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}
GLFrameBuffer::~GLFrameBuffer() {
    if(id) glDeleteFramebuffers(1,&id);
}
void GLFrameBuffer::bind(uint clearFlags, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
  glViewport(0,0,size().x,size().y);
  if(clearFlags) {
      if(clearFlags&ClearColor) glClearColor(color.x,color.y,color.z,color.w);
      assert((clearFlags&(~(ClearDepth|ClearColor)))==0, clearFlags);
      glClear(clearFlags);
  }
}
