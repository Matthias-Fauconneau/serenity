#include "gl.h"
#include "matrix.h"
#include "data.h"
#include "image.h"
#ifdef GLEW
#include <GL/glew.h> // GLEW
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h> // GL
#endif

/// Rasterizer
void glCullFace(bool enable) { if(enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void glDepthTest(bool enable) { if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void glLine(bool enable) { glPolygonMode(GL_FRONT_AND_BACK, enable?GL_LINE:GL_FILL); }

/// Shader
void GLUniform::operator=(int v) { assert(location>=0); glProgramUniform1i(program, location, v); }
void GLUniform::operator=(float v) { assert(location>=0); glProgramUniform1f(program, location,v); }
void GLUniform::operator=(ref<float> v) { assert(location>=0); glProgramUniform1fv(program, location, int(v.size), (float*)v.data); }
void GLUniform::operator=(vec2 v) { assert(location>=0); glProgramUniform2f(program, location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { assert(location>=0); glProgramUniform3f(program, location,v.x,v.y,v.z); }
void GLUniform::operator=(ref<vec4> v) { assert(location>=0); glProgramUniform4fv(program, location, int(v.size), (float*)v.data); }
void GLUniform::operator=(vec4 v) { assert(location>=0); glProgramUniform4f(program, location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat4 m) { assert(location>=0); glProgramUniformMatrix4fv(program, location,1,0,m.data); }

GLShader::GLShader(string source, ref<string> stages) {
 id = glCreateProgram();
 array<string> knownTags;
 array<String> glslSource;
 for(uint type: (uint[]){GL_VERTEX_SHADER,GL_FRAGMENT_SHADER}) {
  array<char> global, main;
  for(size_t i: range(stages.size)) {
   buffer<string> tags = split(stages[i], " ") + (type==GL_VERTEX_SHADER?"vertex"_:"fragment"_);
   array<char> stageGlobal, stageMain;
   TextData s (source);
   array<uint> scope;
   for(uint nest=0;s;) { // for each line (FIXME: line independent)
    size_t start = s.index;
    s.whileAny(" \t"_);
    if(s.match("uniform") || s.match("struct") || s.match("buffer") || s.match("layout")) {
        s.whileNo("{;");
        if(s.match('{')) s.until('}');
        s.skip(';');
        stageGlobal.append( s.slice(start, s.index-start) );
        continue;
    }
    string identifier = s.identifier("_!"_);
    s.whileAny(" \t"_);
    if(identifier && s.match("{"_)) { // Scope: [a-z]+ {
     bool condition=true;
     if(startsWith(identifier,"!"_)) { condition=false; identifier=identifier.slice(1); }
     if(tags.contains(identifier) == condition) {
      knownTags.add( identifier );
      scope.append( nest ); nest++; // Remember nesting level to remove matching scope closing bracket
     } else { // Skip scope
      for(uint nest=1; nest;) {
       if(!s) error(source.slice(start), "Unmatched {"_);
       if(s.match('{')) nest++;
       else if(s.match('}')) nest--;
       else s.advance(1);
      }
     }
     continue;
    }
    bool function = false;
    static array<string> types = split("void float vec2 vec3 vec4"_," ");
    static array<string> qualifiers = split("struct layout const buffer attribute varying in out"_," "); // uniform
    if(types.contains(identifier) && s.identifier("_"_) && s.match('(')) {
     function = true;
     s.until('{');
     for(uint n=nest+1;s && n>nest;) {
      if(s.match('{')) n++;
      else if(s.match('}')) n--;
      else s.advance(1);
     }
    }
    while(s && !s.match('\n')) {
     if(s.match('{')) nest++;
     else if(s.match('}')) { nest--;
      if(scope && nest==scope.last()) { // Remove matching scope closing bracket
       scope.pop(); stageMain.append( s.slice(start, s.index-1-start) ); start=s.index;
      }
     }
     else s.advance(1);
    }
    string line = s.slice(start, s.index-start);
    if(trim(line) && !startsWith(trim(line), "//"_))
        ((function || qualifiers.contains(identifier) || startsWith(line,"#"_)) ? stageGlobal : stageMain).append( line );
   }
   global.append( replace(stageGlobal,"$"_,str(i-1)) );
   main.append( replace(stageMain,"$"_,str(i-1)) );
  }
  glslSource.append( "#version 430\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_ );
  uint shader = glCreateShader(type);
  glShaderSource(shader, 1, &glslSource.last().data, (int*)&glslSource.last().size);
  glCompileShader(shader);

  int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
  int length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
  if(!status || length>1) {
   ::buffer<byte> buffer(length);
   glGetShaderInfoLog(shader, length, 0, buffer.begin());
   String s = str(glslSource.last(), buffer.slice(0,buffer.size-1));
   if(!status) error(s); else log(s);
  }
  glAttachShader(id, shader);
 }
 for(string tags: stages) for(string& tag: split(tags," ")) if(!knownTags.contains(tag)) error("Unknown tag",tag, tags, stages);
 glLinkProgram(id);

 int status=0; glGetProgramiv(id, GL_LINK_STATUS, &status);
 int length=0; glGetProgramiv(id , GL_INFO_LOG_LENGTH , &length);
 if(!status || length>1) {
  ::buffer<byte> buffer(length);
  glGetProgramInfoLog(id, length, 0, buffer.begin());
  error(glslSource, stages, "Program failed\n", buffer.slice(0,buffer.size-1));
 }
}
void GLShader::bind() const { glUseProgram(id); }
void GLShader::bindFragments(ref<string> fragments) const {
 for(uint i: range(fragments.size)) glBindFragDataLocation(id, i, strz(fragments[i]));
}
uint GLShader::attribLocation(string name) const {
 int location = -1; //attribLocations.value(name, -1);
 if(location<0) {
  location=glGetAttribLocation(id, strz(name));
  //if(location>=0) attribLocations.insert(copyRef(name), location);
 }
 if(location<0) error("Unknown attribute '"_+str(name)+"'"_);
 return (uint)location;
}
GLUniform GLShader::operator[](string name) const {
 int location = -1;//uniformLocations.value(name, -1);
 if(location == -1) {
  location = glGetUniformLocation(id, strz(name));
  if(location == -1) error("Unknown uniform"_, name);
  //uniformLocations.insert(copyRef(name), location);
 }
 return GLUniform(id, location);
}
void GLShader::bind(string name, const GLBuffer& ssbo, uint binding) const {
    const uint index = glGetProgramResourceIndex(id, GL_SHADER_STORAGE_BLOCK, strz(name));
    assert_(GLEW_ARB_shader_storage_buffer_object);
    assert_(index!=GL_INVALID_INDEX, name);
    glShaderStorageBlockBinding(id, index, binding);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, ssbo.id);
}
/*void GLShader::bind(string name, const GLUniformBuffer& ubo, uint binding) {
 glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo.id);
 const uint index = glGetUniformBlockIndex(id, strz(name));
 assert_(index != uint(-1));
 glUniformBlockBinding(id, index, binding);
}*/

/// Buffer

GLBuffer::~GLBuffer() { if(id) glDeleteBuffers(1, &id); }

void GLBuffer::upload(uint elementSize, ref<byte> data) {
    this->elementSize = elementSize;
    this->elementCount = data.size/elementSize;
    if(!id) {
        glCreateBuffers(1, &id);
        glNamedBufferStorage(id, data.size, data.data, /*GL_DYNAMIC_STORAGE_BIT*/0);
    } else {
        glNamedBufferSubData(id, 0, data.size, data.data);
    }
}


mref<byte> GLBuffer::map() {
    assert_(id);
    return mref<byte>(reinterpret_cast<byte*>(glMapNamedBuffer(id, GL_WRITE_ONLY)), elementCount*elementSize);
    //return mref<byte>(reinterpret_cast<byte*>(glMapNamedBufferRange(id, 0, elementCount*elementSize, GL_WRITE_ONLY)), elementCount*elementSize);
}

mref<byte> GLBuffer::map(uint elementSize, size_t elementCount) {
    this->elementSize = elementSize;
    this->elementCount = elementCount;
    assert_(int(elementCount) > 0 && int(elementSize) > 0);
    assert_(!id);
    glCreateBuffers(1, &id);
    glNamedBufferStorage(id, elementCount*elementSize, 0, GL_MAP_WRITE_BIT /*| GL_MAP_PERSISTENT_BIT*/);
    return map();
}

void GLBuffer::unmap() {
    assert_( glUnmapNamedBuffer(id) );
}

/// Vertex array

GLVertexArray::GLVertexArray() {	glGenVertexArrays(1, &id); }
GLVertexArray::~GLVertexArray() { if(id) glDeleteVertexArrays(1, &id); }
void GLVertexArray::bind() const { glBindVertexArray(id); }
void GLVertexArray::bindAttribute(int index, int elementSize, AttributeType type, const GLBuffer& buffer, uint64 offset, uint stride) const {
 bind();
 glBindBuffer(GL_ARRAY_BUFFER, buffer.id);
 if(type==UInt) glVertexAttribIPointer(index, elementSize, type, stride, (void*)offset);
 else glVertexAttribPointer(index, elementSize, type, false, stride, (void*)offset);
 glEnableVertexAttribArray(index);
 glBindVertexArray(0);
 glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void GLVertexArray::draw(PrimitiveType primitiveType, uint vertexCount) const {
 bind();
 glDrawArrays(primitiveType, 0, vertexCount);
}

void GLIndexBuffer::draw(size_t start, size_t end) {
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
 GLenum type = elementSize==2 ? GL_UNSIGNED_SHORT : elementSize==4 ? GL_UNSIGNED_INT : ({error(elementSize); 0;});
 glDrawElements(primitiveType, (end-start)?: elementCount, type, (void*)(start*elementSize));
 glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/// Texture
GLTexture::GLTexture(uint3 size, uint format) {
    glCreateTextures(/*size.z==1?GL_TEXTURE_2D:*/GL_TEXTURE_2D_ARRAY, 1, &id);
    assert_(format == RGB32F);
    assert_(size.x && size.y && size.z, size);
    glTextureStorage3D(id, 1, GL_RGB32F, size.x, size.y, size.z);
    glTextureParameterf(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if 1
    glTextureParameterf(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameterf(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#else
    glTextureParameterf(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameterf(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif
}
uint64 GLTexture::handle() const {
#if GLEW
    if(!GLEW_ARB_bindless_texture) return 0;
#endif
    uint64 handle = glGetTextureHandleARB(id);
    glMakeTextureHandleResidentARB(handle);
    return handle;
}
/*void GLTexture::upload(uint2 size, ref<rgb3f> data) const {
    glTextureSubImage2D(id, 0, 0,0, size.x,size.y, GL_RGB, GL_FLOAT, data.begin());
}*/
void GLTexture::upload(uint3 size, ref<rgb3f> data) const {
    glTextureSubImage3D(id, 0, 0,0,0, size.x,size.y,size.z, GL_RGB, GL_FLOAT, data.begin());
}
void GLTexture::upload(uint3 size, const GLBuffer& buffer, size_t data, size_t unused bufferSize) const {
    assert_(buffer.id);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer.id);
    assert_(data+size.z*size.y*size.x*sizeof(rgb3f) <= buffer.elementCount*buffer.elementSize);
    if(size.z>1) glTextureSubImage3D(id, 0, 0,0,0, size.x,size.y,size.z, GL_RGB, GL_FLOAT, reinterpret_cast<void*>(data*sizeof(rgb3f)));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}
GLTexture::~GLTexture() { if(id) glDeleteTextures(1, &id); id=0; }

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(uint2 size, uint sampleCount) : size(size) {
    if(sampleCount==0) {
        glGetIntegerv(GL_MAX_SAMPLES, &(int&)sampleCount);
        int modeCount; glGetIntegerv(GL_MAX_MULTISAMPLE_COVERAGE_MODES_NV, &modeCount);
        int2 modes [modeCount];
        glGetIntegerv(GL_MULTISAMPLE_COVERAGE_MODES_NV, (int*)modes);
        log(ref<int2>(modes, modeCount));
    }

    glGenFramebuffers(1, &id);
    glBindFramebuffer(GL_FRAMEBUFFER, id);

    glGenRenderbuffers(1, &depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    if(sampleCount > 1) {
#ifdef GLEW
        if(GLEW_NV_framebuffer_multisample_coverage)
            glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, sampleCount, sampleCount, GL_DEPTH_COMPONENT24, size.x, size.y);
        else
#endif
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, GL_DEPTH_COMPONENT24, size.x, size.y);
    } else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    glGenRenderbuffers(1, &colorBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer);
    if(sampleCount > 1) glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, sampleCount, sampleCount, GL_RGBA32F, size.x, size.y);
    else glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA32F, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer);

    assert_(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}
GLFrameBuffer::~GLFrameBuffer() {
 if(depthBuffer) glDeleteRenderbuffers(1, &depthBuffer);
 if(colorBuffer) glDeleteRenderbuffers(1, &colorBuffer);
 if(id) glDeleteFramebuffers(1,&id);
}
void GLFrameBuffer::bind(uint clearFlags, rgba4f color) const {
 glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
 glViewport(0,0,width,height);
 if(clearFlags) {
  if(clearFlags&ClearColor) glClearColor(color.r,color.g,color.b,color.a);
  assert((clearFlags&(~(ClearDepth|ClearColor)))==0, clearFlags);
  glClear(clearFlags);
 }
}
GLFrameBuffer window(uint2 size) {
    GLFrameBuffer window;
    window.size = size;
    return window;
}

void GLFrameBuffer::blit(uint target, uint2 targetSize, uint2 offset) const {
 glBindFramebuffer(GL_READ_FRAMEBUFFER, id);
 glReadBuffer(GL_COLOR_ATTACHMENT0);
 glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
 targetSize = targetSize?:size;
 glBlitFramebuffer(0,0, size.x,size.y, offset.x,offset.y, offset.x+targetSize.x,offset.x+targetSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void GLFrameBuffer::readback(const Image& target) const {
    assert_(target.size == size);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, id);
    glReadPixels(0,0, target.size.x, target.size.y, GL_BGRA, GL_UNSIGNED_BYTE, (void*)target.data);
    flip(target);
}

void GLFrameBuffer::readback(const Image4f& target) const {
    assert_(target.size == size);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, id);
    glReadPixels(0, 0, target.size.x, target.size.y, GL_BGRA, GL_FLOAT, (void*)target.data);
    flip(target);
}

#if 0
struct GLTime {
    handle<uint> id = 0;
    GLTime() { glGenQueries(1, &id); }
    void queryCounter() { glQueryCounter(id, GL_TIMESTAMP); }
    uint64 nanoseconds() { GLuint64 time; glGetQueryObjectui64v(id, GL_QUERY_RESULT, &time); return time; }
};
struct GLTimer {
    GLTime startTime, stopTime;
    void start() { startTime.queryCounter(); }
    void stop() { stopTime.queryCounter(); }
    uint64 nanoseconds() { return stopTime.nanoseconds()-startTime.nanoseconds(); }
    float milliseconds() { return nanoseconds() / 1000000.f; }
    float seconds() { return nanoseconds() / 1000000000.f; }
};
#endif
