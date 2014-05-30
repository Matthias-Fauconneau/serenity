#include "project.h"
#include "thread.h"

void projectGL(const ImageF& image, const GLTexture& volume, const Projection& projection) {
    SHADER(project)
    GLShader& program = projectShader;

    program["rotation"_] = projection.glRotation;
    program["origin"_] = projection.glOrigin;
    float halfHeight = float(volume.size.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
    program["plusMinusHalfHeightMinusOriginZ"_] = vec2(1,-1)*halfHeight - vec2(projection.glOrigin.z);
    float radius = float(volume.size.x-1)/2;
    program["c"_] = sq(projection.glOrigin.xy()) - radius*radius;
    program["radiusSq"_] = radius*radius;
    program["halfHeight"_] = halfHeight;
    program["dataOrigin"_] = projection.glOrigin + vec3(volume.size-int3(1,1,2/*FIXME*/))/2.f;

    program.bindSamplers({"volume"_}); volume.bind(0);
    GLFrameBuffer target = GLTexture(image.size());
    target.bind(ClearColor);

    GLVertexBuffer vertexBuffer;
    vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)});
    vertexBuffer.bindAttribute(program,"position"_,2);
    vertexBuffer.draw(TriangleStrip);

    target.texture.read(image);
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume = source;
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        v4sf start, step, end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) { row[x] = intersect(projection, vec2(x,y), volume, start, step, end) ? project(start, step, end, volume, source.data) : 0; }
    }, coreCount);
}
