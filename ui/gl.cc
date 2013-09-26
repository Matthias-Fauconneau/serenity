#include "gl.h"
#include "data.h"

#undef packed
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h> //GL

/// Context

void glCullFace(bool enable) { if(enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void glDepthTest(bool enable) { if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void glAlphaTest(bool enable) { if(enable) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST); }
void glBlendAdd() { glBlendFunc(GL_ONE,GL_ONE); glEnable(GL_BLEND); }
void glBlendAlpha() { glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE); glEnable(GL_BLEND); }
void glBlendNone() { glDisable(GL_BLEND); }

/// Shader

void GLUniform::operator=(int v) { assert(location>=0); glUseProgram(program); glUniform1i(location,v); }
void GLUniform::operator=(float v) { assert(location>=0); glUseProgram(program); glUniform1f(location,v); }
void GLUniform::operator=(vec2 v) { assert(location>=0); glUseProgram(program); glUniform2f(location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { assert(location>=0); glUseProgram(program); glUniform3f(location,v.x,v.y,v.z); }
void GLUniform::operator=(vec4 v) { assert(location>=0); glUseProgram(program); glUniform4f(location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat3 m) { assert(location>=0); glUseProgram(program); glUniformMatrix3fv(location,1,0,m.data); }
void GLUniform::operator=(mat4 m) { assert(location>=0); glUseProgram(program); glUniformMatrix4fv(location,1,0,m.data); }

GLUniform GLShader::operator[](const string& name) {
    int location = uniformLocations.value(String(name),-1);
    if(location<0) {
        location=glGetUniformLocation(id,strz(name));
        if(location<0) return GLUniform(id,location); //error("Unknown uniform"_,name);
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
            for(uint nest=0;s;) { //for each line
                static array<string> qualifiers = split("struct const uniform attribute varying in out"_);
                static array<string> types = split("void float vec2 vec3 vec4"_);
                uint lineStart = s.index;
                s.whileAny(" \t"_);
                string identifier = s.identifier("_"_);
                s.whileAny(" \t"_);
                if(identifier && s.match("{\n"_)) { //scope: "[a-z]+ {"
                    if(tags_.contains(identifier)) {
                        knownTags += identifier;
                        scope<<nest; nest++; // Remember nesting level to remove matching scope closing bracket
                    } else { // Skip scope
                        for(uint nest=1; nest;) {
                            if(!s) error(source, "Unmatched {"_);
                            if(s.match('{')) nest++;
                            else if(s.match('}')) nest--;
                            else s.advance(1);
                        }
                        if(!s.match('\n')) error("Expecting newline after scope");
                    }
                    continue;
                }
                bool function = false;
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
                    else if(s.match('}')) nest--;
                    else s.advance(1);
                }
                if(scope && nest==scope.last()) { scope.pop(); continue; } // Remove matching scope closing bracket
                string line = s.slice(lineStart, s.index-lineStart);
                if(trim(line)) ((function || qualifiers.contains(identifier) || startsWith(line,"#"_)) ? stageGlobal : stageMain) << line;
            }
            global << replace(stageGlobal,"$"_,str(i-1));
            main << replace(stageMain,"$"_,str(i-1));
        }
        String glsl = "#version 130\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_;
        this->source = copy(glsl);
        uint shader = glCreateShader(type);
        const char* data = glsl.data; int size = glsl.size;
        glShaderSource(shader, 1, &data,&size);
        glCompileShader(shader);

        int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
        int length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        if(!status || length>1) {
            ::buffer<byte> buffer(length-1);
            glGetShaderInfoLog(shader, length-1, 0, buffer.begin());
            error(glsl,"Shader failed\n",buffer);
        }
        glAttachShader(id, shader);
    }
    glLinkProgram(id);

    int status=0; glGetProgramiv(id, GL_LINK_STATUS, &status);
    int length=0; glGetProgramiv(id , GL_INFO_LOG_LENGTH , &length);
    if(!status || length>1) {
        ::buffer<byte> buffer(length-1);
        glGetProgramInfoLog(id, length-1, 0, buffer.begin());
        error(stages, "Program failed\n", buffer);
    }
    for(string tags: stages) for(string& tag: split(tags,' ')) if(!knownTags.contains(tag)) error("Unknown tag",tag, tags, stages);
}
void GLShader::bind() { glUseProgram(id); }
void GLShader::bindSamplers(const ref<string> &textures) { for(int i: range(textures.size)) { GLUniform tex = operator[](textures[i]); if(tex) tex = i; } }
void GLShader::bindFragments(const ref<string> &fragments) { for(uint i: range(fragments.size)) glBindFragDataLocation(id, i, fragments[i]); }
uint GLShader::attribLocation(const string& name) {
    int location = attribLocations.value(String(name),-1);
    if(location<0) {
        location=glGetAttribLocation(id,strz(name));
        if(location>=0) attribLocations.insert(String(name), location);
    }
    //if(location<0) error("Unknown attribute"_,name);
    return (uint)location;
}

/// Vertex buffer

GLVertexBuffer::~GLVertexBuffer() { if(vertexBuffer) glDeleteBuffers(1,&vertexBuffer); }
void GLVertexBuffer::allocate(int vertexCount, int vertexSize) {
    this->vertexCount = vertexCount;
    this->vertexSize = vertexSize;
    if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertexCount*vertexSize, 0, GL_STATIC_DRAW);
}
void* GLVertexBuffer::mapVertexBuffer() {
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    return glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY );
}
void GLVertexBuffer::unmapVertexBuffer() { glUnmapBuffer(GL_ARRAY_BUFFER); glBindBuffer(GL_ARRAY_BUFFER, 0); }
void GLVertexBuffer::upload(const ref<byte>& vertices) {
    if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size, vertices.data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertexCount = vertices.size/vertexSize;
}
void GLVertexBuffer::bindAttribute(GLShader& program, const string& name, int elementSize, uint64 offset) const {
    assert(vertexBuffer); assert(elementSize<=4);
    int index = program.attribLocation(name);
    //assert(index>=0,"Unknown attribute"_,name);
    if(index<0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, elementSize, GL_FLOAT, 0, vertexSize, (void*)(offset));
}
void GLVertexBuffer::draw(PrimitiveType primitiveType) const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawArrays(primitiveType, 0, vertexCount);
}
/// Index buffer

