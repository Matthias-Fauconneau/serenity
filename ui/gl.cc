#include "gl.h"
#include "matrix.h"
#include "data.h" //FIXME -> et/shader.cc?
#include "image.h" //FIXME -> et/shader.cc?

#undef packed
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h> //GL

/// Context
void glCullFace(bool enable) {
    if(enable) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
}
void glDepthTest(bool enable) {
    if(enable) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}
void glPolygonOffsetFill(bool enable) {
    if(enable) { glPolygonOffset(-1,-2); glEnable(GL_POLYGON_OFFSET_FILL); } else glDisable(GL_POLYGON_OFFSET_FILL);
}
static int blend = 0;
void glBlendNone() {
    if(!blend) return;
    glDisable(GL_BLEND);
    blend=0;
}
void glBlendAlpha() {
    if(!blend) glEnable(GL_BLEND);
    if(blend!=1) { glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); }
    blend=1;
}
void glBlendOneAlpha() {
    if(!blend) glEnable(GL_BLEND);
    if(blend!=2) { glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE); }
    blend=2;
}
void glBlendColor() {
    if(!blend) glEnable(GL_BLEND);
    if(blend!=3) { glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE); }
    blend=3;
}
void glBlendSubstract() {
    if(!blend) glEnable(GL_BLEND);
    if(blend!=4) { glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFuncSeparate(GL_SRC_COLOR, GL_ONE, GL_ZERO, GL_ONE); }
    blend=4;
}

