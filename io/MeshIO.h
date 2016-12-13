#include "Path.h"
#include "primitives/Triangle.h"
#include "primitives/Vertex.h"
#include <string>
#include <vector>

bool load(const Path &path, std::vector<Vertex> &verts, std::vector<TriangleI> &tris);
bool save(const Path &path, const std::vector<Vertex> &verts, const std::vector<TriangleI> &tris);
