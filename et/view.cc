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

    // Updates view
    view=mat4(); view.rotateX(-pitch); view.rotateZ(-yaw); // World to view coordinates transform (i.e rotates world in the reverse direction)
    // Assumes constant frame rate (60fps)
    velocity += view.inverse()*vec3(strafe*sprint,0,-walk*sprint)+vec3(0,0,jump*sprint);
    velocity *= 31.0/32; // Velocity damping (friction)
    position += velocity;
    view.translate( -position );
    projection=mat4(); projection.perspective(PI/4, w, h, 1, 32768); inverseProjection=projection.inverse();
    // Computes view frustum planes
    mat4 m = projection*view;
    planes[0] = vec4( m(3,0) + m(0,0), m(3,1) + m(0,1), m(3,2) + m(0,2), m(3,3) + m(0,3) );
    planes[1] = vec4( m(3,0) - m(0,0), m(3,1) - m(0,1), m(3,2) - m(0,2), m(3,3) - m(0,3) );
    planes[2] = vec4( m(3,0) - m(1,0), m(3,1) - m(1,1), m(3,2) - m(1,2), m(3,3) - m(1,3) );
    planes[3] = vec4( m(3,0) + m(1,0), m(3,1) + m(1,1), m(3,2) + m(1,2), m(3,3) + m(1,3) );
    planes[4] = vec4( m(2,0), m(2,1), m(2,2), m(2,3) );
    planes[5] = vec4( m(3,0) - m(2,0), m(3,1) - m(2,1), m(3,2) - m(2,2), m(3,3) - m(2,3) );
    for(int i=0;i<6;i++) { planes[i]=normalize(planes[i]); signs[i]=sign(planes[i].xyz()); }

