#include "thread.h"
#include "scene.h"
#include "view.h"
/*Ideas:
 - restore: GL window (+glXSwapIntervalSGI(1)), view, shading, lighting (etxmap), sky, shadows (soft PCF), water
 - procedural textures: +normal, specular, displacement (relaxed cone stepping, tesselation)
 - atmosphere: tonemapping, bloom, color grading, clouds, sun/moon/stars, dynamic water (waves, caustics), rain/snow, specular reflection
 - shadows: sample distribution shadow maps, exponential variance soft shadows (+multisampling)
 - indirect lighting: ambient occlusion volumes, voxel global illumination, sparse virtual textures
*/

struct ET {
    Folder data = "opt/enemy-territory/etmain"_;
    Scene scene {data.list(Files|Recursive).filter([](const string& file){return !endsWith(file,".bsp"_);}).first(), data};
    View view {scene};
} et;
