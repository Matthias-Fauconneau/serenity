#include "thread.h"
#include "scene.h"
#include "view.h"
#include "window.h"
/*Ideas:
 - restore: shader (build-time compile), view, shading, lighting (etxmap), sky, shadows (soft PCF), water
 - procedural textures: +normal, specular, displacement (relaxed cone stepping, tesselation)
 - atmosphere: tonemapping, bloom, color grading, clouds, sun/moon/stars, dynamic water (waves, caustics), rain/snow, specular reflection
 - shadows: sample distribution shadow maps, exponential variance soft shadows (+multisampling)
 - indirect lighting: ambient occlusion volumes, voxel global illumination, sparse virtual textures
*/

struct ET {
    Window window{0, int2(640,480),"ET Map Viewer"_,Image(),Window::OpenGL};

    Folder data = "opt/enemy-territory/etmain"_;
    Scene scene {data.list(Files|Recursive).filter([](const string& file){return !endsWith(file,".bsp"_);}).first(), data};
    View view {scene};
    ET() { window.widget=&view; window.show(); }
} et;
