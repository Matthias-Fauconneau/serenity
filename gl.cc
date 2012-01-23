#include "gl.h"
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"

/// State
#if DEBUG
#include "GL/glu.h"
#define glCheck ({ auto e=glGetError(); if(e) { log(strz((const  char*)gluErrorString(e)));abort(); } })
#else
#define glCheck
#endif

static vec2 glViewportScale;
void glViewport(int2 size) {
    glViewportScale = vec2(2,-2)/vec2(size);
    glViewport(0,0,size.x,size.y);
}

/// Shader

bool GLShader::compileShader(uint id, uint type, const array<string>& tags) {
	string global, main;
    const char* s = &source, *e=&source+source.size;
	array<int> scope;
	for(int nest=0;s<e;) { //for each line
		const char* l=s;
		{ //[a-z]+ {
			const char* t=s; while(*t==' '||*t=='\t') t++; const char* b=t; while(*t>='a'&&*t<='z') t++;
			const string tag(b,t);
			if(t>b && *t++==' ' && *t++=='{' && *t++=='\n') { //scope
				bool skip=true;
				for(const auto& e : tags) if(tag==e) { skip=false; break; }
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
    string source = "#version 120\n"_+global+"\nvoid main() {\n"_+main+"\n}\n"_;

	uint shader = glCreateShader(type);
    glShaderSource(shader,1,(const char**)& &source,&source.size);
	(type==GL_VERTEX_SHADER ? vertex : fragment) = move(source);
	glCheck;
	glCompileShader(shader);
	glAttachShader(id,shader);
	int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
	if(status) return true;
	int l=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&l);
    string msg(l); glGetProgramInfoLog(id,l,&msg.size,(char*)&msg);
    error("Error compiling shader\n"_,msg,(type==GL_VERTEX_SHADER ? vertex : fragment));
	return false;
}

bool GLShader::compile(const array<string>& vertex, const array<string>& fragment) {
	if(!id) id = glCreateProgram();
	compileShader(id,GL_VERTEX_SHADER,vertex);
	compileShader(id,GL_FRAGMENT_SHADER,fragment);
	glLinkProgram(id);
	int status; glGetProgramiv(id,GL_LINK_STATUS,&status);
	if(status) return true;
	int l=0; glGetProgramiv(id,GL_INFO_LOG_LENGTH,&l);
    string msg(l); glGetProgramInfoLog(id,l,&msg.size,(char*)&msg);
    error("Error linking shader\n"_,msg);
	return false;
}


void GLShader::bind() {
    if(!id && name) compile({"vertex"_,strz(name)},{"fragment"_,strz(name)});
    glUseProgram(id);
    operator[]("scale")=glViewportScale;
}
uint GLShader::attribLocation(const char* name) {
	int location = attribLocations.value(name,-1);
	if(location<0) attribLocations.insert(name,location=glGetAttribLocation(id,name));
    assert(location>=0,"Unknown attribute"_,strz(name),"for vertex shader\n"_,vertex);
	return (uint)location;
}
void GLUniform::operator=(float v) { glUniform1f(id,v); }
void GLUniform::operator=(vec2 v) { glUniform2f(id,v.x,v.y); }
void GLUniform::operator=(vec4 v) { glUniform4f(id,v.x,v.y,v.z,v.w); }
void GLUniform::operator=(mat4 m) { glUniformMatrix4fv(id,1,0,m.data); }
GLUniform GLShader::operator[](const char* name) {
	int location = uniformLocations.value(name,-1);
	if(location<0) uniformLocations.insert(name,location=glGetUniformLocation(id,name));
    assert(location>=0,"Unknown uniform"_,strz(name),"\n[Vertex]\n"_,vertex,"\n[Fragment]\n"_,fragment);
	return GLUniform(location);
}

SHADER(blit);
GLShader flat(blitShader,"flat");
GLShader blit(blitShader,"blit");

/// Texture

GLTexture::GLTexture(const Image& image) : Image(image.copy()) {
	if(!id) glGenTextures(1, &id);
	assert(id);
    glBindTexture(GL_TEXTURE_2D, id);
    for(int i=0;i<width*height;i++) { //convert to darken coverage
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

void GLBuffer::upload(const array<uint32>& indices) {
	if(!indexBuffer) glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size*sizeof(uint32), &indices, GL_STATIC_DRAW);
	indexCount = indices.size;
}
void GLBuffer::upload(const array<vec2>& vertices) {
	if(!vertexBuffer) glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	vertexSize = sizeof(vec2);
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
