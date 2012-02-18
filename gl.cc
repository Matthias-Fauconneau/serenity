#include "gl.h"
//TODO: reduced header
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"

/// State
#if DEBUG
#include "GL/glu.h"
#define glCheck ({ auto e=glGetError(); if(e) { log(strz((const  char*)gluErrorString(e)));abort(); } })
#else
#define glCheck
#endif

vec2 viewport;
void glViewport(int2 size) {
    viewport = vec2(2,-2)/vec2(size);
    glViewport(0,0,size.x,size.y);
}

/// Shader

void GLShader::bind() {
    if(!id) {
        id = glCreateProgram();
        GLint formats = 0;
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        GLint binaryFormats[formats];
        glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, binaryFormats);
        glProgramBinary(id, binaryFormats[0], &binary, binary.size);
        //binary.clear();
        int success=0; glGetProgramiv(id, GL_LINK_STATUS, &success);
        assert(success);
    }
    glUseProgram(id);
}
uint GLShader::attribLocation(const char* name) {
    int location = attribLocations.value(name,-1);
    if(location<0) attribLocations.insert(name,location=glGetAttribLocation(id,name));
    assert(location>=0,"Unknown attribute"_,strz(name));
    return (uint)location;
}
void GLUniform::operator=(float v) { glUniform1f(id,v); }
void GLUniform::operator=(vec2 v) { glUniform2f(id,v.x,v.y); }
void GLUniform::operator=(vec4 v) { glUniform4f(id,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat4 m) { glUniformMatrix4fv(id,1,0,m.data); }
GLUniform GLShader::operator[](const char* name) {
    int location = uniformLocations.value(name,-1);
    if(location<0) uniformLocations.insert(name,location=glGetUniformLocation(id,name));
    assert(location>=0,"Unknown uniform"_,strz(name));
    return GLUniform(location);
}

/// Texture

GLTexture::GLTexture(const Image& image) : Image(image.copy()) {
    if(!id) glGenTextures(1, &id);
    assert(id);
    glBindTexture(GL_TEXTURE_2D, id);
    for(int i=0;i<width*height;i++) { //convert alpha to multiply blend
        data[i].r = (data[i].r*data[i].a + 255*(255-data[i].a))/255;
        data[i].g = (data[i].g*data[i].a + 255*(255-data[i].a))/255;
        data[i].b = (data[i].b*data[i].a + 255*(255-data[i].a))/255;
    }
    glTexImage2D(GL_TEXTURE_2D,0,4,width,height,0,GL_BGRA,GL_UNSIGNED_BYTE,data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}
void GLTexture::bind() const { bind(id); }
void GLTexture::bind(int id) { assert(id); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, id); }
void GLTexture::free() { assert(id); glDeleteTextures(1,&id); id=0; }

/// Buffer

GLBuffer::GLBuffer(PrimitiveType primitiveType) : primitiveType(primitiveType) {}
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

void GLBuffer::upload(const array<int>& indices) {
    if(!indexBuffer) glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size*sizeof(uint32), &indices, GL_STATIC_DRAW);
    indexCount = indices.size;
}
void GLBuffer::upload(const array<byte> &vertices) {
    if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size*vertexSize, &vertices, GL_STATIC_DRAW);
    vertexCount = vertices.size;
}
void GLBuffer::bindAttribute(GLShader& program, const char* name, int elementSize, uint64 offset) {
    assert(vertexBuffer);
    int location = program.attribLocation(name);
    assert(location>=0,"unused attribute"_,strz(name));
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
        glCheck;
        glDrawArrays(primitiveType, 0, vertexCount);
    }
    glCheck;
}

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord) {
    glCheck;
    glBindBuffer(GL_ARRAY_BUFFER,0);
    uint positionIndex = shader.attribLocation("position");
    vec2 positions[] = { vec2(min.x,min.y), vec2(max.x,min.y), vec2(max.x,max.y), vec2(min.x,max.y) };
    glVertexAttribPointer(positionIndex,2,GL_FLOAT,0,0,positions);
    glEnableVertexAttribArray(positionIndex);
    uint texCoordIndex;
    if(texCoord) {
        texCoordIndex = shader.attribLocation("texCoord");
        vec2 texCoords[] = { vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1) };
        glVertexAttribPointer(texCoordIndex,2,GL_FLOAT,0,0,texCoords);
        glEnableVertexAttribArray(texCoordIndex);
    }
    glDrawArrays(GL_QUADS,0,4);
    glDisableVertexAttribArray(positionIndex);
    if(texCoord) glDisableVertexAttribArray(texCoordIndex);
}

void glWireframe(bool wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK,wireframe?GL_LINE:GL_FILL);
}