/// Shader
void GLUniform::operator=(int v) { assert(location>=0); glUseProgram(program); glUniform1i(location,v); }
void GLUniform::operator=(float v) { assert(location>=0); glUseProgram(program); glUniform1f(location,v); }
void GLUniform::operator=(vec2 v) { assert(location>=0); glUseProgram(program); glUniform2f(location,v.x,v.y); }
void GLUniform::operator=(vec3 v) { assert(location>=0); glUseProgram(program); glUniform3f(location,v.x,v.y,v.z); }
void GLUniform::operator=(vec4 v) { assert(location>=0); glUseProgram(program); glUniform4f(location,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat3x2 m) { assert(location>=0); glUseProgram(program); glUniformMatrix3x2fv(location,1,0,m.data); }
void GLUniform::operator=(mat3 m) { assert(location>=0); glUseProgram(program); glUniformMatrix3fv(location,1,0,m.data); }
void GLUniform::operator=(mat4 m) { assert(location>=0); glUseProgram(program); glUniformMatrix4fv(location,1,0,m.data); }

GLUniform GLShader::operator[](string name) {
	int location = uniformLocations.value(name, -1);
    if(location<0) {
		location = glGetUniformLocation(id, strz(name));
		if(location<0) /*return GLUniform(id, location); //*/error("Unknown uniform"_,name);
		uniformLocations.insert(copyRef(name), location);
    }
	return GLUniform(id, location);
}

GLShader::GLShader(string source, ref<string> stages) {
    id = glCreateProgram();
    array<string> knownTags;
    for(uint type: (uint[]){GL_VERTEX_SHADER,GL_FRAGMENT_SHADER}) {
		array<char> global, main;
		for(size_t i: range(stages.size)) {
			buffer<string> tags = split(stages[i], " ") + (type==GL_VERTEX_SHADER?"vertex"_:"fragment"_);
			array<char> stageGlobal, stageMain;
            TextData s (source);
            array<uint> scope;
			for(uint nest=0;s;) { // for each line (FIXME: line independent)
                uint start = s.index;
                s.whileAny(" \t"_);
                string identifier = s.identifier("_!"_);
                s.whileAny(" \t"_);
                if(identifier && identifier!="else"_ && s.match("{"_)) { //scope: "[a-z]+ {"
                    bool condition=true;
                    if(startsWith(identifier,"!"_)) condition=false, identifier=identifier.slice(1);
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
                        //if(!s.match('\n')) error("Expecting newline after scope");
                    }
                    continue;
                    //start = s.index;
                }
                bool function = false;
				static array<string> types = split("void float vec2 vec3 vec4"_," ");
				static array<string> qualifiers = split("struct const uniform attribute varying in out"_," ");
                if(types.contains(identifier) && s.identifier("_"_) && s.match('(')) {
                    function = true;
                    s.until('{');
                    for(uint n=nest+1;s && n>nest;) {
                        if(s.match('{')) n++;
                        else if(s.match('}')) n--;
                        else s.advance(1);
                    }
                }
				/*if(identifier=="uniform"_ && s.match("sampler"_)) {
					if(!s.match("2D") && !s.match("Buffer")) error("Unknown sampler type", s.line());
					s.whileAny(" \t"_);
					sampler2D.append( copyRef(s.identifier("_"_)) );
				}*/
                while(s && !s.match('\n')) {
                    if(s.match('{')) nest++;
                    else if(s.match('}')) { nest--;
                        if(scope && nest==scope.last()) { //Remove matching scope closing bracket
							scope.pop(); stageMain.append( s.slice(start, s.index-1-start) ); start=s.index;
                        }
                    }
                    else s.advance(1);
                }
                string line = s.slice(start, s.index-start);
				if(trim(line) && !startsWith(trim(line), "//"_)) ((function || qualifiers.contains(identifier) || startsWith(line,"#"_)) ? stageGlobal : stageMain).append( line );
            }
			global.append( replace(stageGlobal,"$"_,str(i-1)) );
			main.append( replace(stageMain,"$"_,str(i-1)) );
        }
		this->source.append( "#version 330\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_ );
		uint shader = glCreateShader(type);
		glShaderSource(shader, 1, &this->source.last().data, (int*)&this->source.last().size);
        glCompileShader(shader);

        int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
        int length=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&length);
        if(!status || length>1) {
            ::buffer<byte> buffer(length);
            glGetShaderInfoLog(shader, length, 0, buffer.begin());
			error(this->source.last(), buffer.slice(0,buffer.size-1));
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
        error(this->source, stages, "Program failed\n", buffer.slice(0,buffer.size-1));
    }
}
void GLShader::bind() { glUseProgram(id); }
//void GLShader::bindFragments(const ref<string> &fragments) { for(uint i: range(fragments.size)) glBindFragDataLocation(id, i, fragments[i]); }
uint GLShader::attribLocation(string name) {
	int location = attribLocations.value(name, -1);
    if(location<0) {
		location=glGetAttribLocation(id, strz(name));
		if(location>=0) attribLocations.insert(copyRef(name), location);
    }
	if(location<0) error("Unknown attribute '"_+str(name)+"'"_);
    return (uint)location;
}

/// Buffer

GLBuffer::GLBuffer(uint elementSize, ref<byte> data) : elementSize(elementSize), elementCount(data.size/elementSize) {
	glGenBuffers(1, &id);
	glBindBuffer(GL_ARRAY_BUFFER, id);
	assert_(int(data.size) > 0, data.size);
	glBufferData(GL_ARRAY_BUFFER, data.size, data.data, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
GLBuffer::~GLBuffer() { if(id) glDeleteBuffers(1, &id); }

/// Vertex array

GLVertexArray::GLVertexArray() {	glGenVertexArrays(1, &id); }
GLVertexArray::~GLVertexArray() { if(id) glDeleteVertexArrays(1, &id); }
void GLVertexArray::bind() const { glBindVertexArray(id); }
void GLVertexArray::bindAttribute(int index, int elementSize, AttributeType type, const GLBuffer& buffer, uint64 offset) const {
	bind();
	glBindBuffer(GL_ARRAY_BUFFER, buffer.id);
	glVertexAttribPointer(index, elementSize, type, 0, buffer.elementSize, (void*)offset);
	glEnableVertexAttribArray(index);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void GLVertexArray::draw(PrimitiveType primitiveType, uint vertexCount) const {
	bind();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawArrays(primitiveType, 0, vertexCount);
}

#include "time.h"
void GLIndexBuffer::draw(int base /*instanceCount*/) {
	glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
	GLenum type = 0;
	if(elementSize==2) type = GL_UNSIGNED_SHORT;
	if(elementSize==4) type = GL_UNSIGNED_INT;
	assert_(type);
	glDrawElementsBaseVertex(primitiveType, elementCount, type, 0, base);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/// Texture
GLTexture::GLTexture(uint width, uint height, uint format, const void* data) : width(width), height(height), format(format),
	target((format&Cube)?GL_TEXTURE_CUBE_MAP:(format&Multisample)?GL_TEXTURE_2D_MULTISAMPLE:GL_TEXTURE_2D) {
    glGenTextures(1, &id);
	glBindTexture(target, id);
    if(format&Multisample) {
        int colorSamples=0; glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &colorSamples);
        int depthSamples=0; glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &depthSamples);
        assert(colorSamples==depthSamples);
		if(format&Depth) glTexImage2DMultisample(target, depthSamples, GL_DEPTH_COMPONENT32, width, height, false);
		else glTexImage2DMultisample(target, colorSamples, GL_RGB8, width, height, false);
	}
	else if(format&Cube) {
		assert_(width == height/6);
		for(size_t index: range(6))
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+index, 0, format&SRGB?GL_SRGB8:GL_RGB8, width, height/6, 0, GL_BGRA, GL_UNSIGNED_BYTE, ((byte4*)data)+index*height/6*width);
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	}
	else if((format&0b11) == Short)
			glTexImage2D(target, 0, GL_R16I, width, height, 0, GL_R16I, GL_SHORT, data);
	else if((format&0b11) == Depth)
			glTexImage2D(target, 0, GL_DEPTH_COMPONENT32, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, data);
	else if((format&0b11) == RGBA8)
		glTexImage2D(target, 0, format&SRGB?GL_SRGB8_ALPHA8:GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
	else {
		assert_((format&0b11) == RGB8);
		glTexImage2D(target, 0, format&SRGB?GL_SRGB8:GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
	}
    if(format&Shadow) {
		glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    }
    if(format&Bilinear) {
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, format&Mipmap?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
    if(format&Anisotropic) {
		glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0);
    }
    if(format&Clamp) {
		glTexParameterf(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
	if(format&Mipmap) glGenerateMipmap(target);
}
GLTexture::GLTexture(const Image& image, uint format)
	: GLTexture(image.width, image.height, (image.alpha?RGBA8:RGB8)|format, image.data) {
    assert(width==image.stride);
}
/*GLTexture::GLTexture(const Image16& image, uint format)
	: GLTexture(image.width, image.height, Short|format, image.data) {
	assert(width==image.stride);
}*/
GLTexture::GLTexture(const GLBuffer& buffer, int2 size, uint format) : size(size), format(format), target(GL_TEXTURE_BUFFER) {
	glGenTextures(1, &id);
	glBindTexture(target, id);
	GLenum internalFormat = 0;
	if(format == R16I) internalFormat = GL_R16I;
	if(format == R32F) internalFormat = GL_R32F;
	assert_(format);
	glTexBuffer(target, internalFormat, buffer.id);
	glBindTexture(target, 0);
}

GLTexture::~GLTexture() { if(id) glDeleteTextures(1, &id); id=0; }
void GLTexture::bind(uint sampler) const {
	assert_(id);
	glActiveTexture(GL_TEXTURE0+sampler);
	glBindTexture(target, id);
}

/// Framebuffer

GLFrameBuffer::GLFrameBuffer(GLTexture&& depth):width(depth.width),height(depth.height),depthTexture(move(depth)) {
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture.id, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}
GLFrameBuffer::GLFrameBuffer(GLTexture&& depth, GLTexture&& color)
    : width(depth.width), height(depth.height), depthTexture(move(depth)), colorTexture(move(color)) {
	assert(depth.size==color.size && (depthTexture.format&Multisample)==(colorTexture.format&Multisample));
    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);
    uint target = depthTexture.format&Multisample? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, depthTexture.id, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, colorTexture.id, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}
GLFrameBuffer::GLFrameBuffer(int2 size, int sampleCount/*, uint format*/) : size(size) {
    if(sampleCount==-1) glGetIntegerv(GL_MAX_SAMPLES,&sampleCount);

    glGenFramebuffers(1,&id);
    glBindFramebuffer(GL_FRAMEBUFFER,id);

    glGenRenderbuffers(1, &depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, GL_DEPTH_COMPONENT32, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

    glGenRenderbuffers(1, &colorBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, /*(format&0b11==RGBA8)?GL_RGBA8:*/GL_RGB8, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer);

    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}
GLFrameBuffer::~GLFrameBuffer() {
    if(depthBuffer) glDeleteRenderbuffers(1, &depthBuffer);
    if(colorBuffer) glDeleteRenderbuffers(1, &colorBuffer);
    if(id) glDeleteFramebuffers(1,&id);
}
void GLFrameBuffer::bind(uint clearFlags, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,id);
  glViewport(0,0,width,height);
  if(clearFlags) {
      if(clearFlags&ClearColor) glClearColor(color.x,color.y,color.z,color.w);
      assert((clearFlags&(~(ClearDepth|ClearColor)))==0, clearFlags);
      glClear(clearFlags);
  }
}
void GLFrameBuffer::bindWindow(int2 position, int2 size, uint clearFlags, vec4 color) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(position.x, position.y, size.x, size.y);
  if(clearFlags&ClearColor) glClearColor(color.x, color.y, color.z, color.w);
  if(clearFlags) glClear(clearFlags);
}
void GLFrameBuffer::blit(uint target) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER,id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target);
    glBlitFramebuffer(0,0,width,height,0,0,width,height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
void GLFrameBuffer::blit(GLTexture& color) {
    assert(color.width==width && color.height==height);
    uint target=0;
    glGenFramebuffers(1,&target);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.id, 0);
    blit(target);
    glDeleteFramebuffers(1,&target);
    if(color.format&Mipmap) { glBindTexture(GL_TEXTURE_2D, color.id); glGenerateMipmap(GL_TEXTURE_2D); }
}
