#include "MeshIO.h"
#include "FileUtils.h"
#include "ObjLoader.h"

bool loadWo3(const Path &path, std::vector<Vertex> &verts, std::vector<TriangleI> &tris)
{

    InputStreamHandle stream = FileUtils::openInputStream(path);
    if (!stream)
        return false;

    uint64 numVerts, numTris;
    FileUtils::streamRead(stream, numVerts);
    verts.resize(size_t(numVerts));
    FileUtils::streamRead(stream, verts);
    FileUtils::streamRead(stream, numTris);
    tris.resize(size_t(numTris));
    FileUtils::streamRead(stream, tris);

    return true;
}

bool loadObj(const Path &path, std::vector<Vertex> &verts, std::vector<TriangleI> &tris)
{
    return ObjLoader::loadGeometryOnly(path, verts, tris);
}

bool load(const Path &path, std::vector<Vertex> &verts, std::vector<TriangleI> &tris)
{
    if (path.testExtension("wo3"))
        return loadWo3(path, verts, tris);
    else if (path.testExtension("obj"))
        return loadObj(path, verts, tris);
    return false;
}
