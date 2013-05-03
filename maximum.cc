#include "maximum.h"
#include "simd.h"

/// Returns the field of the radii of the maximum sphere enclosing each voxel and fitting within the boundaries
void maximum(Volume16& target, const Volume16& source) {
    int X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    const uint16* const sourceData = source;
    const uint* const offsetX = source.offsetX;
    const uint* const offsetY = source.offsetY;
    const uint* const offsetZ = source.offsetZ;
    assert(offsetX && offsetY && offsetZ);
    uint16* const targetData = target;
    //clear(targetData, Z*Y*X); //if memoize
    //TODO: tiled, Z-order
    for(int z=1; z<Z-1; z++) {
        uint16* const targetZ = targetData+z*XY;
        for(int y=1; y<Y-1; y++) {
            uint16* const targetZY = targetZ+y*X;
            for(int x=1; x<X-1; x++) {
                #define distance(x,y,z) sourceData[offsetX[x]+offsetY[y]+offsetZ[z]]
                int d = distance(x,y,z);
                int ox = x, oy = y, oz = z; //Origin of the enclosing sphere
                for(;;) { // Ascent distance field until reaching a local maximum
                    //{uint16 max = targetData[oz*XY+oy*X+ox]; if(max) { targetData[z*XY+y*X+x] = max; break; }} // Reuse already computed paths //FIXME: break result ?!
                    // 6 first neighbours
                    int max=d, stepX=0, stepY=0, stepZ=0;
                    {int nd = distance(ox,oy,oz-1); if(nd > max) max=nd, stepX=0, stepY=0, stepZ=-1;}
                    {int nd = distance(ox,oy-1,oz); if(nd > max) max=nd, stepX=0, stepY=-1, stepZ=0;}
                    {int nd = distance(ox-1,oy,oz); if(nd > max) max=nd, stepX=-1, stepY=0, stepZ=0;}
                    {int nd = distance(ox+1,oy,oz); if(nd > max) max=nd, stepX=+1, stepY=0, stepZ=0;}
                    {int nd = distance(ox,oy+1,oz); if(nd > max) max=nd, stepX=0, stepY=+1, stepZ=0;}
                    {int nd = distance(ox,oy,oz+1); if(nd > max) max=nd, stepX=0, stepY=0, stepZ=+1;}
                    if(max>d) { d=max, ox+=stepX, oy+=stepY, oz+=stepZ; continue; }
                    // 12 edge neighbours
                    {int nd = distance(ox,oy-1,oz-1); if(nd > max) max=nd, stepX=0, stepY=-1, stepZ=-1;}
                    {int nd = distance(ox,oy-1,oz+1); if(nd > max) max=nd, stepX=0, stepY=-1, stepZ=+1;}
                    {int nd = distance(ox,oy+1,oz-1); if(nd > max) max=nd, stepX=0, stepY=+1, stepZ=-1;}
                    {int nd = distance(ox,oy+1,oz+1); if(nd > max) max=nd, stepX=0, stepY=+4, stepZ=+1;}

                    {int nd = distance(ox-1,oy,oz-1); if(nd > max) max=nd, stepX=-1, stepY=0, stepZ=-1;}
                    {int nd = distance(ox+1,oy,oz-1); if(nd > max) max=nd, stepX=+1, stepY=0, stepZ=-1;}
                    {int nd = distance(ox-1,oy,oz+1); if(nd > max) max=nd, stepX=-1, stepY=0, stepZ=+1;}
                    {int nd = distance(ox+1,oy,oz+1); if(nd > max) max=nd, stepX=+1, stepY=0, stepZ=+1;}

                    {int nd = distance(ox-1,oy-1,oz); if(nd > max) max=nd, stepX=-1, stepY=-1, stepZ=0;}
                    {int nd = distance(ox+1,oy-1,oz); if(nd > max) max=nd, stepX=+1, stepY=-1, stepZ=0;}
                    {int nd = distance(ox-1,oy+1,oz); if(nd > max) max=nd, stepX=-1, stepY=+1, stepZ=0;}
                    {int nd = distance(ox+1,oy+1,oz); if(nd > max) max=nd, stepX=+1, stepY=+1, stepZ=0;}
                    if(max>d) { d=max, ox+=stepX, oy+=stepY, oz+=stepZ; continue; }
                    // 8 corner neighbours
                    {int nd = distance(ox-1,oy-1,oz-1); if(nd > max) max=nd, stepX=-1, stepY=-1, stepZ=-1;}
                    {int nd = distance(ox+1,oy-1,oz-1); if(nd > max) max=nd, stepX=+1, stepY=-1, stepZ=-1;}
                    {int nd = distance(ox-1,oy+1,oz-1); if(nd > max) max=nd, stepX=-1, stepY=+1, stepZ=-1;}
                    {int nd = distance(ox+1,oy+1,oz-1); if(nd > max) max=nd, stepX=+1, stepY=+1, stepZ=-1;}
                    {int nd = distance(ox-1,oy-1,oz+1); if(nd > max) max=nd, stepX=-1, stepY=-1, stepZ=+1;}
                    {int nd = distance(ox+1,oy-1,oz+1); if(nd > max) max=nd, stepX=+1, stepY=-1, stepZ=+1;}
                    {int nd = distance(ox-1,oy+1,oz+1); if(nd > max) max=nd, stepX=-1, stepY=+1, stepZ=+1;}
                    {int nd = distance(ox+1,oy+1,oz+1); if(nd > max) max=nd, stepX=+1, stepY=+1, stepZ=+1;}
                    if(max>d) { d=max, ox+=stepX, oy+=stepY, oz+=stepZ; continue; }
                    break;
                }
                targetZY[x] = d;
            }
        }
    }
    target.num=source.num, target.den=source.den;
}
