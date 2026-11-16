#pragma once
#include <cmath>
#include <cstdint>

namespace TouchTransform {
struct Point { float rawX, rawY, x, y; };
struct Affine {
    float ax=0, bx=1, cx=0, ay=-1, by=0, cy=239;
    bool valid=false;
};
inline bool finite(const Affine &a) {
    return std::isfinite(a.ax)&&std::isfinite(a.bx)&&std::isfinite(a.cx)&&
           std::isfinite(a.ay)&&std::isfinite(a.by)&&std::isfinite(a.cy);
}
// Four measured corners fit the transform. The fifth centre point is held out.
// Row reduction uses double precision to avoid native-coordinate conditioning loss.
inline bool fit(const Point *p, int count, Affine &a) {
    if(count<3) return false;
    double m[3][5]={};
    for(int i=0;i<count;i++) {
        const double v[]={p[i].rawX,p[i].rawY,1};
        for(int r=0;r<3;r++) {
            for(int c=0;c<3;c++) m[r][c]+=v[r]*v[c];
            m[r][3]+=v[r]*p[i].x; m[r][4]+=v[r]*p[i].y;
        }
    }
    for(int c=0;c<3;c++) {
        int pivot=c;
        for(int r=c+1;r<3;r++) if(std::fabs(m[r][c])>std::fabs(m[pivot][c])) pivot=r;
        if(std::fabs(m[pivot][c])<1e-7) return false;
        for(int j=0;j<5;j++){double v=m[c][j];m[c][j]=m[pivot][j];m[pivot][j]=v;}
        double d=m[c][c]; for(int j=c;j<5;j++) m[c][j]/=d;
        for(int r=0;r<3;r++) if(r!=c){double f=m[r][c];for(int j=c;j<5;j++)m[r][j]-=f*m[c][j];}
    }
    Affine result;result.ax=m[0][3];result.bx=m[1][3];result.cx=m[2][3];
    result.ay=m[0][4];result.by=m[1][4];result.cy=m[2][4];result.valid=true;
    if(!finite(result)||std::fabs(result.ax*result.by-result.bx*result.ay)<0.05f) return false;
    a=result;return true;
}
inline float error(const Affine &a, const Point &p) {
    const float dx=a.ax*p.rawX+a.bx*p.rawY+a.cx-p.x;
    const float dy=a.ay*p.rawX+a.by*p.rawY+a.cy-p.y;
    return std::sqrt(dx*dx+dy*dy);
}
inline bool map(const Affine &a, int rx, int ry, int16_t &x, int16_t &y) {
    if(!a.valid||!finite(a)||rx<0||ry<0||rx>4095||ry>4095)return false;
    const float xf=a.ax*rx+a.bx*ry+a.cx, yf=a.ay*rx+a.by*ry+a.cy;
    if(xf < -16 || xf>335 || yf < -16 || yf>255)return false;
    x=std::lround(xf);y=std::lround(yf);
    if(x<0)x=0;if(x>319)x=319;if(y<0)y=0;if(y>239)y=239;
    return true;
}
}