#if 0
    if(lightRender.depth.width!=w||lightRender.depth.height!=h) { //#OPTI: alias/reuse buffers when possible
        depthBuffer.allocate(w,h,Depth);
        albedoBuffer.allocate(w,h);
        normalBuffer.allocate(w,h);
        if(scene.water) {
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

    /*if(scene.water&&position.z>scene.water->z) {
        scene.water->render(this);
        clipPlane=vec4(0,0,1,-scene.water->z);
    } else*/ clipPlane=vec4(0,0,0,0);
    render(/*surfaceRender, lightRender, true*/);
    /*if(scene.water) {
        if( position.z>scene.water->z ) {
            GLTexture::bindSamplers(refractionDepthBuffer,refractionBuffer,reflectionBuffer);
            scene.water->compose(vec2(1.0/w,1.0/h),projection,view,scene.sky?scene.sky->fogOpacity:8192);
        } else {
            static Shader* underwater; if(!underwater) underwater=new Shader("screen deferred position underwater");
            GLShader& program = *underwater->bind();
            GLTexture::bindSamplers(depthBuffer); program.bindSamplers("depthBuffer"); program.bindFragments("color");
            float nearPlane = (projection.inverse()*vec3(0,0,-1)).z, farPlane = (projection.inverse()*vec3(0,0,1)).z;
            program["A"]= - farPlane / (farPlane - nearPlane); program["B"]= farPlane * nearPlane / (farPlane - nearPlane);
            program["inverseProjectionMatrix"]= projection.inverse();
            program["waterPlane"] = view.inverse().transpose()*vec4(0,0,-1,scene.water->z);
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

    if( norm(velocity) > 0.1 ) contentChanged(); // Keeps rendering at 60fps
}

void View::render(/*GLFrameBuffer& deferRender,GLFrameBuffer& targetRender, bool withShadow, bool reverseWinding*/) {
    // Draws opaque and alpha tested objects into G-Buffer
#if 1
    //deferRender.bind(true);
    /*if(clipPlane.x||clipPlane.y||clipPlane.z) ClipPlane=true;*/ glDepthTest(true); //if(reverseWinding) glReverseWinding();
    if(scene.opaque) { glCullFace(true); draw(scene.opaque); }
    if(scene.alphaTest) { glCullFace(false); glAlphaTest(true); glBlendAlpha(); draw(scene.alphaTest); glCullFace(true); glBlendNone(); }
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
#endif
    // Forward render transparent objects
    /*if(clipPlane.x||clipPlane.y||clipPlane.z) ClipPlane=true;*/ glDepthTest(true);  //TODO: forward lighting
    //if(reverseWinding) glReverseWinding();
    if(scene.blendAdd) {
        glBlendAdd();
        draw(scene.blendAdd);
    }
    if(scene.blendAlpha) {
      /*PolygonOffsetFill=true; glPolygonOffset(-2,-1);*/ glBlendAlpha();
      draw(scene.blendAlpha,BackToFront);
      /*PolygonOffsetFill=false;*/
    }
    //if(reverseWinding) glNormalWinding();
    /*ClipPlane=false;*/ glDepthTest(false); glBlendNone();
}

void View::draw(map<GLShader*, array<Object>>& objects, Sort /*sort*/) {
    for(pair<GLShader*, array<Object>> e: objects) {
        GLShader& program = *e.key;
        program.bind();
        //shader["fogOpacity"] = /*scene.sky ? scene.sky->fogOpacity :*/ 8192;
        //shader["clipPlane"] = clipPlane;
        program.bindFragments({"albedo"_}); //program.bindFragments({"albedo"_,"normal"_});

        array<Object>& objects = e.value;
        mat4 currentTransform=mat4(0); vec3 currentColor=0; vec3 tcScales[4]={0,0,0,0}, rgbScales[4]={0,0,0,0}; // Save current state to minimize state changes (TODO: UBOs)
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
            assert_(&shader);
            if(object.transform != currentTransform) {
                program["modelViewProjectionMatrix"_] = projection*view*object.transform;
                program["normalMatrix"_]= (view*object.transform).normalMatrix();
                GLUniform modelLightMatrix=program["modelLightMatrix"_]; // Model to world
                if(modelLightMatrix) {
                    mat4 light; light.scale(vec3(1)/(scene.gridMax-scene.gridMin)); light.translate(-scene.gridMin);
                    modelLightMatrix = light*object.transform;
                    program["viewNormalMatrix"_] = view.normalMatrix(); // World to view normal matrix
                }
                /*if(tangentSpace) { //for displacement mapping
                    program["modelViewMatrix"_]= view*object.transform;
                    program["viewOrigin"_]= (view*object.transform).inverse()*vec3(0,0,0);
                }*/
                currentTransform=object.transform;
            }
            if(object.uniformColor!=currentColor) { GLUniform uniformColor = program["uniformColor"_]; if(uniformColor) uniformColor=object.uniformColor; currentColor=object.uniformColor; }
            for(int i: range(shader.size)) {
                Texture& tex = shader[i];
                if(tex.path=="$lightmap"_) {
                    assert_(tex.type=="lightgrid"_);
                    program["lightGrid0"_]=i; scene.lightGrid[0].bind(i);
                    program["lightGrid1"_]=int(shader.size); scene.lightGrid[1].bind(shader.size);
                    program["lightGrid2"_]=int(shader.size+1); scene.lightGrid[2].bind(shader.size+1);
                }
                else {
                    if(!tex.texture) tex.upload();
                    program["tex"_+str(i)] = i;
                    assert_(i<4);
                    if(tcScales[i]!=tex.tcScale) { GLUniform uniform = program["tcScale"_+str(i)]; if(uniform) uniform=tex.tcScale; tcScales[i]=tex.tcScale; }
                    if(rgbScales[i]!=tex.rgbScale) { GLUniform uniform = program["rgbScale"_+str(i)]; if(uniform) uniform=tex.rgbScale; rgbScales[i]=tex.rgbScale; }
                    tex.texture->bind(i);
                }
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
    /**/ if(key=='w') walk++;
    else if(key=='a') strafe--;
    else if(key=='s') walk--;
    else if(key=='d') strafe++;
    else if(key==ControlKey) jump--;
    else if(key==' ') jump++;
    else if(key==ShiftKey) sprint = 8;
    else if(key=='q') velocity=vec3(0,0,0);
    else return false;
    return true; // Starts rendering (if view was still)
}
bool View::keyRelease(Key key, Modifiers) {
    if(key=='w') walk--;
    if(key=='a') strafe++;
    if(key=='s') walk++;
    if(key=='d') strafe--;
    if(key==ControlKey) jump++;
    if(key==' ') jump--;
    if(key==ShiftKey) sprint = 2;
    return false;
}
bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    setFocus(this);
    if(event==Press && button==LeftButton) { dragStart=cursor; deltaStart=vec2(yaw, pitch); }
    if(event==Release && button==LeftButton && dragStart==cursor) {
        mat4 transform; transform.perspective(PI/4, size.x, size.y, 1, 32768);
        transform.rotateX(-pitch); transform.rotateZ(-yaw); //transform.translate(-position);*/

        vec3 direction = transform.inverse() * normalize(vec3(2.0*cursor.x/size.x-1,1-2.0*cursor.y/size.y,1));
        float minZ=65536; Object* hit=0;
        for(Object* object: scene.objects) {
            mat4 toObject = object->transform.inverse();
            if(object->surface.raycast(toObject*position/*vec3(0,0,0)*/,toObject.normalMatrix()*direction,minZ)) hit=object;
        }
        if(selected) selected->uniformColor=vec3(1,1,1); // Deselect previous
        if(hit) {
            selected=hit;
            selected->uniformColor=vec3(1,0.5,0.5);
            log(selected->surface.shader->name);
            log(selected->surface.shader->source);
            log(selected->surface.shader->program->source);
        }
        return true;
    }
    if(event==Motion && button==LeftButton) {
        setDrag(this);
        vec2 delta = deltaStart - float(PI/size.x)*vec2(cursor-dragStart);
        yaw = delta.x; pitch = clip<float>(0, delta.y, PI);
        return true;
    }
    return false;
}
