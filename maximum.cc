#include "maximum.h"
#include "process.h"
#include "simd.h"

template<> string str(const v4sf& v) { return "("_+str(extractf(v,0))+", "_+str(extractf(v,1))+", "_+str(extractf(v,2))+", "_+str(extractf(v,3))+")"_; }

/// Returns the field of the radii of the maximum sphere enclosing each voxel and fitting within the boundaries
void maximum(Volume16& target, const Volume16& source) {
    int X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint marginX=source.marginX+1, marginY=source.marginY+1, marginZ=source.marginZ+1;
    const uint16* const sourceData = source;
    const uint* const offsetX = source.offsetX;
    const uint* const offsetY = source.offsetY;
    const uint* const offsetZ = source.offsetZ;
    assert_(offsetX && offsetY && offsetZ);
    uint16* const targetData = target;
    //clear(targetData, Z*Y*X); //if memoize
    //TODO: tiled, Z-order
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        uint16* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint16* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
#if 0 //FIXME: smooth first ?
                v4sf position = {float(x),float(y),float(z),0};
                for(;;) {
                    // Lookup sample offsets
                    const v4si p0 = cvtps2dq(position);
                    const uint vx0 = offsetX[extracti(p0,0)];
                    const uint vy0 = offsetY[extracti(p0,1)];
                    const uint vz0 = offsetZ[extracti(p0,2)];
                    const v4si p1 = p0 +_1i;
                    const uint vx1 = offsetX[extracti(p1,0)];
                    const uint vy1 = offsetY[extracti(p1,1)];
                    const uint vz1 = offsetZ[extracti(p1,2)];
                    // Loads samples
                    const v4si icx0 = {sourceData[vx0 + vy0 + vz0], sourceData[vx0 + vy0 + vz1], sourceData[vx0 + vy1 + vz0], sourceData[vx0 + vy1 + vz1]};
                    const v4si icx1 = {sourceData[vx1 + vy0 + vz0], sourceData[vx1 + vy0 + vz1], sourceData[vx1 + vy1 + vz0], sourceData[vx1 + vy1 + vz1]};
                    // Trilinear interpolation
                    const v4sf cx0 = cvtdq2ps(icx0);
                    const v4sf cx1 = cvtdq2ps(icx1);
                    const v4sf pc = position - cvtdq2ps(p0);
                    const v4sf _1mpc = _1f - pc;
                    const v4sf z0011 = shuffle(_1mpc, pc, 2,2,2,2);
                    const v4sf z0101 = shuffle(z0011, z0011, 0,2,0,2);
                    const v4sf y0011 = shuffle(_1mpc, pc, 1,1,1,1);
                    const v4sf y0101 = shuffle(y0011, y0011, 0,2,0,2);
                    const v4sf x0000 = shuffle(_1mpc, _1mpc, 0,0,0,0);
                    const v4sf x0011 = shuffle(_1mpc, pc, 0,0,0,0);
                    const v4sf x1111 = shuffle(pc, pc, 0,0,0,0);
                    const v4sf sw_yz = z0101 * y0011;
                    const v4sf sample = dot4(sw_yz, x0000*cx0 + x1111*cx1);
                    // Discrete gradient
                    const v4sf dx = y0011*z0101*(cx0-cx1);
                    const v4sf dy = x0011*z0101*(shuffle(cx0,cx1, 0,1,0,1)-shuffle(cx0,cx1, 2,3,2,3));
                    const v4sf dz = x0011*y0101*(shuffle(cx0,cx1, 0,2,0,2)-shuffle(cx0,cx1, 1,3,1,3));
                    const v4sf dp = transpose(dx, dy, dz, _0001f);
                    if(extractf(dot3(dp,dp),0)<=3) { // Local maximum
                        targetZY[x] = uint16(extractf(sample,0));
                        break;
                    }
                    position += _halff * dp;
                }
#else
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
                    //if(max>d) { d=max, ox+=stepX, oy+=stepY, oz+=stepZ; continue; }
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
                    //if(max>d) { d=max, ox+=stepX, oy+=stepY, oz+=stepZ; continue; }
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
#endif
            }
        }
    });
    target.marginX=marginX, target.marginY=marginY, target.marginZ=marginZ;
}
