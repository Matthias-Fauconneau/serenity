#include "view.h"
#include "scene.h"
#include "object.h"
#include "shader.h"
#include "file.h"
#include "gl.h"

View::View(Scene &scene) : scene(scene) {
    vec4 position_yaw = scene.defaultPosition();
    position = position_yaw.xyz(), yaw=position_yaw.w;
}

void View::render(int2, int2 size) {
    int w = size.x, h = size.y;
#if 0
    if(lightRender.depth.width!=w||lightRender.depth.height!=h) { //#OPTI: alias/reuse buffers when possible
        depthBuffer.allocate(w,h,Depth);
        albedoBuffer.allocate(w,h);
        normalBuffer.allocate(w,h);
        if(scene->water) {
            refractionDepthBuffer.allocate(w,h,Depth);
            refractionBuffer.allocate(w,h);
            reflectionBuffer.allocate(w,h,Float);
        }
        lightBuffer.allocate(w,h/*,Float|Mipmap*/);
        finalBuffer.allocate(w,h,Bilinear|Clamp); //Gamma|
        surfaceRender.attach(depthBuffer,albedoBuffer,normalBuffer);
        refractionSurfaceRender.attach(refractionDepthBuffer,albedoBuffer,normalBuffer);
        refractionRender.attach(refractionDepthBuffer,refractionBuffer); refractionRender.depthWrite=false;
        reflectionRender.attach(depthBuffer,reflectionBuffer); reflectionRender.depthWrite=false;
        lightRender.attach(depthBuffer,lightBuffer); lightRender.depthWrite=false;
        finalRender.attach(depthBuffer,finalBuffer); finalRender.depthWrite=false;
    }
#endif
    projection=mat4(); projection.perspective(PI/4, w, h, 1, 16384); inverseProjection=projection.inverse();
    view=mat4(); view.rotateX(-pitch); view.rotateZ(-yaw); view.translate( -position );
    /*if(scene->water&&position.z>scene->water->z) {
        scene->water->render(this);
        clipPlane=vec4(0,0,1,-scene->water->z);
    } else*/ clipPlane=vec4(0,0,0,0);
    render(/*surfaceRender, lightRender, true*/);
    /*if(scene->water) {
        if( position.z>scene->water->z ) {
            GLTexture::bindSamplers(refractionDepthBuffer,refractionBuffer,reflectionBuffer);
            scene->water->compose(vec2(1.0/w,1.0/h),projection,view,scene->sky?scene->sky->fogOpacity:8192);
        } else {
            static Shader* underwater; if(!underwater) underwater=new Shader("screen deferred position underwater");
            GLShader& program = *underwater->bind();
            GLTexture::bindSamplers(depthBuffer); program.bindSamplers("depthBuffer"); program.bindFragments("color");
            float nearPlane = (projection.inverse()*vec3(0,0,-1)).z, farPlane = (projection.inverse()*vec3(0,0,1)).z;
            program["A"]= - farPlane / (farPlane - nearPlane); program["B"]= farPlane * nearPlane / (farPlane - nearPlane);
            program["inverseProjectionMatrix"]= projection.inverse();
            program["waterPlane"] = view.inverse().transpose()*vec4(0,0,-1,scene->water->z);
            DepthTest=false; glBlendAlpha(); renderQuad(); glBlendNone();
        }
    }*/
    /*{static Shader* tonemap; if(!tonemap) tonemap=new Shader("screen deferred tonemap"); GLShader& program = *tonemap->bind();
        finalRender.bind(); program.bindFragments("color");
        if(enableFXAA) lightBuffer.generateMipmap();
        else GLFrameBuffer::bindWindow(w,h);
        GLTexture::bindSamplers(lightBuffer); program.bindSamplers("tex");
        program["deviceScale"]=vec2(1.0/w,1.0/h);
        DepthTest=false; renderQuad();
    }
    if(enableFXAA) {
        static Shader* fxaa; if(!fxaa) fxaa=new Shader("screen deferred fxaa"); GLShader& program = *fxaa->bind();
        GLFrameBuffer::bindWindow(w,h); program.bindFragments("color");
        GLTexture::bindSamplers(finalBuffer); program.bindSamplers("tex");
        program["deviceScale"]=vec2(1.0/w,1.0/h);
        DepthTest=false; renderQuad();
    }*/
}

