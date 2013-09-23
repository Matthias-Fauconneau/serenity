#pragma once
#include "widget.h"
#include "gl.h"
struct Scene;

struct View : Widget {
    View(const Scene& scene);
    void render(int2 position, int2 size);
    /*enum Sort { FrontToBack, BackToFront };
    void draw(QMap<GLShader*,QVector<Object*> >& objects,Sort sort=FrontToBack);
    void render(GLFrameBuffer& deferRender,GLFrameBuffer& targetRender,bool withShadow, bool reverseWinding=false);
signals:
    void clicked(int index);
    void fpsChanged(QString fps);
protected:
    void paintGL();
    void keyPressEvent(QKeyEvent*);
    void keyReleaseEvent(QKeyEvent*);
    void mousePressEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent*);
    void wheelEvent(QWheelEvent*);
    void timerEvent(QTimerEvent*);*/

    const Scene& scene;
    /*QString fps;
    QPoint drag;
    bool grab = 0;
    float pitch=PI/2,yaw=0,speed=2;
    int walk=0,strafe=0,jump=0;
    vec3 position;
    vec3 velocity;
    vec3 momentum;
    QBasicTimer timer;
    GLTexture depthBuffer,albedoBuffer,normalBuffer,refractionBuffer,refractionDepthBuffer,reflectionBuffer,lightBuffer,finalBuffer;
    GLFrameBuffer surfaceRender,refractionSurfaceRender,refractionRender,reflectionRender,lightRender,finalRender;
    //->Camera
    mat4 projection,inverseProjection,view; vec4 planes[6]; vec3 signs[6]; vec4 clipPlane;*/
};