GLIndexBuffer::~GLIndexBuffer() { if(indexBuffer) glDeleteBuffers(1,&indexBuffer); }
void GLIndexBuffer::allocate(int indexCount) {
    this->indexCount = indexCount;
    if(indexCount) {
        if(!indexBuffer) glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount*sizeof(uint), 0, GL_STATIC_DRAW );
    }
}
uint* GLIndexBuffer::mapIndexBuffer() {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    return (uint*)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
}
void GLIndexBuffer::unmapIndexBuffer() { glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
void GLIndexBuffer::upload(const ref<uint16>& indices) {
    if(!indexBuffer) glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size*sizeof(uint16), indices.data, GL_STATIC_DRAW);
    indexCount = indices.size;
    indexSize = GL_UNSIGNED_SHORT;
}
void GLIndexBuffer::upload(const ref<uint>& indices) {
    if(!indexBuffer) glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size*sizeof(uint32), indices.data, GL_STATIC_DRAW);
    indexCount = indices.size;
    indexSize = GL_UNSIGNED_INT;
}
void GLIndexBuffer::draw() const {
    assert(indexBuffer);
    /*if (primitiveType == Point) {
        glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
        glEnable(GL_POINT_SPRITE);
    }*/
    if(primitiveType == Line) {
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_BLEND);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        glEnable(GL_LINE_SMOOTH);
        glLineWidth(2);
    }
    if(primitiveRestart) {
        glEnableClientState(GL_PRIMITIVE_RESTART_NV);
        glPrimitiveRestartIndex(indexSize==GL_UNSIGNED_SHORT?0xFFFF:0xFFFFFFFF);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glDrawElements(primitiveType, indexCount, indexSize, 0);
    if(primitiveRestart) glDisableClientState(GL_PRIMITIVE_RESTART_NV);
}

void glDrawRectangle(GLShader& shader, vec2 min, vec2 max, bool texCoord) {
    shader.bind();
    glBindBuffer(GL_ARRAY_BUFFER,0);
    uint positionIndex = shader.attribLocation("position");
    vec2 positions[] = { vec2(min.x,min.y), vec2(max.x,min.y), vec2(min.x,max.y), vec2(max.x,max.y) };
    glVertexAttribPointer(positionIndex,2,GL_FLOAT,0,0,positions);
    glEnableVertexAttribArray(positionIndex);
    uint texCoordIndex;
    if(texCoord) {
        texCoordIndex = shader.attribLocation("texCoord");
        vec2 texCoords[] = { vec2(0,1), vec2(1,1), vec2(0,0), vec2(1,0) }; //flip Y
        glVertexAttribPointer(texCoordIndex,2,GL_FLOAT,0,0,texCoords);
        glEnableVertexAttribArray(texCoordIndex);
    }
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glDisableVertexAttribArray(positionIndex);
    if(texCoord) glDisableVertexAttribArray(texCoordIndex);
}