void View::render(/*GLFrameBuffer& deferRender,GLFrameBuffer& targetRender, bool withShadow, bool reverseWinding*/) {
    // Computes view frustum planes
    mat4 m = projection*view;
    planes[0] = vec4( m(3,0) + m(0,0), m(3,1) + m(0,1), m(3,2) + m(0,2), m(3,3) + m(0,3) );
    planes[1] = vec4( m(3,0) - m(0,0), m(3,1) - m(0,1), m(3,2) - m(0,2), m(3,3) - m(0,3) );
    planes[2] = vec4( m(3,0) - m(1,0), m(3,1) - m(1,1), m(3,2) - m(1,2), m(3,3) - m(1,3) );
    planes[3] = vec4( m(3,0) + m(1,0), m(3,1) + m(1,1), m(3,2) + m(1,2), m(3,3) + m(1,3) );
    planes[4] = vec4( m(2,0), m(2,1), m(2,2), m(2,3) );
    planes[5] = vec4( m(3,0) - m(2,0), m(3,1) - m(2,1), m(3,2) - m(2,2), m(3,3) - m(2,3) );
    for(int i=0;i<6;i++) { planes[i]=normalize(planes[i]); signs[i]=sign(planes[i].xyz()); }

    // Draws opaque and alpha tested objects into G-Buffer
#if 1
    //deferRender.bind(true);
    /*if(clipPlane.x||clipPlane.y||clipPlane.z) ClipPlane=true;*/ glDepthTest(true); //if(reverseWinding) glReverseWinding();
    if(scene.opaque) { glCullFace(true); draw( scene.opaque ); }
    //if(scene.alphaTest) { glCullFace(false); /*AlphaTest=true;*/ draw( scene.alphaTest ); glCullFace(true); /*AlphaTest=false;*/ }
#endif
    // Draws lights using G-Buffer
#if 0
    /*ClipPlane=false;*/ DepthTest=false; if(reverseWinding) glNormalWinding();
    targetRender.bind(true); deferRender.bindSamplers();
    //DepthBoundsTest=true;
    glBlendAdd();
    static GLShader program; if(!program.id) program.compile(getGLSL(QStringList("screen deferred position light"),"vertex"),
                                                             getGLSL(QStringList("screen deferred position light"),"fragment")
                                                             .replace("%",QString::number(1)));
    program.bind();
    program.bindSamplers("depthBuffer","albedoBuffer","normalBuffer");
    program.bindFragments("color");
    float nearPlane=(inverseProjection*vec3(0,0,-1)).z, farPlane=(inverseProjection*vec3(0,0,1)).z;
    program["A"]= farPlane/(nearPlane-farPlane); program["B"]= farPlane*nearPlane/(nearPlane-farPlane);
    program["fogOpacity"]=scene.sky?scene.sky->fogOpacity:8192;
    program["inverseProjectionMatrix"]=inverseProjection;

    for(int n=0;n<scene.lights.count();n++) { Light* light = &scene.lights[n]; //OPTI: query visible
        // View frustum culling
        int shortcut=light.planeIndex;
        if( dot(light.origin, planes[shortcut].xyz()) <= -planes[shortcut].w-light.radius ) goto cull;
        for(int i=0;i<6;i++) if( dot(light.origin, planes[i].xyz())+planes[i].w <= -light.radius ) {
            light.planeIndex=i;
            goto cull;
        }
        {
            light.project(projection,view);
            vec4 lightPosition[1]; vec3 lightColor[1];
            lightPosition[0]=vec4(light.center.x,light.center.y,light.center.z,light.radius);
            program["lightPosition"].set(lightPosition,1);
            lightColor[0]=light.color;
            program["lightColor"].set(lightColor,1);
            light.clipMin.z=(light.clipMin.z+1)/2;
            light.clipMax.z=(light.clipMax.z+1)/2;
            renderQuad(light.clipMin,light.clipMax);
        }
        cull: ;
    }

    //DepthBoundsTest=false;
    if(scene.sky) scene.sky->render(projection,view,scene,deferRender,targetRender,withShadow); //TODO: cull sky indoor

    // Forward render transparent objects
    /*if(clipPlane.x||clipPlane.y||clipPlane.z) ClipPlane=true;*/ DepthTest=true;  //TODO: forward lighting
    if(reverseWinding) glReverseWinding();
    glBlendAdd();
    if(scene.blendAdd.count()) draw(scene.blendAdd);
    if(scene.blendAlpha.count()) {
      /*PolygonOffsetFill=true; glPolygonOffset(-2,-1);*/ glBlendAlpha();
      draw(scene.blendAlpha,BackToFront);
      /*PolygonOffsetFill=false;*/
    }
    if(reverseWinding) glNormalWinding();
    /*ClipPlane=false;*/ DepthTest=false; glBlendNone();
#endif
}

