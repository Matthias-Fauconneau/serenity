#include "maximum.h"
#include "simd.h"

/// Returns the field of the radii of the maximum sphere enclosing each voxel and fitting within the boundaries
void maximum(Volume32& maximum, const Volume32& distance) {
    int X=distance.x, Y=distance.y, Z=distance.z, XY=X*Y;
    const uint32* const data = distance;
    const uint* const offsetX = distance.offsetX;
    const uint* const offsetY = distance.offsetY;
    const uint* const offsetZ = distance.offsetZ;
    uint32* const dst = maximum;
    clear(dst, Z*Y*X);
    //TODO: use tiled target, process volume in Z-order
    for(int z=1; z<Z-1; z++) {
        for(int y=1; y<Y-1; y++) {
            for(int x=1; x<X-1; x++) {
#if 0 //FIXME: smooth first
                v4sf position = {float(x),float(y),float(z),0};
                for(;;) {
                    // Lookup sample offsets
                    const v4si p0 = cvtps2dq(position);
                    const uint vx0 = offsetX[extract(p0,0)];
                    const uint vy0 = offsetY[extract(p0,1)];
                    const uint vz0 = offsetZ[extract(p0,2)];
                    const v4si p1 = p0 +_1i;
                    const uint vx1 = offsetX[extract(p1,0)];
                    const uint vy1 = offsetY[extract(p1,1)];
                    const uint vz1 = offsetZ[extract(p1,2)];
                    // Loads samples
                    const v4si icx0 = {data[vx0 + vy0 + vz0], data[vx0 + vy0 + vz1], data[vx0 + vy1 + vz0], data[vx0 + vy1 + vz1]};
                    const v4si icx1 = {data[vx1 + vy0 + vz0], data[vx1 + vy0 + vz1], data[vx1 + vy1 + vz0], data[vx1 + vy1 + vz1]};
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
                    if(extract(dot3(dp,dp),0)<=1) { // Local maximum
                        dst[z*XY+y*X+x]= uint16(16*extract(sample,0));
                        break;
                    }
                    position += _halff * dp;
                }
#else
                #define distance(x,y,z) data[offsetX[x]+offsetY[y]+offsetZ[z]]
                int d = distance(x,y,z);
                int ox = x, oy = y, oz = z; //Origin of the enclosing sphere
                for(;;) { // Ascent distance field until reaching a local maximum
                    //{int max = dst[oz*XY+oy*X+ox]; if(max) { dst[z*XY+y*X+x] = max; break; }} // Reuse already computed paths //FIXME: break result ?!
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

                    break;
                }
                //if((ox-x)*(ox-x)+(oy-y)*(oy-y)>d) qDebug()<<x<<y<<ox<<oy<<ox-x<<oy-y<<d<<(ox-x)*(ox-x)+(oy-y)*(oy-y); // Enclosing sphere FIXME
                dst[z*XY+y*X+x]= 16*sqrt(d);
#endif
            }
        }
    }
    maximum.num = 1, maximum.den=0x100;
}