vec2 viewportSize;
vec2 project(vec2 p) { return vec2(2*p.x/viewportSize.x-1,1-2*p.y/viewportSize.y); }
void glDrawRectangle(GLShader& shader, Rect rect, bool texCoord) {
    glDrawRectangle(shader, project(vec2(rect.min.x,rect.max.y)), project(vec2(rect.max.x,rect.min.y)), texCoord);
}

void glDrawLine(GLShader& shader, vec2 p1, vec2 p2) {
    shader.bind();
    glBindBuffer(GL_ARRAY_BUFFER,0);
    uint positionIndex = shader.attribLocation("position");
    vec2 positions[] = { project(p1+vec2(0.5)), project(p2+vec2(0.5)) };
    glVertexAttribPointer(positionIndex,2,GL_FLOAT,0,0,positions);
    glEnableVertexAttribArray(positionIndex);
    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_LINE_SMOOTH);
    glLineWidth(2);
    glDrawArrays(GL_LINES,0,2);
    glDisableVertexAttribArray(positionIndex);
}

/// Texture
GLTexture::GLTexture(int width, int height, uint format, const void* data) : width(width), height(height), format(format) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if((format&3)==sRGB8)
        glTexImage2D(GL_TEXTURE_2D, 0, /*GL_SRGB8*/GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
    if((format&3)==sRGBA)
        glTexImage2D(GL_TEXTURE_2D, 0, /*GL_SRGB8_ALPHA8*/GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
    if((format&3)==Depth24)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, data);
    if((format&3)==RGB16F)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
    if(format&Shadow) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    }
    if(format&Bilinear) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if(format&Anisotropic) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0);
    }
    if(format&Clamp) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if(format&Mipmap) glGenerateMipmap(GL_TEXTURE_2D);
}
GLTexture::GLTexture(const Image& image, uint format)
    : GLTexture(image.width, image.height, (image.alpha?sRGBA:sRGB8)|format, image.data) {
    assert(width==image.stride);
}
GLTexture::~GLTexture() { if(id) glDeleteTextures(1,&id); id=0; }
void GLTexture::bind(uint sampler) const { assert(id); glActiveTexture(GL_TEXTURE0+sampler); glBindTexture(GL_TEXTURE_2D, id); }

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(GLTexture&& depth):width(depth.width),height(depth.height),depthTexture(move(depth)) {
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) error("Incomplete framebuffer");
}
GLFrameBuffer::GLFrameBuffer(uint width, uint height, uint format, int sampleCount):width(width),height(height){
    if(sampleCount==-1) glGetIntegerv(GL_MAX_SAMPLES,&sampleCount);

    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);

    glGenRenderbuffers(1, &depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    glGenRenderbuffers(1, &colorBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, format==RGB16F?GL_RGB16F:GL_SRGB8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) error("");
}
GLFrameBuffer::~GLFrameBuffer() {
    if(depthBuffer) glDeleteRenderbuffers(1, &depthBuffer);
    if(colorBuffer) glDeleteRenderbuffers(1, &colorBuffer);
    if(id) glDeleteFramebuffers(1,&id);
}
void GLFrameBuffer::bind(uint clearFlags, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
  glViewport(0,0,width,height);
  viewportSize = vec2(width,height);
  if(clearFlags) {
      if(clearFlags&ClearColor) glClearColor(color.x,color.y,color.z,color.w);
      assert((clearFlags&(~(ClearDepth|ClearColor)))==0, clearFlags);
      glClear(clearFlags);
  }
}
void GLFrameBuffer::bindWindow(int2 position, int2 size, uint clearFlags, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(position.x,position.y,size.x,size.y);
  viewportSize = vec2(size);
  if(clearFlags&ClearColor) glClearColor(color.x,color.y,color.z,color.w);
  glClear(clearFlags);
}
void GLFrameBuffer::blit(uint target) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
    glBlitFramebuffer(0,0,width,height,0,0,width,height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
void GLFrameBuffer::blit(GLTexture& color) {
    assert(color.width==width, color.height==height);
    uint target;
    glGenFramebuffers(1,&target);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color.id,0);
    blit(target);
    glDeleteFramebuffers(1,&target);
}
