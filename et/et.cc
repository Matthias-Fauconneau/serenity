#include "thread.h"
#include "scene.h"
#include "view.h"
#include "window.h"
/*Ideas:
 - MSAA, dynamic lighting (sun, lights (etxmap), shadows (soft PCF))
 - procedural textures: +normal, specular, displacement (relaxed cone stepping, tesselation)
 - atmosphere: tonemapping, bloom, color grading, specular reflection, refraction, waves, caustics, rain/snow
 - shadows: sample distribution shadow maps, exponential variance soft shadows (+multisampling)
 - indirect lighting: ambient occlusion volumes, voxel global illumination, sparse virtual textures
*/

struct ET {
    Window window{0, int2(1050,1050),"ET Map Viewer"_,Image(),Window::OpenGL};

    Folder data = "opt/enemy-territory/etmain"_;
    Scene scene {data.list(Files|Recursive).filter([](const string& file){return !endsWith(file,".bsp"_);}).first(), data};
    View view {scene};
    ET() {
        window.localShortcut(Escape).connect([]{exit();});
        window.widget=window.focus=window.directInput=&view;
        view.contentChanged.connect(&window,&Window::render);
        view.statusChanged.connect([this](string status){window.setTitle(status);});
        window.clearBackground = false;
        window.show();
    }
} et;
