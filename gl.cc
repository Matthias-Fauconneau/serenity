#include "gl.h"

#define GL_GLEXT_PROTOTYPES
#include  <GL/gl.h>
#include <GL/glu.h>

/// Context

void glCullFace(bool enable) { if(enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE); }
void glDepthTest(bool enable) { if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void glBlend(bool enable, bool add) {
    if(enable) {
        glEnable(GL_BLEND);
        if(add) {
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // alpha blend
        } else {
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            glBlendFunc(GL_ONE,GL_ONE); // substractive blend
        }
    } else glDisable(GL_BLEND);
}

/// Shader

#if GL_PROGRAM_UNIFORM
void GLUniform::operator=(float v) { glProgramUniform1f(program,location,v); }
void GLUniform::operator=(vec2 v) { glProgramUniform2f(program,location,v.x,v.y); }
void GLUniform::operator=(vec4 v) { glProgramUniform4f(program,location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat4 m) { glProgramUniformMatrix4fv(program,location,1,0,m.data); }
#else
void GLUniform::operator=(int v) { glUseProgram(program); glUniform1i(location,v); }
void GLUniform::operator=(float v) { glUseProgram(program); glUniform1f(location,v); }
void GLUniform::operator=(vec2 v) { glUseProgram(program); glUniform2f(location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { glUseProgram(program); glUniform3f(location,v.x,v.y,v.z); }
void GLUniform::operator=(vec4 v) { glUseProgram(program); glUniform4f(location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat3 m) { glUseProgram(program); glUniformMatrix3fv(location,1,0,m.data); }
void GLUniform::operator=(mat4 m) { glUseProgram(program); glUniformMatrix4fv(location,1,0,m.data); }
#endif
GLUniform GLShader::operator[](const char* name) {
    int location = uniformLocations.value(name,-1);
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
    if(!success) error("Compile failed");
    glAttachShader(id, shader);
}
GLShader::GLShader(const ref<byte> &vertex, const ref<byte> &fragment) {
    id = glCreateProgram();
    compile(GL_VERTEX_SHADER, vertex);
    compile(GL_FRAGMENT_SHADER, fragment);
    glLinkProgram(id);
    int length;
    glGetProgramiv(id , GL_INFO_LOG_LENGTH , &length);
    if(length>1) {
        string buffer(length); buffer.setSize(length-1);
        glGetProgramInfoLog(id, length, 0, buffer.data());
        if(buffer) log(buffer);
    }
    int success=0;
    glGetProgramiv(id, GL_LINK_STATUS, &success);
    if(!success) error("Link failed");
}
void GLShader::bind() { glUseProgram(id); }
uint GLShader::attribLocation(const char* name) {
    int location = attribLocations.value(name,-1);
    if(location<0) attribLocations.insert(name,location=glGetAttribLocation(id,name));
    if(location<0) error("Unknown attribute"_,name);
    return (uint)location;
}
void GLShader::bindSamplers(const char* tex0) { operator[](tex0) = 0; }

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
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
vec2 project(int x, int y) { return vec2(2*x/viewportSize.x-1,1-2*y/viewportSize.y); }
void glDrawRectangle(GLShader& shader, Rect rect, bool texCoord) {
    glDrawRectangle(shader, project(rect.min.x,rect.max.y), project(rect.max.x,rect.min.y), texCoord);
}

/// Texture

GLTexture::GLTexture(const Image& image) : width(image.width), height(image.height), alpha(image.alpha) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    assert(width==image.stride);
    glTexImage2D(GL_TEXTURE_2D,0,alpha?GL_RGBA:GL_RGB,width,height,0,alpha?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,image.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
GLTexture::GLTexture(int width, int height, int format) : width(width), height(height) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if((format&3)==sRGB)
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,0); //FIXME: sRGB
    if((format&3)==Depth24)
        glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32,width,height,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_BYTE,0);
    if((format&3)==RGB16F)
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_FLOAT,0); //FIXME: float
    if((format&3)==Multisample)
        //glTexImage2DMultisample(GL_TEXTURE_2D,4,GL_RGB,width,height,false); //FIXME: multisample
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_FLOAT,0); //FIXME: float
    if(format&Shadow) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL );
    }
    if(format&Bilinear) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if(format&Anisotropic) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 2.0);
    if(format&Clamp) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}
GLTexture::~GLTexture() { if(id) glDeleteTextures(1,&id); id=0; }
void GLTexture::bind(uint sampler) const { assert(id); glActiveTexture(GL_TEXTURE0+sampler); glBindTexture(GL_TEXTURE_2D, id); }
void GLTexture::bindSamplers(const GLTexture& tex0) { tex0.bind(0); }

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(GLTexture&& color):color(move(color)){
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    if(this->depth.id) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,this->depth.id,0);
    else {
        glGenRenderbuffers(1, &depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, color.width, color.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    }
    if(this->color.id) glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,this->color.id,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) error("");
}
GLFrameBuffer::~GLFrameBuffer() { if(depthBuffer) glDeleteRenderbuffers(1, &depthBuffer); glDeleteFramebuffers(1,&id); }
void GLFrameBuffer::bind(bool clear, vec4 clearColor) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
  if(depth.id) glViewport(0,0,depth.width,depth.height), viewportSize = vec2(depth.size());
  else glViewport(0,0,color.width,color.height), viewportSize = vec2(color.size());
  if(clear) {
      glClearColor(clearColor.x,clearColor.y,clearColor.z,clearColor.w);
      glClear( ((depth.id||depthBuffer)?GL_DEPTH_BUFFER_BIT:0) | (color.id?GL_COLOR_BUFFER_BIT:0) );
  }
}
void GLFrameBuffer::bindWindow(int2 position, int2 size, bool clear, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(position.x,position.y,size.x,size.y);
  viewportSize = vec2(size);
  if(clear) {
      glClearColor(color.x,color.y,color.z,color.w);
      glClear(GL_COLOR_BUFFER_BIT);
  }
}
