#include "gl.h"
#define GL_GLEXT_PROTOTYPES
#include "GL/gl.h"
#include "GL/glu.h"

/// State

#define glCheck ({ auto e=glGetError(); \
	if(e) { log_(__FILE__);log_(_(":"));log_(__LINE__);log_(_(": "));log_((const  char*)gluErrorString(e));log_("\n");abort(); } })

/// Shader

extern char _binary_shader_glsl_start[];
extern char _binary_shader_glsl_end[];
static string glsl = string(_binary_shader_glsl_start,_binary_shader_glsl_end);

bool GLShader::compileShader(uint id, uint type, const array<string>& tags) {
    string global, main;
	const char* s = glsl.data, *e=glsl.data+glsl.size;
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
                    for(int nest=1;nest;s++) { assert(*s,"Unmatched {"); if(*s=='{') nest++; if(*s=='}') nest--; }
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
                for(const auto& e : {_("uniform"),_("attribute"),_("varying"),_("in"),_("out")}) if(qualifier==e) { declaration=true; break; }
            }
        }
		for(;s<e && *s!='\n';s++) { if(*s=='{') nest++; if(*s=='}') nest--; } s++;
        if(scope.size && nest==scope.last()) { scope.removeLast(); continue; }
		(declaration ? global : main) << string(l,s);
    }
    string source = _("#version 120\n")+global+_("\nvoid main() {\n")+main+_("\n}\n");

	uint shader = glCreateShader(type);
    glShaderSource(shader,1,&source.data,&source.size);
	(type==GL_VERTEX_SHADER ? vertex : fragment) = move(source);
    glCompileShader(shader);
    glAttachShader(id,shader);
	int status=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&status);
    if(status) return true;
    int l=0; glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&l);
    string msg(l); glGetProgramInfoLog(id,l,&msg.size,(char*)msg.data);
	fail("Error compiling shader\n",msg.size,msg.data,vertex);
    return false;
}

bool GLShader::compile(const array<string>& vertex, const array<string>& fragment) {
    glCheck;
    if(!id) id = glCreateProgram();
    compileShader(id,GL_VERTEX_SHADER,vertex);
    compileShader(id,GL_FRAGMENT_SHADER,fragment);
    glLinkProgram(id);
    int status; glGetProgramiv(id,GL_LINK_STATUS,&status);
    if(status) return true;
    int l=0; glGetProgramiv(id,GL_INFO_LOG_LENGTH,&l);
    string msg(l); glGetProgramInfoLog(id,l,&msg.size,(char*)msg.data);
    fail("Error linking shader\n",msg);
    return false;
}

void GLShader::bind() {
	if(!id && name) {
		compile(array<string>({_("vertex"),strz(name)}),
				array<string>({_("fragment"),strz(name)}));
	}
	glUseProgram(id);
}
uint GLShader::attribLocation(const char* name ) {
	int location = attribLocations.value(name,-1);
    if(location<0) attribLocations.insert(name,location=glGetAttribLocation(id,name));
	assert(location>=0,"Unknown attribute",name,"for vertex shader:",vertex);
	return (uint)location;
}
void GLUniform::operator=(float v) { glCheck; glUniform1f(id,v); glCheck; }
void GLUniform::operator=(vec2 v) { glCheck; glUniform2f(id,v.x,v.y); glCheck; }
void GLUniform::operator=(vec4 v) { glCheck; glUniform4f(id,v.x,v.y,v.z,v.w); glCheck; }
GLUniform GLShader::operator[](const char* name) {
	int location = uniformLocations.value(name,-1);
	if(location<0) uniformLocations.insert(name,location=glGetUniformLocation(id,name));
	assert(location>=0,"Unknown uniform",name,"\nVertex:\n",vertex,"\nFragment:\n",fragment);
    return GLUniform(location);
}

GLShader flat("flat");
GLShader blit("blit");

/// Texture

GLTexture::GLTexture(const Image& image) : Image(0,image.width,image.height,image.depth) {
    glCheck;
    if(!id) glGenTextures(1, &id);
	assert(id);
    glBindTexture(GL_TEXTURE_2D, id);
	uint32 format[] = { 0, GL_ALPHA, GL_LUMINANCE_ALPHA, GL_RGB, GL_BGRA };
	if(image.depth==2) { //convert to darken coverage
		byte2* ia=(byte2*)image.data;
		for(int i=0;i<image.width*image.height;i++) ia[i].i = (ia[i].i*ia[i].a + 255*(255-ia[i].a))/255;
	}
	glTexImage2D(GL_TEXTURE_2D,0,depth,this->width=width,this->height=height,0,format[depth],GL_UNSIGNED_BYTE,image.data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glCheck;
}
void GLTexture::bind() { assert(id); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, id); glCheck; }
void GLTexture::free() { assert(id); glDeleteTextures(1,&id); id=0; }

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord) {
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
