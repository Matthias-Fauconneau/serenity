#include "gl.h"

#define MESA_EGL_NO_X11_HEADERS
#include  <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include  <GL/gl.h>
#include <GL/glu.h>

/// Context

#define glCheck ({uint e=glGetError(); if(e) error(str((const char*)gluErrorString(e))); })

GLContext::GLContext(Window& window) {
    window.softwareRendering = false; //disable software rendering
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, 0, 0);
    EGLConfig config; EGLint matchingConfigurationCount;
    {int attributes[] ={EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_DEPTH_SIZE, 24, EGL_NONE};
        eglChooseConfig(display, attributes, &config, 1, &matchingConfigurationCount);
    }
    if(matchingConfigurationCount != 1) error("No matching configuration", matchingConfigurationCount);

    surface = eglCreateWindowSurface(display, config, window.id, 0);
    {int attributes[]={EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context = eglCreateContext(display, config, 0, attributes);}
    eglMakeCurrent(display, surface, surface, context);
}

GLContext::~GLContext() {
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
}

void GLContext::swapBuffers() {
    glCheck;
    eglSwapBuffers(display, surface);
}

void glCullFace(bool enable) { if(enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void glDepthTest(bool enable) { if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void glBlend(bool enable) { if(enable) glEnable(GL_BLEND); else glDisable(GL_BLEND); }

/// Shader

#if GL_PROGRAM_UNIFORM
void GLUniform::operator=(float v) { glProgramUniform1f(program,location,v); }
void GLUniform::operator=(vec2 v) { glProgramUniform2f(program,location,v.x,v.y); }
void GLUniform::operator=(vec4 v) { glProgramUniform4f(program,location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat4 m) { glProgramUniformMatrix4fv(program,location,1,0,m.data); }
#else
void GLUniform::operator=(float v) { glUseProgram(program); glUniform1f(location,v); }
void GLUniform::operator=(vec2 v) { glUseProgram(program); glUniform2f(location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { glUseProgram(program); glUniform3f(location,v.x,v.y,v.z); }
void GLUniform::operator=(vec4 v) { glUseProgram(program); glUniform4f(location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat3 m) { glUseProgram(program); glUniformMatrix3fv(location,1,0,m.data); }
void GLUniform::operator=(mat4 m) { glUseProgram(program); glUniformMatrix4fv(location,1,0,m.data); }
#endif
GLUniform GLShader::operator[](const char* name) {
    int location = uniformLocations.value(name,-1);
    glCheck;
    if(location<0) uniformLocations.insert(name,location=glGetUniformLocation(id,name));
    if(location<0) error("Unknown uniform"_,name);
    return GLUniform(id,location);
}

void GLShader::compile(uint type, const ref<byte>& source) {
    uint shader=glCreateShader(type);
    glShaderSource(shader, 1, &source.data, &(int&)source.size);
    glCompileShader(shader);
    int length;
    glGetShaderiv(shader , GL_INFO_LOG_LENGTH , &length);
    if(length>1) {
        string buffer(length); buffer.setSize(length-1);
        glGetShaderInfoLog(shader, length, 0, buffer.data());
        if(buffer) log(buffer);
    }
    int success=0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if(!success) error(success);
    glAttachShader(id, shader);
}
GLShader::GLShader(const ref<byte> &vertex, const ref<byte> &fragment) {
    id = glCreateProgram();
    compile(GL_VERTEX_SHADER, vertex);
    compile(GL_FRAGMENT_SHADER, fragment);
    glLinkProgram(id);
    int success=0; glGetProgramiv(id, GL_LINK_STATUS, &success); if(!success) error("");
}
void GLShader::bind() { glUseProgram(id); }
uint GLShader::attribLocation(const char* name) {
    int location = attribLocations.value(name,-1);
    if(location<0) attribLocations.insert(name,location=glGetAttribLocation(id,name));
    if(location<0) error("Unknown attribute"_,name);
    return (uint)location;
}
void GLShader::bindSamplers(const char* tex0, const char* tex1) {
  if(tex0) operator[](tex0) = 0;
  if(tex1) operator[](tex1) = 1;
}

/// Buffer

GLBuffer::~GLBuffer() { if(vertexBuffer) glDeleteBuffers(1,&vertexBuffer); if(indexBuffer) glDeleteBuffers(1,&indexBuffer); }
void GLBuffer::allocate(int indexCount, int vertexCount, int vertexSize) {
    this->vertexCount = vertexCount;
    this->vertexSize = vertexSize;
    if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertexCount*vertexSize, 0, GL_STATIC_DRAW );
    this->indexCount = indexCount;
    if(indexCount) {
        if(!indexBuffer) glGenBuffers(1, &indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexCount*sizeof(uint), 0, GL_STATIC_DRAW );
    }
}
uint* GLBuffer::mapIndexBuffer() {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    return (uint*)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
}
void GLBuffer::unmapIndexBuffer() { glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
void* GLBuffer::mapVertexBuffer() {
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    return glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY );
}
void GLBuffer::unmapVertexBuffer() { glUnmapBuffer(GL_ARRAY_BUFFER); glBindBuffer(GL_ARRAY_BUFFER, 0); }

void GLBuffer::upload(const ref<int>& indices) {
    if(!indexBuffer) glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size*sizeof(uint32), indices.data, GL_STATIC_DRAW);
    indexCount = indices.size;
}
void GLBuffer::upload(const ref<byte> &vertices) {
    if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size, vertices.data, GL_STATIC_DRAW);
    vertexCount = vertices.size/vertexSize;
}
void GLBuffer::bindAttribute(GLShader& program, const char* name, int elementSize, uint64 offset) {
    assert(vertexBuffer);
    int location = program.attribLocation(name);
    assert(location>=0,"unused attribute"_,name);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(location, elementSize, GL_FLOAT, 0, vertexSize, (void*)offset);
    glEnableVertexAttribArray(location);
}
void GLBuffer::draw() {
    glCheck;
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    /*if (primitiveType == Point) {
        glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
        glEnable(GL_POINT_SPRITE);
    }*/
    if (indexBuffer) {
        if(primitiveRestart) {
            glEnable(GL_PRIMITIVE_RESTART);
            glPrimitiveRestartIndex(0xFFFFFFFF);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        glDrawElements(primitiveType, indexCount, GL_UNSIGNED_INT, 0);
        if(primitiveRestart) {
            glDisable(GL_PRIMITIVE_RESTART);
        }
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDrawArrays(primitiveType, 0, vertexCount);
    }
}

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord) {
    glCheck;
    shader.bind();
    glBindBuffer(GL_ARRAY_BUFFER,0);
    uint positionIndex = shader.attribLocation("position");
    vec2 positions[] = { vec2(min.x,min.y), vec2(max.x,min.y), vec2(min.x,max.y), vec2(max.x,max.y) };
    glVertexAttribPointer(positionIndex,2,GL_FLOAT,0,0,positions);
    glEnableVertexAttribArray(positionIndex);
    uint texCoordIndex;
    if(texCoord) {
        texCoordIndex = shader.attribLocation("texCoord");
        vec2 texCoords[] = { vec2(0,0), vec2(1,0), vec2(0,1), vec2(1,1) };
        glVertexAttribPointer(texCoordIndex,2,GL_FLOAT,0,0,texCoords);
        glEnableVertexAttribArray(texCoordIndex);
    }
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glDisableVertexAttribArray(positionIndex);
    if(texCoord) glDisableVertexAttribArray(texCoordIndex);
}

/// Texture

GLTexture::GLTexture(const Image& image) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D,0,4,image.width,image.height,0,image.alpha?GL_BGRA:GL_BGR,GL_UNSIGNED_BYTE,image.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
GLTexture::GLTexture(int width, int height, int format) : width(width), height(height) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if(format&Shadow) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL );
    } else if(format&Depth) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,0);
    } else if(format&Gamma) glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8,width,height,0,GL_SRGB,GL_UNSIGNED_BYTE,0);
    else if(format&Float) glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    else if(format&RG16) glTexImage2D(GL_TEXTURE_2D,0,GL_RG16,width,height,0,GL_RG,GL_UNSIGNED_SHORT,0);
    else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    if(format&Bilinear) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_LINEAR_MIPMAP_NEAREST:GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if(format&Anisotropic) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 2.0);
    if(format&Clamp) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}
GLTexture::~GLTexture() { assert(id); glDeleteTextures(1,&id); id=0; }
void GLTexture::bind(uint sampler) const { assert(id); glActiveTexture(GL_TEXTURE0+sampler); glBindTexture(GL_TEXTURE_2D, id); }
void GLTexture::bindSamplers(const GLTexture& tex0) { tex0.bind(0); }

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(GLTexture&& depth, GLTexture&& color):depth(move(depth)),color(move(color)){
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    if(this->depth.id) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,this->depth.id,0);
    if(this->color.id) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,this->color.id,0);
}
GLFrameBuffer::~GLFrameBuffer() { glDeleteFramebuffers(1,&id); }
void GLFrameBuffer::bind(bool clear, vec4 clearColor) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
  if(depth.id) glViewport(0,0,depth.width,depth.height); else glViewport(0,0,color.width,color.height);
  if(clear) {
      glClearColor(clearColor.x,clearColor.y,clearColor.z,clearColor.w);
      glClear( (depth.id?GL_DEPTH_BUFFER_BIT:0) | (color.id?GL_COLOR_BUFFER_BIT:0) );
  }
}
void GLFrameBuffer::bindWindow(int2 position, int2 size) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(position.x,position.y,size.x,size.y);
}
