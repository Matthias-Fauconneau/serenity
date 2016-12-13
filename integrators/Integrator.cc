#include "Integrator.h"
#include "renderer/TraceableScene.h"
#include "cameras/Camera.h"
#include "io/FileUtils.h"
#include "io/ImageIO.h"
#include "io/Scene.h"
#include "Timer.h"
#include <algorithm>
#undef Type
#undef unused
#include <rapidjson/stringbuffer.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#define Type typename
#define unused __attribute((unused))

Integrator::Integrator()
: _scene(nullptr)
{
}

Integrator::~Integrator()
{
}