void View::draw(map<GLShader*, array<Object>>& objects, Sort /*sort*/) {
    for(pair<GLShader*, array<Object>> e: objects) {
        //GLShader program("vertex {\n attribute vec4 position;\n uniform mat4 modelViewProjectionMatrix;\n gl_Position = modelViewProjectionMatrix * position;\n }\n fragment {\n out vec4 color;\n color = vec4(1,1,1,1);\n }\n"_);
        GLShader& program = *e.key;
        program.bind();
        //shader["fogOpacity"] = /*scene.sky ? scene.sky->fogOpacity :*/ 8192;
        //shader["clipPlane"] = clipPlane;
        program.bindFragments({"albedo"_,"normal"_});
        program.bindSamplers({"tex0"_,"tex1"_,"tex2"_,"tex3"_});

        array<Object>& objects = e.value;
        mat4 currentTransform=mat4(0); vec3 currentColor; vec3 tcScales[4],rgbScales[4]; // Save current state to minimize state changes (TODO: UBOs)
#ifdef VIEW_FRUSTUM_CULLING
        QMap<float,Object*> depthSort; //useless without proper partitionning
        for(int n=0;n<objects.count();n++) { Object* object = objects[n];
            int shortcut=object.planeIndex;
            if( dot(object.center+object.extent*signs[shortcut], planes[shortcut].xyz())+planes[shortcut].w < 0 ) goto cull;
            for(int i=0;i<6;i++) {
                if( dot(object.center+object.extent*signs[i], planes[i].xyz())+planes[i].w < 0 ) {
                    object.planeIndex=i; goto cull;
                }
            }
            depthSort.insertMulti( (sort==FrontToBack?-1:+1)*(view*object.center).z, objects[n] );
            cull: ;
        }
        foreach(Object* object,depthSort.values()) {
#else
        for(Object& object: objects) {
#endif
            Shader& shader = *object.surface.shader;
            if(object.transform != currentTransform) {
                program["modelViewProjectionMatrix"_] = projection*view*object.transform;
                program["normalMatrix"_]= (view*object.transform).normalMatrix();
                /*if(tangentSpace) { //for displacement mapping
                    program["modelViewMatrix"_]= view*object.transform;
                    program["viewOrigin"_]= (view*object.transform).inverse()*vec3(0,0,0);
                }*/
                currentTransform=object.transform;
            }
            if(object.uniformColor!=currentColor) { GLUniform uniformColor = program["uniformColor"_]; if(uniformColor) uniformColor=object.uniformColor; currentColor=object.uniformColor; }
            for(uint i: range(shader.size)) {
                Texture& tex = shader[i];
                if(!tex.texture) tex.upload();
                string tcNames[] = {"tcScale0"_,"tcScale1"_,"tcScale2"_,"tcScale3"_};
                if(tcScales[i]!=tex.tcScale) { program[tcNames[i]]=tex.tcScale; tcScales[i]=tex.tcScale; }
                string rgbNames[] = {"rgbScale0"_,"rgbScale1"_,"rgbScale2"_,"rgbScale3"_};
                if(rgbScales[i]!=tex.rgbScale) { program[rgbNames[i]]=tex.rgbScale; rgbScales[i]=tex.rgbScale; }
                tex.texture->bind(i);
            }
            object.surface.draw(program, true, true, shader.vertexBlend, shader.tangentSpace);

            /*if(object.uniformColor!=vec3(1,1,1)) { // Shows bounding box (for debugging)
                CullFace=false;
                static Shader* debug; if(!debug) debug=new Shader("transform debug"); GLShader& program = *debug->bind();
                program.bindFragments("albedo","normal");
                program["modelViewProjectionMatrix"]= projection*view;
                program["normalMatrix"]= view.normalMatrix();
                renderBox(program,object.center-object.extent,object.center+object.extent);
                CullFace=true;
                shader.bind();
            }*/
        }
    }
}

bool View::keyPress(Key key, Modifiers) {
    if(key=='w') walk++;
    if(key=='a') strafe--;
    if(key=='s') walk--;
    if(key=='d') strafe++;
    //if(key==Control) jump--; //modifiers&Control
    if(key==' ') jump++;
    //if(key==Shift) speed=8; //modifiers&Shift
    if(key=='q') velocity=vec3(0,0,0);
    //if(key=='x') enableFXAA=!enableFXAA;
    return true;
}
bool View::keyRelease(Key key, Modifiers) {
    if(key=='w') walk--;
    if(key=='a') strafe++;
    if(key=='s') walk++;
    if(key=='d') strafe--;
    //if(key==Control) jump++;
    if(key==' ') jump--;
    //if(key==Shift) speed=2;
    return false;
}
bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    // TODO: Wheel -> velocity=vec3(0,0,e->delta()/60);
    if(event==Press && button==LeftButton) { dragStart=cursor; deltaStart=vec2(yaw, pitch); }
    if(event==Release && button==LeftButton && dragStart==cursor) {
        /*mat4 transform; transform.perspective(PI/4, (float)width()/height(), 1, 16384);
        transform.rotateX(-pitch); transform.rotateZ(-yaw);
        vec3 direction = transform.inverse() * normalize(vec3(2.0*e->x()/width()-1,1-2.0*e->y()/height(),1));
        float minZ=65536; int hit=-1;
        for(int i=0;i<scene->objects.count();i++) { if(scene->objects[i].surface.shader->name.contains("caulk")) continue;
            mat4 toObject = scene->objects[i].transform.inverse();
            if(scene->objects[i].surface->raycast(toObject*position,toObject.normalMatrix()*direction,minZ)) hit=i;
        }
        static Object* selected;
        if(selected) selected->uniformColor=vec3(1,1,1);
        if(hit>=0) { selected=&scene->objects[hit]; selected->uniformColor=vec3(1,0.5,0.5); }*/
    }
    if(event==Motion && button==LeftButton) {
        setDrag(this);
        vec2 delta = deltaStart + float(PI/size.x)*vec2(cursor-dragStart);
        yaw = delta.x; pitch = clip<float>(0, delta.y, PI);
        return true;
    }
    return false;
}
/* TODO: 60fps
    mat4 view; view.rotateZ(yaw); view.rotateX(pitch);
    velocity += view*vec3(strafe*speed,0,-walk*speed)+vec3(0,0,jump*speed);
    velocity *= 31.0/32; position += velocity;
    if( length(velocity) > 0.1 ) update(); else timer.stop();
*/
